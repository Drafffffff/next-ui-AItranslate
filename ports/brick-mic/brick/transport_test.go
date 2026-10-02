package main

import (
	"encoding/json"
	"github.com/godbus/dbus/v5"
	"testing"
)

func TestReceiverAcknowledgements(t *testing.T) {
	peer := "/org/bluez/hci0/dev_01_02_03_04_05_06"
	m := &Mic{receiver: peer, state: State{Session: 7, Frames: 100}, ackFrames: 10}
	c := &Characteristic{m: m}
	ack := func(session int, frame int, device string) *dbus.Error {
		b, _ := json.Marshal(map[string]any{"op": "ack", "session": session, "frame": frame})
		return c.WriteValue(b, map[string]dbus.Variant{"device": dbus.MakeVariant(dbus.ObjectPath(device))})
	}
	if err := ack(7, 50, peer); err != nil || m.ackFrames != 50 {
		t.Fatal("valid acknowledgement rejected", err)
	}
	for _, value := range [][2]int{{6, 80}, {7, 40}, {7, 101}} {
		ack(value[0], value[1], peer)
		if m.ackFrames != 50 {
			t.Fatal("stale or impossible progress accepted")
		}
	}
	if err := ack(7, 90, "/org/bluez/hci0/dev_11_12_13_14_15_16"); err == nil || m.ackFrames != 50 {
		t.Fatal("another device can acknowledge audio")
	}
}
