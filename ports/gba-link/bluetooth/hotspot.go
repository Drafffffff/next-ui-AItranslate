package main

import (
	"fmt"
	"math"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

// Both consoles supply the same password automatically; no keyboard step.
const hotspotPassword = "NextUI-GBA-Link"
const hotspotPrefix = "NextUI-GBA-"
const hotspotHostIP = "192.168.241.1"
const hotspotClientIP = "192.168.241.2"

// Physical testing is required: xradio advertises combinations that its
// firmware cannot use reliably. Do not infer usability from iw list alone.
type hotspotRoom struct {
	Address   string `json:"address"`
	SSID      string `json:"ssid"`
	Frequency int    `json:"frequency"`
	RSSI      int8   `json:"rssi"`
}

func hotspotSSID(address string) (string, error) {
	if !validRoomAddress(address) {
		return "", fmt.Errorf("invalid hotspot BSSID")
	}
	clean := strings.ReplaceAll(strings.ToUpper(address), ":", "")
	return hotspotPrefix + clean[len(clean)-4:], nil
}

var hotspotBSS = regexp.MustCompile(`^BSS ([0-9a-fA-F:]{17})(?:\(|\s|$)`)

// Parse the response to a fresh kernel scan, not a stale scan-results cache.
// Require our exact room name and WPA2/CCMP/PSK; never list unrelated networks.
func parseHotspotScan(raw string) []hotspotRoom {
	var rooms []hotspotRoom
	var room hotspotRoom
	var rsn, psk, ccmp bool
	finish := func() {
		ssid, err := hotspotSSID(room.Address)
		if err == nil && room.SSID == ssid && room.Frequency >= 2412 && room.Frequency <= 2462 && (room.Frequency-2412)%5 == 0 && rsn && psk && ccmp {
			for _, old := range rooms {
				if old.Address == room.Address {
					return
				}
			}
			rooms = append(rooms, room)
		}
	}
	for _, line := range strings.Split(raw, "\n") {
		line = strings.TrimSpace(line)
		if match := hotspotBSS.FindStringSubmatch(line); match != nil {
			finish()
			room = hotspotRoom{Address: strings.ToUpper(match[1]), RSSI: -127}
			rsn = false
			psk = false
			ccmp = false
		} else if strings.HasPrefix(line, "SSID: ") {
			room.SSID = strings.TrimPrefix(line, "SSID: ")
		} else if strings.HasPrefix(line, "freq: ") {
			room.Frequency, _ = strconv.Atoi(strings.TrimPrefix(line, "freq: "))
		} else if strings.HasPrefix(line, "signal: ") {
			parts := strings.Fields(line)
			if len(parts) > 1 {
				signal, err := strconv.ParseFloat(parts[1], 64)
				if err == nil && !math.IsNaN(signal) && !math.IsInf(signal, 0) {
					room.RSSI = int8(math.Max(-127, math.Min(0, signal)))
				}
			}
		} else if strings.HasPrefix(line, "RSN:") {
			rsn = true
		} else if rsn && strings.Contains(line, "Authentication suites:") {
			psk = strings.Contains(line, "PSK")
		} else if rsn && strings.Contains(line, "Pairwise ciphers:") {
			ccmp = strings.Contains(line, "CCMP")
		}
	}
	finish()
	sort.Slice(rooms, func(i, j int) bool {
		if rooms[i].RSSI != rooms[j].RSSI {
			return rooms[i].RSSI > rooms[j].RSSI
		}
		return rooms[i].Address < rooms[j].Address
	})
	if len(rooms) > 8 {
		rooms = rooms[:8]
	}
	return rooms
}
