package main

import (
	"bytes"
	"testing"
	"time"

	"github.com/godbus/dbus/v5"
)

func TestServerFDVerifiesOwnerAndFullMessage(t *testing.T) {
	probeServerFD = true
	defer func() { probeServerFD = false }()
	p := &Peripheral{}
	defer p.closeFDs()
	txChar, rxChar := &Characteristic{Peripheral: p, TX: true}, &Characteristic{Peripheral: p}
	options := map[string]dbus.Variant{"device": dbus.MakeVariant(dbus.ObjectPath("/org/bluez/hci0/dev_22_22_1C_11_93_21")), "mtu": dbus.MakeVariant(uint16(517))}
	if _, _, err := txChar.AcquireWrite(options); err == nil {
		t.Fatal("wrong characteristic accepted")
	}
	if _, _, err := txChar.AcquireNotify(options); err != nil {
		t.Fatal(err)
	}
	if _, _, err := rxChar.AcquireWrite(options); err != nil {
		t.Fatal(err)
	}
	if _, _, err := rxChar.AcquireWrite(options); err == nil {
		t.Fatal("duplicate write acquisition accepted")
	}
	other := map[string]dbus.Variant{"device": dbus.MakeVariant(dbus.ObjectPath("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF")), "mtu": dbus.MakeVariant(uint16(517))}
	if _, _, err := txChar.AcquireNotify(other); err == nil {
		t.Fatal("other peer accepted")
	}
	packets, err := fragmentMessage(512, 42, 1, []byte("complete payload"))
	if err != nil {
		t.Fatal(err)
	}
	txPeer, rxPeer := p.files[1], p.files[3]
	txPeer.SetReadDeadline(time.Now().Add(time.Second))
	if _, err := rxPeer.Write(packets[0]); err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 512)
	n, err := txPeer.Read(buf)
	if err != nil || !bytes.Equal(buf[:n], packets[0]) {
		t.Fatal("wrong echo", err)
	}
	n, err = txPeer.Read(buf)
	if err != nil || !validAck(buf[:n], 42, 1) {
		t.Fatal("wrong completion ACK", err)
	}
	p.mu.Lock()
	count := p.packets
	p.mu.Unlock()
	if count != 1 {
		t.Fatal("wrong whole message count", count)
	}
}
