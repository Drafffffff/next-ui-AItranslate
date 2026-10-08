package main

import (
	"context"
	"errors"
	"testing"
	"time"
)

func TestBluetoothPowerHandoff(t *testing.T) {
	for _, initial := range []bluetoothPowerState{{true, true, true}, {true, false, true}, {false, false, false}, {false, false, true}} {
		state := initial
		starts, powers, inspections := 0, 0, 0
		err := ensureBluetoothPower(context.Background(), func(context.Context) (bluetoothPowerState, error) {
			inspections++
			if initial.daemon && !initial.adapter && inspections == 2 {
				state.adapter = true
			}
			return state, nil
		}, func(context.Context) error { starts++; state = bluetoothPowerState{true, false, true}; return nil }, func(context.Context) error { powers++; state.powered = true; return nil })
		expectedStart := 0
		if !initial.daemon && !initial.adapter {
			expectedStart = 1
		}
		expectedPower := 1
		if initial.powered {
			expectedPower = 0
		}
		if err != nil || starts != expectedStart || powers != expectedPower {
			t.Fatalf("initial=%+v start=%d power=%d err=%v", initial, starts, powers, err)
		}
	}
}
func TestBluetoothPowerFailureAndCancel(t *testing.T) {
	denied := errors.New("access denied")
	for _, stage := range []string{"inspect", "start", "power"} {
		state := bluetoothPowerState{}
		if stage == "power" {
			state = bluetoothPowerState{true, false, true}
		}
		err := ensureBluetoothPower(context.Background(), func(context.Context) (bluetoothPowerState, error) {
			if stage == "inspect" {
				return state, denied
			}
			return state, nil
		}, func(context.Context) error { return denied }, func(context.Context) error { return denied })
		if !errors.Is(err, denied) {
			t.Fatalf("%s failure lost: %v", stage, err)
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
	defer cancel()
	calls := 0
	err := ensureBluetoothPower(ctx, func(context.Context) (bluetoothPowerState, error) { return bluetoothPowerState{daemon: true}, nil }, func(context.Context) error { calls++; return nil }, func(context.Context) error { calls++; return nil })
	if !errors.Is(err, context.DeadlineExceeded) || calls != 0 {
		t.Fatalf("restarted existing service on cancellation calls=%d err=%v", calls, err)
	}
}
