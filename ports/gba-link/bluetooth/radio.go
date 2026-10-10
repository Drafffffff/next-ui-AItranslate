package main

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"strings"
	"time"
)

type radioRoom struct {
	address     string
	addressType byte
	rssi        int8
	seen        time.Time
}

func advertisesRoom(data []byte) bool {
	uuid, err := hex.DecodeString(strings.ReplaceAll(serviceUUID, "-", ""))
	if err != nil {
		return false
	}
	for i, j := 0, len(uuid)-1; i < j; i, j = i+1, j-1 {
		uuid[i], uuid[j] = uuid[j], uuid[i]
	}
	found := false
	for pos := 0; pos < len(data); {
		length := int(data[pos])
		if length == 0 {
			break
		}
		if length < 1 || pos+1+length > len(data) {
			return false
		}
		typ := data[pos+1]
		value := data[pos+2 : pos+1+length]
		if typ == 6 || typ == 7 {
			if len(value)%16 != 0 {
				return false
			}
			for i := 0; i < len(value); i += 16 {
				if bytes.Equal(value[i:i+16], uuid) {
					found = true
				}
			}
		}
		pos += length + 1
	}
	return found
}

func advertisementRooms(packet []byte) []radioRoom {
	if len(packet) < 5 || packet[0] != 4 || packet[1] != 0x3e || int(packet[2])+3 != len(packet) || packet[3] != 2 {
		return nil
	}
	rooms := []radioRoom{}
	pos := 5
	for report := 0; report < int(packet[4]); report++ {
		if pos+10 > len(packet) {
			return nil
		}
		length := int(packet[pos+8])
		end := pos + 10 + length
		if end > len(packet) {
			return nil
		}
		if packet[pos] == 0 && packet[pos+1] <= 1 && advertisesRoom(packet[pos+9:pos+9+length]) {
			address := fmt.Sprintf("%02X:%02X:%02X:%02X:%02X:%02X", packet[pos+7], packet[pos+6], packet[pos+5], packet[pos+4], packet[pos+3], packet[pos+2])
			rooms = append(rooms, radioRoom{address: address, addressType: packet[pos+1], rssi: int8(packet[end-1]), seen: time.Now()})
		}
		pos = end
	}
	if pos != len(packet) {
		return nil
	}
	return rooms
}
