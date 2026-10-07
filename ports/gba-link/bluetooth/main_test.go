package main

import (
	"encoding/binary"
	"github.com/godbus/dbus/v5"
	"testing"
)

func TestRejectWriteWithoutNegotiatedSession(t *testing.T) {
	p := &Peripheral{}
	c := &Characteristic{Peripheral: p}
	cases := []map[string]dbus.Variant{
		{},
		{"device": dbus.MakeVariant(dbus.ObjectPath("/peer")), "mtu": dbus.MakeVariant(uint16(22))},
		{"device": dbus.MakeVariant(dbus.ObjectPath("/peer")), "mtu": dbus.MakeVariant(uint16(23))},
	}
	for _, options := range cases {
		if c.WriteValue(make([]byte, 21), options) == nil {
			t.Fatal("write outside negotiated size/session accepted")
		}
	}
	options := map[string]dbus.Variant{"device": dbus.MakeVariant(dbus.ObjectPath("/peer")), "mtu": dbus.MakeVariant(uint16(23))}
	if c.WriteValue(make([]byte, 8), options) == nil {
		t.Fatal("write before subscription accepted")
	}
	p.notifying = true
	p.peer = "/other"
	if c.WriteValue(make([]byte, 8), options) == nil {
		t.Fatal("another peer accepted into active session")
	}
}
func TestRejectUnrelatedHCIReply(t *testing.T) {
	valid := []byte{4, 0x0e, 4, 1, 8, 0x20, 0}
	if status, ok := hciCommandReply(valid, 0x2008); !ok || status != 0 {
		t.Fatal("valid command completion rejected")
	}
	if _, ok := hciCommandReply(valid, 0x2009); ok {
		t.Fatal("unrelated opcode accepted")
	}
	for i := 0; i < len(valid); i++ {
		if _, ok := hciCommandReply(valid[:i], 0x2008); ok {
			t.Fatal("truncated packet accepted")
		}
	}
	rejected := []byte{4, 0x0f, 4, 0x0c, 1, 0, 0}
	binary.LittleEndian.PutUint16(rejected[5:], 0x2008)
	if status, ok := hciCommandReply(rejected, 0x2008); !ok || status != 0x0c {
		t.Fatal("command rejection not preserved")
	}
}
