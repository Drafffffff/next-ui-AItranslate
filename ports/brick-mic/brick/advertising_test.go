package main

import (
	"errors"
	"reflect"
	"testing"

	"github.com/godbus/dbus/v5"
)

func TestImmediateReceiverCannotRaceAdvertisingPreparation(t *testing.T) {
	m := &Mic{}
	var events []string
	prepared := false
	call := func(method string, path dbus.ObjectPath) error {
		if path == root {
			events = append(events, "gatt")
			return nil
		}
		if path != advert || !prepared {
			t.Fatal("receiver could connect before payload was prepared")
		}
		events = append(events, "advertise-and-connect")
		return nil
	}
	err := m.registerEndpoints(call, true, func() error {
		events = append(events, "prepare-with-radio-off")
		prepared = true
		return nil
	})
	if err != nil || !reflect.DeepEqual(events, []string{"gatt", "prepare-with-radio-off", "advertise-and-connect"}) {
		t.Fatal(events, err)
	}
	if !m.gattRegistered || !m.advertRegistered || !m.legacyConfigured {
		t.Fatal("lost teardown ownership")
	}
}

func TestAdvertisingPreparationFailureDoesNotPublish(t *testing.T) {
	m := &Mic{}
	err := m.registerEndpoints(func(_ string, path dbus.ObjectPath) error {
		if path != root {
			t.Fatal("published incomplete microphone endpoint")
		}
		return nil
	}, true, func() error { return errors.New("controller rejected payload") })
	if err == nil || !m.gattRegistered || !m.legacyConfigured || m.advertRegistered {
		t.Fatal("failed registration cannot clean up partial state", err)
	}
}

func TestModernControllerUsesBlueZWithoutLegacyCommands(t *testing.T) {
	m := &Mic{}
	var paths []dbus.ObjectPath
	err := m.registerEndpoints(func(_ string, path dbus.ObjectPath) error {
		paths = append(paths, path)
		return nil
	}, false, func() error { t.Fatal("legacy command sent on modern controller"); return nil })
	if err != nil || !reflect.DeepEqual(paths, []dbus.ObjectPath{root, advert}) || m.legacyConfigured {
		t.Fatal(paths, err)
	}
}

func TestLegacyPayloadPreparationNeverEnablesAdvertising(t *testing.T) {
	var ops []string
	var payloads [][]byte
	err := prepareLegacyAdvertising(func(op string, data []byte) error {
		ops = append(ops, op)
		payloads = append(payloads, append([]byte(nil), data...))
		return nil
	})
	if err != nil || !reflect.DeepEqual(ops, []string{"0x000a", "0x0008", "0x0009"}) {
		t.Fatal("payload prepared after enabling the radio", ops, err)
	}
	if len(payloads[0]) != 1 || payloads[0][0] != 0 || len(payloads[1]) != 32 || len(payloads[2]) != 32 {
		t.Fatal("invalid advertising payload")
	}
	if int(payloads[1][0]) != 21 || int(payloads[2][0]) != 11 {
		t.Fatal("invalid payload length prefix")
	}
}

func TestHCIRepliesMatchCommandNotLastEvent(t *testing.T) {
	cases := []struct {
		name    string
		packet  []byte
		opcode  uint16
		matched bool
		status  byte
	}{
		{"ours", []byte{4, 14, 4, 5, 8, 32, 0}, 0x2008, true, 0},
		{"other-command-failure", []byte{4, 14, 4, 5, 10, 32, 12}, 0x2008, false, 0},
		{"our-failure", []byte{4, 14, 4, 5, 8, 32, 12}, 0x2008, true, 12},
		{"other-command-success", []byte{4, 14, 4, 5, 9, 32, 0}, 0x2008, false, 0},
		{"pending", []byte{4, 15, 4, 0, 5, 8, 32}, 0x2008, false, 0},
		{"status-failure", []byte{4, 15, 4, 12, 5, 8, 32}, 0x2008, true, 12},
		{"short", []byte{4, 14, 4, 5, 8, 32}, 0x2008, false, 0},
		{"wrong-type", []byte{2, 14, 4, 5, 8, 32, 0}, 0x2008, false, 0},
		{"bad-length", []byte{4, 14, 5, 5, 8, 32, 0}, 0x2008, false, 0},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			status, matched := hciCommandReply(c.packet, c.opcode)
			if matched != c.matched || status != c.status {
				t.Fatal(status, matched)
			}
		})
	}
}
