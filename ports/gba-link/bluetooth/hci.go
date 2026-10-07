package main

import (
	"encoding/binary"
	"fmt"
)

type hciStatusError struct {
	opcode uint16
	status byte
}

func (e *hciStatusError) Error() string {
	return fmt.Sprintf("controller command 0x%04x rejected, status 0x%02x", e.opcode, e.status)
}

// Ignore unrelated completions from BlueZ. Raw HCI command tools can observe
// other commands' replies when the system adapter is already busy.
func hciCommandReply(packet []byte, opcode uint16) (byte, bool) {
	if len(packet) < 3 || packet[0] != 4 || len(packet) != 3+int(packet[2]) {
		return 0, false
	}
	switch packet[1] {
	case 0x0e: // Command Complete: command credits, opcode, status.
		if len(packet) >= 7 && binary.LittleEndian.Uint16(packet[4:6]) == opcode {
			return packet[6], true
		}
	case 0x0f: // Command Status: status, credits, opcode.
		if len(packet) == 7 && binary.LittleEndian.Uint16(packet[5:7]) == opcode && (packet[3] != 0 || opcode == 0x2013) {
			return packet[3], true
		}
	}
	return 0, false
}
