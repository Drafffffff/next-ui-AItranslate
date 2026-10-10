package main

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

func connectionUpdatePacket(handle, interval uint16) ([]byte, error) {
	if handle > 0x0eff || interval < 6 || interval > 3200 {
		return nil, fmt.Errorf("invalid connection timing")
	}
	packet := make([]byte, 14)
	for offset, value := range map[int]uint16{0: handle, 2: interval, 4: interval, 6: 0, 8: 100, 10: 0, 12: 0} {
		binary.LittleEndian.PutUint16(packet[offset:], value)
	}
	// Supervision timeout must exceed twice the interval for zero latency.
	if int(interval)*1250*2 >= 1000000 {
		return nil, fmt.Errorf("connection interval exceeds supervision budget")
	}
	return packet, nil
}

func ownedCentralHandle(output, address string) (uint16, error) {
	for _, line := range strings.Split(output, "\n") {
		fields := strings.Fields(line)
		if len(fields) < 9 || fields[0] != "<" || fields[1] != "LE" || !strings.EqualFold(fields[2], address) || fields[3] != "handle" || fields[5] != "state" || fields[6] != "1" || fields[8] != "MASTER" {
			continue
		}
		handle, err := strconv.ParseUint(fields[4], 10, 12)
		if err != nil {
			return 0, err
		}
		return uint16(handle), nil
	}
	return 0, fmt.Errorf("owned connected LE central link not found; timing unchanged")
}

func requestFastInterval(ctx context.Context, address string) error {
	if !legacyKernel() {
		return nil
	}
	output, err := exec.CommandContext(ctx, "hcitool", "con").Output()
	if err != nil {
		return err
	}
	handle, err := ownedCentralHandle(string(output), address)
	if err != nil {
		return err
	}
	packet, err := connectionUpdatePacket(handle, 6)
	if err != nil {
		return err
	}
	if probeEventLength != 0 {
		// Informational controller hint, in 625us units. The radio may ignore it;
		// only measured packet pacing can establish whether this changes latency.
		binary.LittleEndian.PutUint16(packet[10:], probeEventLength)
		binary.LittleEndian.PutUint16(packet[12:], probeEventLength)
		fmt.Printf("EVENT_LENGTH_HINT units=%d\n", probeEventLength)
	}
	wait, closeMonitor, err := intervalMonitor(handle, 6)
	if err != nil {
		return err
	}
	defer closeMonitor()
	for attempt := 0; attempt < 5; attempt++ {
		err := controllerCommand(0x2013, packet)
		if err == nil {
			break
		}
		var status *hciStatusError
		if !errors.As(err, &status) || status.status != 0x0c || attempt == 4 {
			return err
		}
		// BlueZ's initial connection parameter update may still be in flight.
		// A bounded retry changes only the already verified owned handle.
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(250 * time.Millisecond):
		}
	}
	if err := wait(ctx); err != nil {
		return err
	}
	fmt.Printf("INTERVAL_APPLIED handle=%d actual_us=7500\n", handle)
	return nil
}

// Ignore unrelated events and the initial BlueZ update. Only a successful
// completion for our handle and requested interval permits measurement.
func intervalCompletion(packet []byte, handle, interval uint16) (bool, error) {
	if len(packet) < 4 || packet[0] != 4 || packet[1] != 0x3e || packet[3] != 3 {
		return false, nil
	}
	if len(packet) != 13 || packet[2] != 10 {
		return false, fmt.Errorf("invalid LE interval completion")
	}
	if binary.LittleEndian.Uint16(packet[5:]) != handle {
		return false, nil
	}
	if packet[4] != 0 {
		return false, fmt.Errorf("LE interval update failed status=%02x", packet[4])
	}
	return binary.LittleEndian.Uint16(packet[7:]) == interval && binary.LittleEndian.Uint16(packet[9:]) == 0, nil
}
