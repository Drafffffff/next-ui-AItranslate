package main

import (
	"encoding/binary"
	"fmt"
	"time"

	"golang.org/x/sys/unix"
)

func controllerCommand(opcode uint16, data []byte) error {
	if len(data) > 255 {
		return fmt.Errorf("invalid HCI payload length")
	}
	fd, err := unix.Socket(unix.AF_BLUETOOTH, unix.SOCK_RAW|unix.SOCK_CLOEXEC|unix.SOCK_NONBLOCK, unix.BTPROTO_HCI)
	if err != nil {
		return fmt.Errorf("HCI socket: %w", err)
	}
	defer unix.Close(fd)
	if err := unix.Bind(fd, &unix.SockaddrHCI{Dev: 0, Channel: 0}); err != nil {
		return fmt.Errorf("HCI bind: %w", err)
	}
	// struct hci_filter, padded to 16 bytes. Restrict packet type, event and
	// opcode in the kernel, then verify the opcode again in user space.
	filter := make([]byte, 16)
	binary.LittleEndian.PutUint32(filter, 1<<4)
	binary.LittleEndian.PutUint32(filter[4:], 1<<0x0e|1<<0x0f)
	binary.LittleEndian.PutUint16(filter[12:], opcode)
	if err := unix.SetsockoptString(fd, 0, 2, string(filter)); err != nil {
		return fmt.Errorf("HCI filter: %w", err)
	}
	packet := make([]byte, 4+len(data))
	packet[0] = 1
	binary.LittleEndian.PutUint16(packet[1:], opcode)
	packet[3] = byte(len(data))
	copy(packet[4:], data)
	if n, err := unix.Write(fd, packet); err != nil || n != len(packet) {
		return fmt.Errorf("HCI write 0x%04x: %v", opcode, err)
	}
	deadline := time.Now().Add(time.Second)
	buf := make([]byte, 260)
	for time.Now().Before(deadline) {
		poll := []unix.PollFd{{Fd: int32(fd), Events: unix.POLLIN}}
		remaining := int(time.Until(deadline).Milliseconds()) + 1
		if _, err := unix.Poll(poll, remaining); err != nil {
			if err == unix.EINTR {
				continue
			}
			return fmt.Errorf("HCI poll: %w", err)
		}
		if poll[0].Revents&(unix.POLLERR|unix.POLLHUP|unix.POLLNVAL) != 0 {
			return fmt.Errorf("HCI adapter unavailable")
		}
		if poll[0].Revents&unix.POLLIN == 0 {
			continue
		}
		n, err := unix.Read(fd, buf)
		if err == unix.EAGAIN || err == unix.EINTR {
			continue
		}
		if err != nil {
			return fmt.Errorf("HCI read: %w", err)
		}
		if status, matched := hciCommandReply(buf[:n], opcode); matched {
			if status != 0 {
				return fmt.Errorf("controller command 0x%04x rejected, status 0x%02x", opcode, status)
			}
			return nil
		}
	}
	return fmt.Errorf("controller command 0x%04x timed out", opcode)
}
