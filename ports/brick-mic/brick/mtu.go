package main

import "github.com/godbus/dbus/v5"

// BlueZ server WriteValue options carry the exchanged ATT MTU. A Linux
// write-only control characteristic may report a stale 20-byte capacity on
// the client. Prefer the server's observed value only when explicitly asked.
func notificationPacket(requested int, negotiated bool, options map[string]dbus.Variant) int {
	packet := clamp(requested, 20, 244)
	if v, ok := options["mtu"]; ok {
		if mtu, valid := v.Value().(uint16); valid && mtu >= 23 && mtu <= 517 {
			capacity := clamp(int(mtu)-3, 20, 244)
			if negotiated || packet > capacity {
				packet = capacity
			}
		}
	}
	return packet
}
