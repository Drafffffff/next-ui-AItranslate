package main

import (
	"encoding/binary"
	"fmt"
	"hash/crc32"
)

// v4 short-message envelope: tag, full epoch, full sequence, CRC32, then data.
// ATT datagram length supplies the message length and a single fragment has
// implicit offset zero. No sequence/session/checksum bits are removed.
// Sequence one always uses the legacy format to negotiate v4 before identity.
const compactHeader = 13
const compactTag = 2

func compactFragments(payload int, epoch, sequence uint32, message []byte) ([][]byte, error) {
	if payload <= frameHeader || payload > 512 || epoch == 0 || sequence == 0 || len(message) > maxMessage {
		return nil, fmt.Errorf("invalid compact frame bounds")
	}
	if sequence == 1 || len(message)+compactHeader > payload {
		return fragmentMessage(payload, epoch, sequence, message)
	}
	packet := make([]byte, compactHeader+len(message))
	packet[0] = compactTag
	binary.BigEndian.PutUint32(packet[1:], epoch)
	binary.BigEndian.PutUint32(packet[5:], sequence)
	binary.BigEndian.PutUint32(packet[9:], crc32.ChecksumIEEE(message))
	copy(packet[compactHeader:], message)
	return [][]byte{packet}, nil
}

func expandCompact(packet []byte) ([]byte, error) {
	if len(packet) < compactHeader || packet[0] != compactTag || binary.BigEndian.Uint32(packet[1:]) == 0 || binary.BigEndian.Uint32(packet[5:]) < 2 {
		return nil, fmt.Errorf("invalid compact message")
	}
	data := packet[compactHeader:]
	expanded := frameHeaderBytes(frameData, binary.BigEndian.Uint32(packet[1:]), binary.BigEndian.Uint32(packet[5:]), 0)
	blob := make([]byte, 8+len(data))
	binary.BigEndian.PutUint32(blob, uint32(len(data)))
	copy(blob[4:8], packet[9:13])
	copy(blob[8:], data)
	return append(expanded, blob...), nil
}
