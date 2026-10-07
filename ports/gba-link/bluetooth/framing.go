package main

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
)

// The BLE stream carries bounded complete messages. It does not deliver partial
// GBA packets. A fresh random epoch is negotiated for each link, never reused.
const frameHeader = 16
const maxMessage = 65536
const frameData = 1
const frameAck = 2

func frameHeaderBytes(kind byte, epoch, sequence, offset uint32) []byte {
	b := make([]byte, frameHeader)
	b[0] = 1
	b[1] = kind
	binary.BigEndian.PutUint32(b[4:], epoch)
	binary.BigEndian.PutUint32(b[8:], sequence)
	binary.BigEndian.PutUint32(b[12:], offset)
	return b
}

func fragmentMessage(payload int, epoch, sequence uint32, message []byte) ([][]byte, error) {
	if payload <= frameHeader || payload > 512 || epoch == 0 || sequence == 0 || len(message) > maxMessage {
		return nil, fmt.Errorf("invalid BLE frame bounds")
	}
	blob := make([]byte, 8+len(message))
	binary.BigEndian.PutUint32(blob, uint32(len(message)))
	binary.BigEndian.PutUint32(blob[4:], crc32.ChecksumIEEE(message))
	copy(blob[8:], message)
	frames := make([][]byte, 0, (len(blob)+payload-frameHeader-1)/(payload-frameHeader))
	for offset := 0; offset < len(blob); {
		n := len(blob) - offset
		if n > payload-frameHeader {
			n = payload - frameHeader
		}
		packet := frameHeaderBytes(frameData, epoch, sequence, uint32(offset))
		packet = append(packet, blob[offset:offset+n]...)
		frames = append(frames, packet)
		offset += n
	}
	return frames, nil
}

type assembly struct {
	epoch, next uint32
	buffer      []byte
}

func newAssembly(epoch uint32) *assembly { return &assembly{epoch: epoch, next: 1} }
func (a *assembly) reset()               { a.epoch = 0; a.next = 0; a.buffer = nil }
func (a *assembly) accept(packet []byte) (message, ack []byte, err error) {
	defer func() {
		// A malformed stream is terminal. The worker must report failure and
		// negotiate a new epoch, never continue with a partly poisoned packet.
		if err != nil {
			a.reset()
		}
	}()
	if a.epoch == 0 || len(packet) <= frameHeader || packet[0] != 1 || packet[1] != frameData || packet[2] != 0 || packet[3] != 0 {
		return nil, nil, fmt.Errorf("invalid data frame")
	}
	epoch := binary.BigEndian.Uint32(packet[4:])
	sequence := binary.BigEndian.Uint32(packet[8:])
	offset := binary.BigEndian.Uint32(packet[12:])
	data := packet[frameHeader:]
	if epoch != a.epoch || sequence == 0 {
		return nil, nil, fmt.Errorf("stale or invalid BLE session")
	}
	if a.next > 1 && sequence == a.next-1 {
		return nil, frameHeaderBytes(frameAck, a.epoch, sequence, 0), nil
	}
	if sequence != a.next {
		return nil, nil, fmt.Errorf("out-of-order BLE message")
	}
	if uint64(offset)+uint64(len(data)) > maxMessage+8 {
		return nil, nil, fmt.Errorf("BLE message exceeds queue bound")
	}
	if offset < uint32(len(a.buffer)) {
		end := uint64(offset) + uint64(len(data))
		if end > uint64(len(a.buffer)) {
			return nil, nil, fmt.Errorf("overlapping BLE fragment")
		}
		for i, value := range data {
			if a.buffer[int(offset)+i] != value {
				return nil, nil, fmt.Errorf("inconsistent duplicate BLE fragment")
			}
		}
		return nil, nil, nil
	}
	if offset != uint32(len(a.buffer)) {
		return nil, nil, fmt.Errorf("missing BLE fragment")
	}
	a.buffer = append(a.buffer, data...)
	if len(a.buffer) < 8 {
		return nil, nil, nil
	}
	total := uint64(binary.BigEndian.Uint32(a.buffer)) + 8
	if total > maxMessage+8 || uint64(len(a.buffer)) > total {
		return nil, nil, fmt.Errorf("invalid BLE message length")
	}
	if uint64(len(a.buffer)) != total {
		return nil, nil, nil
	}
	if crc32.ChecksumIEEE(a.buffer[8:]) != binary.BigEndian.Uint32(a.buffer[4:]) {
		return nil, nil, fmt.Errorf("BLE message checksum mismatch")
	}
	if a.next == ^uint32(0) {
		return nil, nil, fmt.Errorf("BLE sequence exhausted; renew session")
	}
	message = make([]byte, len(a.buffer)-8)
	copy(message, a.buffer[8:])
	a.buffer = a.buffer[:0]
	a.next++
	return message, frameHeaderBytes(frameAck, a.epoch, sequence, 0), nil
}

func validAck(packet []byte, epoch, sequence uint32) bool {
	return len(packet) == frameHeader && packet[0] == 1 && packet[1] == frameAck && packet[2] == 0 && packet[3] == 0 && binary.BigEndian.Uint32(packet[4:]) == epoch && binary.BigEndian.Uint32(packet[8:]) == sequence && binary.BigEndian.Uint32(packet[12:]) == 0
}
