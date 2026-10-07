package main

import (
	"context"
	"fmt"
	"strings"
	"time"

	"github.com/godbus/dbus/v5"
)

// Cached objects alone cannot establish that a room is still advertising.
// Only fresh discovery updates from our adapter enter the candidate set.
func freshDiscoveryPath(signal *dbus.Signal) dbus.ObjectPath {
	if signal == nil {
		return ""
	}
	var path dbus.ObjectPath
	var values map[string]dbus.Variant
	switch signal.Name {
	case "org.freedesktop.DBus.Properties.PropertiesChanged":
		var iface string
		var invalid []string
		if dbus.Store(signal.Body, &iface, &values, &invalid) != nil || iface != "org.bluez.Device1" {
			return ""
		}
		path = signal.Path
	case "org.freedesktop.DBus.ObjectManager.InterfacesAdded":
		var interfaces properties
		if dbus.Store(signal.Body, &path, &interfaces) != nil {
			return ""
		}
		values = interfaces["org.bluez.Device1"]
	default:
		return ""
	}
	if !strings.HasPrefix(string(path), string(adapter)+"/dev_") {
		return ""
	}
	for _, name := range []string{"RSSI", "ManufacturerData", "ServiceData", "AdvertisingData", "UUIDs"} {
		if _, ok := values[name]; ok {
			return path
		}
	}
	return ""
}

func liveRoom(objs objects, fresh map[dbus.ObjectPath]time.Time, expected string, now time.Time) dbus.ObjectPath {
	for path, seen := range fresh {
		if now.Sub(seen) > 3*time.Second {
			continue
		}
		d := objs[path]["org.bluez.Device1"]
		address, _ := d["Address"].Value().(string)
		if expected != "" && !strings.EqualFold(address, expected) {
			continue
		}
		uuids, _ := d["UUIDs"].Value().([]string)
		for _, uuid := range uuids {
			if strings.EqualFold(uuid, serviceUUID) {
				return path
			}
		}
	}
	return ""
}

// The observed advertisement proves the current service. Device1.UUIDs can
// instead reflect a cached, previously empty GATT server after role changes.
func liveRadioRoom(objs objects, fresh map[string]radioRoom, expected string, now time.Time) dbus.ObjectPath {
	for path, interfaces := range objs {
		if !strings.HasPrefix(string(path), string(adapter)+"/dev_") {
			continue
		}
		d := interfaces["org.bluez.Device1"]
		address, _ := d["Address"].Value().(string)
		if expected != "" && !strings.EqualFold(address, expected) {
			continue
		}
		room, ok := fresh[strings.ToUpper(address)]
		if !ok || now.Sub(room.seen) > 3*time.Second {
			continue
		}
		typ, _ := d["AddressType"].Value().(string)
		if (room.addressType == 0 && typ != "public") || (room.addressType == 1 && typ != "random") {
			continue
		}
		return path
	}
	return ""
}

func findRoom(ctx context.Context, conn *dbus.Conn, expected string) (dbus.ObjectPath, error) {
	radioUpdates, closeRadio, err := advertisementObserver(ctx)
	if err != nil {
		return "", err
	}
	defer closeRadio()
	propertyMatch := []dbus.MatchOption{dbus.WithMatchSender("org.bluez"), dbus.WithMatchInterface("org.freedesktop.DBus.Properties"), dbus.WithMatchMember("PropertiesChanged"), dbus.WithMatchArg(0, "org.bluez.Device1"), dbus.WithMatchPathNamespace(adapter)}
	objectMatch := []dbus.MatchOption{dbus.WithMatchSender("org.bluez"), dbus.WithMatchInterface("org.freedesktop.DBus.ObjectManager"), dbus.WithMatchMember("InterfacesAdded")}
	if err := conn.AddMatchSignal(propertyMatch...); err != nil {
		return "", err
	}
	defer conn.RemoveMatchSignal(propertyMatch...)
	if err := conn.AddMatchSignal(objectMatch...); err != nil {
		return "", err
	}
	defer conn.RemoveMatchSignal(objectMatch...)
	updates := make(chan *dbus.Signal, 128)
	conn.Signal(updates)
	defer conn.RemoveSignal(updates)
	filter := map[string]dbus.Variant{"Transport": dbus.MakeVariant("le"), "UUIDs": dbus.MakeVariant([]string{serviceUUID}), "DuplicateData": dbus.MakeVariant(true)}
	if err := call(ctx, conn, adapter, "org.bluez.Adapter1.SetDiscoveryFilter", filter).Err; err != nil {
		return "", err
	}
	if err := call(ctx, conn, adapter, "org.bluez.Adapter1.StartDiscovery").Err; err != nil {
		return "", err
	}
	defer call(context.Background(), conn, adapter, "org.bluez.Adapter1.StopDiscovery")
	fresh := make(map[dbus.ObjectPath]time.Time)
	radioFresh := make(map[string]radioRoom)
	ticker := time.NewTicker(200 * time.Millisecond)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return "", fmt.Errorf("room discovery: %w", ctx.Err())
		case signal := <-updates:
			if path := freshDiscoveryPath(signal); path != "" {
				fresh[path] = time.Now()
			}
		case room, ok := <-radioUpdates:
			if !ok {
				return "", fmt.Errorf("radio observation ended: %v", ctx.Err())
			}
			if expected != "" && !strings.EqualFold(expected, room.address) {
				continue
			}
			if _, known := radioFresh[room.address]; known || len(radioFresh) < 256 {
				radioFresh[room.address] = room
			}
		case <-ticker.C:
			objs, err := managed(ctx, conn)
			if err != nil {
				return "", err
			}
			path := dbus.ObjectPath("")
			if radioUpdates != nil {
				path = liveRadioRoom(objs, radioFresh, expected, time.Now())
			} else {
				path = liveRoom(objs, fresh, expected, time.Now())
			}
			if path != "" {
				address, _ := objs[path]["org.bluez.Device1"]["Address"].Value().(string)
				fmt.Printf("FOUND_FRESH address=%s path=%s\n", address, path)
				return path, nil
			}
			for path, seen := range fresh {
				if time.Since(seen) > 3*time.Second {
					delete(fresh, path)
				}
			}
			for address, room := range radioFresh {
				if time.Since(room.seen) > 3*time.Second {
					delete(radioFresh, address)
				}
			}
		}
	}
}
