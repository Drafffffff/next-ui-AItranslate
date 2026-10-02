package main

import (
	"github.com/godbus/dbus/v5"
	"testing"
)

func TestNotificationPacketUsesActualServerMTU(t *testing.T) {
	for _, c := range []struct {
		request, mtu, want int
		negotiated         bool
	}{{20, 247, 244, true}, {20, 185, 182, true}, {244, 23, 20, true}, {182, 247, 182, false}, {244, 185, 182, false}, {20, 0, 20, true}, {244, 999, 244, true}} {
		options := map[string]dbus.Variant{}
		if c.mtu > 0 {
			options["mtu"] = dbus.MakeVariant(uint16(c.mtu))
		}
		if got := notificationPacket(c.request, c.negotiated, options); got != c.want {
			t.Fatalf("%+v: %d", c, got)
		}
	}
}
