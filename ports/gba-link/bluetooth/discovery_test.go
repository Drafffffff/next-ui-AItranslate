package main

import (
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
