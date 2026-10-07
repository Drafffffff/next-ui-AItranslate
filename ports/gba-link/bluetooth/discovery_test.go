package main

import (
	"fmt"
	"testing"
	"time"

	"github.com/godbus/dbus/v5"
)

func TestDiscoveryRequiresFreshAdapterUpdate(t *testing.T) {
	path := dbus.ObjectPath("/org/bluez/hci0/dev_22_22_1C_11_93_21")
	props := map[string]dbus.Variant{"UUIDs": dbus.MakeVariant([]string{serviceUUID}), "Address": dbus.MakeVariant("22:22:1C:11:93:21")}
	objs := objects{path: {"org.bluez.Device1": props}}
	now := time.Now()
	if liveRoom(objs, nil, "", now) != "" {
		t.Fatal("cached room accepted")
	}
	if liveRoom(objs, map[dbus.ObjectPath]time.Time{path: now.Add(-4 * time.Second)}, "", now) != "" {
		t.Fatal("expired room accepted")
	}
	if liveRoom(objs, map[dbus.ObjectPath]time.Time{path: now}, "AA:BB:CC:DD:EE:FF", now) != "" {
		t.Fatal("wrong room accepted")
	}
	if liveRoom(objs, map[dbus.ObjectPath]time.Time{path: now}, "22:22:1C:11:93:21", now) != path {
		t.Fatal("fresh expected room rejected")
	}
	base := &dbus.Signal{Path: path, Name: "org.freedesktop.DBus.Properties.PropertiesChanged", Body: []any{"org.bluez.Device1", map[string]dbus.Variant{"Connected": dbus.MakeVariant(false)}, []string{}}}
	if freshDiscoveryPath(base) != "" {
		t.Fatal("connection bookkeeping treated as discovery")
	}
	base.Body[1] = map[string]dbus.Variant{"RSSI": dbus.MakeVariant(int16(-45))}
	if freshDiscoveryPath(base) != path {
		t.Fatal("fresh discovery signal rejected")
	}
	base.Path = "/org/bluez/hci1/dev_22_22_1C_11_93_21"
	if freshDiscoveryPath(base) != "" {
		t.Fatal("other adapter accepted")
	}
	base.Body = []any{"org.bluez.Device1"}
	if freshDiscoveryPath(base) != "" {
		t.Fatal("malformed signal accepted")
	}
}

func TestRoomListRejectsStaleAndMalformedAddresses(t *testing.T) {
	now := time.Now()
	objs := objects{}
	fresh := map[dbus.ObjectPath]time.Time{}
	addresses := []string{"22:22:64:D7:F4:74", "22:22:1C:11:93:21", "GG:22:64:D7:F4:74", "22-22-64-D7-F4-74"}
	for i, address := range addresses {
		path := dbus.ObjectPath(fmt.Sprintf("/org/bluez/hci0/dev_%d", i))
		objs[path] = map[string]map[string]dbus.Variant{"org.bluez.Device1": {"Address": dbus.MakeVariant(address), "UUIDs": dbus.MakeVariant([]string{serviceUUID})}}
		fresh[path] = now
	}
	rooms := roomCandidates(objs, fresh, nil, false, now)
	if len(rooms) != 2 || rooms[0].address != addresses[1] || rooms[1].address != addresses[0] || rooms[0].label != "GBA Link 9321" {
		t.Fatal(rooms)
	}
	if len(roomCandidates(objs, fresh, nil, false, now.Add(4*time.Second))) != 0 {
		t.Fatal("stale room remained visible")
	}
}
