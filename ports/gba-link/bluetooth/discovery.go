package main

import (
	"context"
	"fmt"
	"sort"
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
	return discoverRooms(ctx, conn, expected, nil)
}

type roomCandidate struct {
	path           dbus.ObjectPath
	address, label string
	rssi           int8
}

func validRoomAddress(address string) bool {
	if len(address) != 17 {
		return false
	}
	for i, c := range address {
		if i%3 == 2 {
			if c != ':' {
				return false
			}
		} else if !(c >= '0' && c <= '9' || c >= 'a' && c <= 'f' || c >= 'A' && c <= 'F') {
			return false
		}
	}
	return true
}

func roomCandidates(objs objects, fresh map[dbus.ObjectPath]time.Time, radioFresh map[string]radioRoom, physical bool, now time.Time) []roomCandidate {
	var result []roomCandidate
	for path, interfaces := range objs {
		if !strings.HasPrefix(string(path), string(adapter)+"/dev_") {
			continue
		}
		d := interfaces["org.bluez.Device1"]
		address, _ := d["Address"].Value().(string)
		if len(address) != 17 || !validRoomAddress(address) {
			continue
		}
		if physical {
			if liveRadioRoom(objects{path: interfaces}, radioFresh, address, now) == "" {
				continue
			}
		} else if liveRoom(objects{path: interfaces}, fresh, address, now) == "" {
			continue
		}
		clean := strings.ReplaceAll(strings.ToUpper(address), ":", "")
		rssi := radioFresh[strings.ToUpper(address)].rssi
		result = append(result, roomCandidate{path: path, address: strings.ToUpper(address), label: "GBA Link " + clean[len(clean)-4:], rssi: rssi})
	}
	sort.Slice(result, func(i, j int) bool { return result[i].address < result[j].address })
	if len(result) > 8 {
		result = result[:8]
	}
	return result
}

func discoverRooms(ctx context.Context, conn *dbus.Conn, expected string, onRooms func([]roomCandidate) error) (dbus.ObjectPath, error) {
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
			if onRooms != nil {
				rooms := roomCandidates(objs, fresh, radioFresh, radioUpdates != nil, time.Now())
				if err := onRooms(rooms); err != nil {
					return "", err
				}
				continue
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

		}
	}
}

// Selection already came from the scanner's physical advertisement proof.
// Avoid starting discovery again: on this controller a second scan with
// duplicate filtering enabled failed to report the selected room again.
// A cached address is only a connection target; actual ATT service discovery,
// bilateral readiness and protocol 3 identity still gate connected state.
func selectedRoomPath(objs objects, address string) dbus.ObjectPath {
	if !validRoomAddress(address) {
		return ""
	}
	for path, interfaces := range objs {
		if !strings.HasPrefix(string(path), string(adapter)+"/dev_") {
			continue
		}
		props := interfaces["org.bluez.Device1"]
		candidate, _ := props["Address"].Value().(string)
		kind, _ := props["AddressType"].Value().(string)
		if strings.EqualFold(candidate, address) && (kind == "public" || kind == "random") {
			return path
		}
	}
	return ""
}
