package main

import (
	"context"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	"github.com/godbus/dbus/v5"
	"golang.org/x/sys/unix"
)

var noAttribute = errors.New("attribute not found")

type attCharacteristic struct {
	declaration, handle uint16
	properties          byte
	uuid                []byte
}
type rawATT struct {
	file   *os.File
	mtu    int
	tx, rx uint16
}

func (a *rawATT) Close() error { return a.file.Close() }

func (a *rawATT) SetReadDeadline(t time.Time) error  { return a.file.SetReadDeadline(t) }
func (a *rawATT) SetWriteDeadline(t time.Time) error { return a.file.SetWriteDeadline(t) }
func (a *rawATT) send(packet []byte) error {
	if len(packet) > a.mtu {
		return fmt.Errorf("ATT packet exceeds negotiated MTU")
	}
	if err := a.file.SetWriteDeadline(time.Now().Add(5 * time.Second)); err != nil {
		return err
	}
	n, err := a.file.Write(packet)
	if err != nil {
		return err
	}
	if n != len(packet) {
		return fmt.Errorf("short ATT write")
	}
	return nil
}
func (a *rawATT) receive() ([]byte, error) {
	buffer := make([]byte, 517)
	for {
		n, err := a.file.Read(buffer)
		if err != nil {
			return nil, err
		}
		if n < 1 || n > a.mtu {
			return nil, fmt.Errorf("invalid ATT receive length")
		}
		packet := buffer[:n]
		if packet[0] == 2 {
			if n != 3 || binary.LittleEndian.Uint16(packet[1:]) < 23 {
				return nil, fmt.Errorf("invalid peer MTU")
			}
			if err := a.send([]byte{3, 5, 2}); err != nil {
				return nil, err
			}
			continue
		}
		// This client intentionally exposes an empty GATT server and does not set
		// Client Supported Features. BlueZ therefore sends immediate notifications,
		// avoiding its 10ms multiple-notification batching timer on this connection.
		switch packet[0] {
		case 4, 8, 0x0a, 0x0c, 0x10, 0x12, 0x16, 0x18:
			handle := uint16(0)
			if n >= 3 {
				handle = binary.LittleEndian.Uint16(packet[1:])
			}
			response := []byte{1, packet[0], byte(handle), byte(handle >> 8), 0x0a}
			if err := a.send(response); err != nil {
				return nil, err
			}
			continue
		case 0x1d:
			if err := a.send([]byte{0x1e}); err != nil {
				return nil, err
			}
			continue
		}
		return append([]byte{}, packet...), nil
	}
}
func (a *rawATT) request(packet []byte, expected byte) ([]byte, error) {
	if err := a.send(packet); err != nil {
		return nil, err
	}
	if err := a.file.SetReadDeadline(time.Now().Add(5 * time.Second)); err != nil {
		return nil, err
	}
	response, err := a.receive()
	if err != nil {
		return nil, err
	}
	if response[0] == 1 {
		if len(response) != 5 || response[1] != packet[0] {
			return nil, fmt.Errorf("invalid ATT error")
		}
		if response[4] == 0x0a {
			return nil, noAttribute
		}
		return nil, fmt.Errorf("ATT error opcode=%02x code=%02x", packet[0], response[4])
	}
	if response[0] != expected {
		return nil, fmt.Errorf("unexpected ATT opcode %02x expected %02x", response[0], expected)
	}
	return response, nil
}
func (a *rawATT) Write(data []byte) (int, error) {
	if len(data) > a.mtu-3 {
		return 0, fmt.Errorf("characteristic write exceeds MTU")
	}
	packet := append([]byte{0x52, byte(a.rx), byte(a.rx >> 8)}, data...)
	if err := a.send(packet); err != nil {
		return 0, err
	}
	return len(data), nil
}
func (a *rawATT) Read(buffer []byte) (int, error) {
	packet, err := a.receive()
	if err != nil {
		return 0, err
	}
	if len(packet) < 3 || packet[0] != 0x1b || binary.LittleEndian.Uint16(packet[1:]) != a.tx {
		return 0, fmt.Errorf("unexpected ATT notification")
	}
	if len(packet)-3 > len(buffer) {
		return 0, fmt.Errorf("notification would be truncated")
	}
	return copy(buffer, packet[3:]), nil
}
func wireUUID(uuid string) []byte {
	decoded, err := hex.DecodeString(strings.ReplaceAll(uuid, "-", ""))
	if err != nil {
		return nil
	}
	for i, j := 0, len(decoded)-1; i < j; i, j = i+1, j-1 {
		decoded[i], decoded[j] = decoded[j], decoded[i]
	}
	return decoded
}
func sameUUID(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
func groupRequest(opcode byte, start, end, typ uint16) []byte {
	return []byte{opcode, byte(start), byte(start >> 8), byte(end), byte(end >> 8), byte(typ), byte(typ >> 8)}
}
func (a *rawATT) discover() (uint16, uint16, error) {
	var start, end uint16
	for cursor := uint32(1); cursor <= 65535; {
		response, err := a.request(groupRequest(0x10, uint16(cursor), 65535, 0x2800), 0x11)
		if errors.Is(err, noAttribute) {
			break
		}
		if err != nil {
			return 0, 0, err
		}
		if len(response) < 2 || (response[1] != 6 && response[1] != 20) || len(response) < 2+int(response[1]) || (len(response)-2)%int(response[1]) != 0 {
			return 0, 0, fmt.Errorf("invalid ATT service list")
		}
		last := uint16(0)
		for pos := 2; pos < len(response); pos += int(response[1]) {
			previous := last
			first := binary.LittleEndian.Uint16(response[pos:])
			last = binary.LittleEndian.Uint16(response[pos+2:])
			if first == 0 || last < first || uint32(first) < cursor || first <= previous {
				return 0, 0, fmt.Errorf("invalid service handles")
			}
			if sameUUID(response[pos+4:pos+int(response[1])], wireUUID(serviceUUID)) {
				start, end = first, last
			}
		}
		if start != 0 {
			break
		}
		if uint32(last) < cursor {
			return 0, 0, fmt.Errorf("non-progressing service discovery")
		}
		cursor = uint32(last) + 1
	}
	if start == 0 {
		return 0, 0, fmt.Errorf("GBA Link service absent")
	}
	var chars []attCharacteristic
	for cursor := uint32(start); cursor <= uint32(end); {
		response, err := a.request(groupRequest(8, uint16(cursor), end, 0x2803), 9)
		if errors.Is(err, noAttribute) {
			break
		}
		if err != nil {
			return 0, 0, err
		}
		if len(response) < 2 || (response[1] != 7 && response[1] != 21) || len(response) < 2+int(response[1]) || (len(response)-2)%int(response[1]) != 0 {
			return 0, 0, fmt.Errorf("invalid characteristic list")
		}
		last := uint16(0)
		for pos := 2; pos < len(response); pos += int(response[1]) {
			previous := last
			last = binary.LittleEndian.Uint16(response[pos:])
			handle := binary.LittleEndian.Uint16(response[pos+3:])
			if uint32(last) < cursor || last <= previous || handle <= last || handle > end {
				return 0, 0, fmt.Errorf("invalid characteristic handles")
			}
			chars = append(chars, attCharacteristic{last, handle, response[pos+2], append([]byte{}, response[pos+5:pos+int(response[1])]...)})
		}
		cursor = uint32(last) + 1
	}
	var txHandle, rxHandle, descriptorEnd uint16
	for _, ch := range chars {
		if sameUUID(ch.uuid, wireUUID(txUUID)) && ch.properties&0x10 != 0 {
			txHandle = ch.handle
			descriptorEnd = end
		}
		if sameUUID(ch.uuid, wireUUID(rxUUID)) && ch.properties&4 != 0 {
			rxHandle = ch.handle
		}
	}
	if txHandle == 0 || rxHandle == 0 {
		return 0, 0, fmt.Errorf("GBA Link data characteristics absent")
	}
	for _, ch := range chars {
		if ch.declaration > txHandle && ch.declaration-1 < descriptorEnd {
			descriptorEnd = ch.declaration - 1
		}
	}
	for cursor := uint32(txHandle) + 1; cursor <= uint32(descriptorEnd); {
		request := []byte{4, byte(cursor), byte(cursor >> 8), byte(descriptorEnd), byte(descriptorEnd >> 8)}
		response, err := a.request(request, 5)
		if err != nil {
			return 0, 0, err
		}
		if len(response) < 2 {
			return 0, 0, fmt.Errorf("invalid descriptor response")
		}
		size := 4
		if response[1] == 2 {
			size = 18
		} else if response[1] != 1 {
			return 0, 0, fmt.Errorf("invalid descriptor format")
		}
		if len(response) < 2+size || (len(response)-2)%size != 0 {
			return 0, 0, fmt.Errorf("invalid descriptor list")
		}
		last := uint16(0)
		for pos := 2; pos < len(response); pos += size {
			previous := last
			last = binary.LittleEndian.Uint16(response[pos:])
			if uint32(last) < cursor || last <= previous || last > descriptorEnd {
				return 0, 0, fmt.Errorf("invalid descriptor handles")
			}
			if size == 4 && binary.LittleEndian.Uint16(response[pos+2:]) == 0x2902 {
				response, err := a.request([]byte{0x12, byte(last), byte(last >> 8), 1, 0}, 0x13)
				if err != nil {
					return 0, 0, err
				}
				if len(response) != 1 {
					return 0, 0, fmt.Errorf("invalid subscribe response")
				}
				return txHandle, rxHandle, nil
			}
		}
		cursor = uint32(last) + 1
	}
	return 0, 0, fmt.Errorf("notification descriptor absent")
}

func centralRaw(ctx context.Context, conn *dbus.Conn, expected string, count, length int, delay time.Duration) error {
	return centralRawSession(ctx, conn, expected, nil, func(ctx context.Context, endpoint duplexEndpoint, payload int) error {
		if probeDuplex {
			epoch, err := randomDuplexEpoch()
			if err != nil {
				return err
			}
			return duplexBenchmark(ctx, endpoint, payload, epoch, count, length, false)
		}
		return probeDataExchange(ctx, endpoint, endpoint, payload, count, length, delay)
	})
}

func centralRawSession(ctx context.Context, conn *dbus.Conn, expected string, progress func(byte, string) error, handler func(context.Context, duplexEndpoint, int) error) error {
	return centralSession(ctx, conn, expected, false, progress, handler)
}

func centralSelectedSession(ctx context.Context, conn *dbus.Conn, expected string, progress func(byte, string) error, handler func(context.Context, duplexEndpoint, int) error) error {
	return centralSession(ctx, conn, expected, true, progress, handler)
}

func centralSession(ctx context.Context, conn *dbus.Conn, expected string, selected bool, progress func(byte, string) error, handler func(context.Context, duplexEndpoint, int) error) error {
	var device dbus.ObjectPath
	var err error
	if !selected {
		device, err = findRoom(ctx, conn, expected)
		if err != nil {
			return err
		}
	}
	objs, err := managed(ctx, conn)
	if err != nil {
		return err
	}
	if selected {
		device = selectedRoomPath(objs, expected)
		if device == "" {
			return fmt.Errorf("selected room unavailable; search again")
		}
	}

	properties := objs[device]["org.bluez.Device1"]
	if connected, _ := properties["Connected"].Value().(bool); connected {
		return fmt.Errorf("peer already connected; raw probe will not reuse it")
	}
	address, _ := properties["Address"].Value().(string)
	if progress != nil {
		if err := progress('C', "Connecting to room..."); err != nil {
			return err
		}
	}
	parts := strings.Split(address, ":")
	if len(parts) != 6 {
		return fmt.Errorf("invalid discovered address")
	}
	addressType, _ := properties["AddressType"].Value().(string)
	typ := uint8(unix.BDADDR_LE_PUBLIC)
	if addressType == "random" {
		typ = unix.BDADDR_LE_RANDOM
	}
	remote := &unix.SockaddrL2{CID: 4, AddrType: typ}
	for i, part := range parts {
		decoded, err := hex.DecodeString(part)
		if err != nil || len(decoded) != 1 {
			return fmt.Errorf("invalid discovered address")
		}
		remote.Addr[i] = decoded[0]
	}
	fd, err := unix.Socket(unix.AF_BLUETOOTH, unix.SOCK_SEQPACKET|unix.SOCK_CLOEXEC|unix.SOCK_NONBLOCK, unix.BTPROTO_L2CAP)
	if err != nil {
		return err
	}
	file := os.NewFile(uintptr(fd), "owned BLE ATT")
	defer file.Close()
	if err := unix.SetsockoptString(fd, unix.SOL_BLUETOOTH, 4, // BT_SECURITY from Linux bluetooth.h
		string([]byte{1, 0})); err != nil {
		return err
	}
	if err := unix.Bind(fd, &unix.SockaddrL2{CID: 4, AddrType: unix.BDADDR_LE_PUBLIC}); err != nil {
		return err
	}
	err = unix.Connect(fd, remote)
	if err != nil && err != unix.EINPROGRESS {
		return fmt.Errorf("raw ATT connect: %w", err)
	}
	// A previously disconnected peer was checked above. From this accepted
	// connect attempt onward, cleanup owns this peer even if ATT setup fails.
	defer call(context.Background(), conn, device, "org.bluez.Device1.Disconnect")
	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-ctx.Done():
			file.Close()
		case <-done:
		}
	}()
	if err != nil {
		deadline := time.Now().Add(10 * time.Second)
		for {
			if ctx.Err() != nil {
				return ctx.Err()
			}
			if time.Now().After(deadline) {
				return fmt.Errorf("raw ATT connection timeout")
			}
			ready := []unix.PollFd{{Fd: int32(fd), Events: unix.POLLOUT}}
			if _, err := unix.Poll(ready, 100); err != nil && err != unix.EINTR {
				return err
			}
			if ready[0].Revents == 0 {
				continue
			}
			code, err := unix.GetsockoptInt(fd, unix.SOL_SOCKET, unix.SO_ERROR)
			if err != nil {
				return err
			}
			if code != 0 {
				return fmt.Errorf("raw ATT connect SO_ERROR: %w", syscallError(code))
			}
			// Tina can report writable with SO_ERROR=0 before ATT reaches
			// BT_CONNECTED. L2CAP_CONNINFO validates the actual socket state.
			_, infoErr := unix.GetsockoptString(fd, unix.SOL_L2CAP, 2)
			if infoErr == nil {
				break
			}
			if infoErr != nil && infoErr != unix.ENOTCONN && infoErr != unix.EAGAIN {
				return fmt.Errorf("ATT connection state: %w", infoErr)
			}
			if ready[0].Revents&(unix.POLLHUP|unix.POLLERR|unix.POLLNVAL) != 0 {
				return fmt.Errorf("ATT peer closed before connection completed")
			}
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-time.After(100 * time.Millisecond):
			}
		}
	}
	// The kernel ATT socket established this new peer connection; clean up that
	// peer only, including BlueZ's automatic server-side bookkeeping.
	att := &rawATT{file: file, mtu: 517}
	response, err := att.request([]byte{2, 5, 2}, 3)
	if err != nil {
		return fmt.Errorf("raw ATT MTU negotiation: %w", err)
	}
	if len(response) != 3 {
		return fmt.Errorf("invalid MTU response")
	}
	mtu := int(binary.LittleEndian.Uint16(response[1:]))
	if mtu < 23 {
		return fmt.Errorf("MTU below ATT minimum")
	}
	if mtu < att.mtu {
		att.mtu = mtu
	}
	att.tx, att.rx, err = att.discover()
	if err != nil {
		return fmt.Errorf("raw GATT discovery: %w", err)
	}
	if err := requestFastInterval(ctx, address); err != nil {
		return err
	}
	payload := att.mtu - 3
	if payload > 512 {
		payload = 512
	}
	fmt.Printf("RAW_ATT_READY mtu=%d payload=%d tx=%d rx=%d\n", att.mtu, payload, att.tx, att.rx)
	if err := att.SetReadDeadline(time.Time{}); err != nil {
		return err
	}
	return handler(ctx, att, payload)
}
func syscallError(code int) error { return unix.Errno(code) }
