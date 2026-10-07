package main

import (
	"context"
	"encoding/hex"
	"fmt"
	"os"
	"strings"

	"github.com/godbus/dbus/v5"
)

func legacyKernel() bool {
	b, err := os.ReadFile("/proc/sys/kernel/osrelease")
	return err == nil && strings.HasPrefix(string(b), "4.9.")
}

// Tina 4.9 needs the legacy controller payload before BlueZ enables advertising.
// Fail instead of disabling or replacing another application's advertisement.
func prepareLegacy(ctx context.Context, conn *dbus.Conn) error {
	if !legacyKernel() {
		return nil
	}
	var instances dbus.Variant
	if err := call(ctx, conn, adapter, "org.freedesktop.DBus.Properties.Get", "org.bluez.LEAdvertisingManager1", "ActiveInstances").Store(&instances); err != nil {
		return err
	}
	count, ok := instances.Value().(byte)
	if !ok || count != 0 {
		return fmt.Errorf("another advertisement is active; will not alter controller data")
	}
	objs, err := managed(ctx, conn)
	if err != nil {
		return err
	}
	for _, interfaces := range objs {
		if connected, _ := interfaces["org.bluez.Device1"]["Connected"].Value().(bool); connected {
			return fmt.Errorf("another Bluetooth connection is active; legacy advertising data will not be modified")
		}
	}
	if err := controllerCommand(0x200a, []byte{0}); err != nil {
		return err
	}
	uuid, err := hex.DecodeString(strings.ReplaceAll(serviceUUID, "-", ""))
	if err != nil {
		return err
	}
	for i, j := 0, len(uuid)-1; i < j; i, j = i+1, j-1 {
		uuid[i], uuid[j] = uuid[j], uuid[i]
	}
	data := append([]byte{2, 1, 6, 17, 7}, uuid...)
	name := []byte("GBA Link")
	scan := append([]byte{byte(len(name) + 1), 9}, name...)
	for i, payload := range [][]byte{data, scan} {
		if len(payload) > 31 {
			return fmt.Errorf("legacy advertising data exceeds limit")
		}
		command := make([]byte, 32)
		command[0] = byte(len(payload))
		copy(command[1:], payload)
		if err := controllerCommand(uint16(0x2008+i), command); err != nil {
			return err
		}
	}
	fmt.Println("LEGACY_ADVERTISEMENT_PREPARED")
	return nil
}
