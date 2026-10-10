package main

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"time"

	"github.com/godbus/dbus/v5"
)

type bluetoothPowerState struct{ adapter, powered, daemon bool }

// A user-selected Bluetooth link enables the shared radio. Never stop/restart
// an existing service, reset an existing controller, or restore an off state
// when the session ends: other applications may now be using it.
func ensureBluetoothPower(ctx context.Context, inspect func(context.Context) (bluetoothPowerState, error), start func(context.Context) error, power func(context.Context) error) error {
	limited, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	started, powered := false, false
	for {
		if err := limited.Err(); err != nil {
			return err
		}
		state, err := inspect(limited)
		if err != nil {
			return err
		}
		if state.adapter && state.powered {
			return nil
		}
		if !state.adapter && !state.daemon && !started {
			started = true
			if err := start(limited); err != nil {
				return fmt.Errorf("start Bluetooth: %w", err)
			}
		} else if state.adapter && !state.powered && !powered {
			powered = true
			if err := power(limited); err != nil {
				return fmt.Errorf("enable Bluetooth: %w", err)
			}
		}
		timer := time.NewTimer(100 * time.Millisecond)
		select {
		case <-limited.Done():
			timer.Stop()
			return limited.Err()
		case <-timer.C:
		}
	}
}

func enableLinkBluetooth(ctx context.Context, conn *dbus.Conn) error {
	inspect := func(ctx context.Context) (bluetoothPowerState, error) {
		devices, err := managed(ctx, conn)
		if err == nil {
			if props, ok := devices[adapter]["org.bluez.Adapter1"]; ok {
				powered, _ := props["Powered"].Value().(bool)
				return bluetoothPowerState{adapter: true, powered: powered, daemon: true}, nil
			}
		} else {
			// Only absence is recoverable; access denial and bus failures must surface.
			if e, ok := err.(dbus.Error); !ok || (e.Name != "org.freedesktop.DBus.Error.ServiceUnknown" && e.Name != "org.freedesktop.DBus.Error.NameHasNoOwner") {
				return bluetoothPowerState{}, err
			}
		}
		cmd := exec.CommandContext(ctx, "pidof", "bluetoothd")
		cmd.WaitDelay = time.Second
		err = cmd.Run()
		if err == nil {
			return bluetoothPowerState{daemon: true}, nil
		}
		if exit, ok := err.(*exec.ExitError); ok && exit.ExitCode() == 1 {
			return bluetoothPowerState{}, nil
		}
		return bluetoothPowerState{}, fmt.Errorf("inspect Bluetooth service: %w", err)
	}
	start := func(ctx context.Context) error {
		// Derive the system root from the installed helper, not shell/user input.
		executable, err := os.Executable()
		if err != nil {
			return err
		}
		executable, err = filepath.EvalSymlinks(executable)
		if err != nil {
			return err
		}
		script := filepath.Join(filepath.Dir(filepath.Dir(executable)), "etc/bluetooth/bt_init.sh")
		cmd := exec.CommandContext(ctx, script, "start")
		cmd.WaitDelay = time.Second
		output, err := cmd.CombinedOutput()
		fmt.Printf("Bluetooth start: %s", output)
		return err
	}
	power := func(ctx context.Context) error {
		return conn.Object("org.bluez", adapter).CallWithContext(ctx, "org.freedesktop.DBus.Properties.Set", 0, "org.bluez.Adapter1", "Powered", dbus.MakeVariant(true)).Err
	}
	return ensureBluetoothPower(ctx, inspect, start, power)
}
