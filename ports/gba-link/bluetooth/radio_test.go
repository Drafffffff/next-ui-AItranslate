package main

import (
	"testing"
	"time"

	"github.com/godbus/dbus/v5"
)

func roomReport() []byte {
	data := []byte{2, 1, 6, 17, 7, 0, 0xa1, 0xb1, 0x3c, 0x92, 0x4f, 0xd0, 0xa2, 0x31, 0x4c, 0x89, 0x7e, 0x10, 0, 0x1c, 0xba}
	p := append([]byte{4, 0x3e, 0, 2, 1, 0, 0, 0x74, 0xf4, 0xd7, 0x64, 0x22, 0x22, byte(len(data))}, data...)
	p = append(p, 0xd0)
	p[2] = byte(len(p) - 3)
	return p
}

func TestRadioRoomProofAndMalformedReports(t *testing.T) {
	p := roomReport()
	r := advertisementRooms(p)
	if len(r) != 1 || r[0].address != "22:22:64:D7:F4:74" || r[0].rssi != -48 || r[0].addressType != 0 {
		t.Fatal(r)
	}
	for _, mutate := range []func([]byte){
		func(p []byte) { p[4] = 2 },
		func(p []byte) { p[13]++ },
		func(p []byte) { p[2]-- },
		func(p []byte) { p[5] = 3 },
		func(p []byte) { p[6] = 4 },
		func(p []byte) { p[20] ^= 1 },
	} {
		copyP := append([]byte{}, p...)
		mutate(copyP)
		if len(advertisementRooms(copyP)) != 0 {
			t.Fatal("invalid or unrelated report accepted", copyP)
		}
	}
	if advertisesRoom(append(append([]byte{}, p[14:len(p)-1]...), 10, 9, 1)) {
		t.Fatal("malformed suffix accepted")
	}
}

func TestObservedRoomOverridesEmptyGattCache(t *testing.T) {
	r := advertisementRooms(roomReport())[0]
	now := time.Now()
	path := dbus.ObjectPath("/org/bluez/hci0/dev_22_22_64_D7_F4_74")
	objs := objects{path: {"org.bluez.Device1": {"Address": dbus.MakeVariant(r.address), "AddressType": dbus.MakeVariant("public"), "UUIDs": dbus.MakeVariant([]string{})}}}
	fresh := map[string]radioRoom{r.address: r}
	if liveRadioRoom(objs, fresh, r.address, now) != path {
		t.Fatal("physical service proof lost to stale GATT cache")
	}
	r.seen = now.Add(-4 * time.Second)
	fresh[r.address] = r
	if liveRadioRoom(objs, fresh, r.address, now) != "" {
		t.Fatal("expired room accepted")
	}
}
