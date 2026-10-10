package main

import (
	"encoding/binary"
	"testing"
)

func TestTimingTargetsOnlyOwnedCentralConnection(t *testing.T) {
	con := "Connections:\n > LE AA:BB:CC:DD:EE:FF handle 7 state 1 lm SLAVE\n < LE 22:22:64:D7:F4:74 handle 3 state 1 lm MASTER\n"
	h, err := ownedCentralHandle(con, "22:22:64:D7:F4:74")
	if err != nil || h != 3 {
		t.Fatal("owned central not found")
	}
	for _, peer := range []string{"AA:BB:CC:DD:EE:FF", "00:00:00:00:00:00"} {
		if _, err := ownedCentralHandle(con, peer); err == nil {
			t.Fatal("unowned or peripheral connection accepted")
		}
	}
	p, err := connectionUpdatePacket(h, 6)
	if err != nil || len(p) != 14 || binary.LittleEndian.Uint16(p) != 3 || binary.LittleEndian.Uint16(p[2:]) != 6 || binary.LittleEndian.Uint16(p[4:]) != 6 || binary.LittleEndian.Uint16(p[6:]) != 0 || binary.LittleEndian.Uint16(p[8:]) != 100 {
		t.Fatal("invalid per-link timing command")
	}
	for _, v := range []uint16{0, 5, 400, 3201} {
		if _, err := connectionUpdatePacket(0, v); err == nil {
			t.Fatal("invalid timing budget accepted")
		}
	}
}
func TestAsyncConnectionUpdateStatus(t *testing.T) {
	p := []byte{4, 0x0f, 4, 0, 1, 0x13, 0x20}
	if status, ok := hciCommandReply(p, 0x2013); !ok || status != 0 {
		t.Fatal("accepted asynchronous request not recognized")
	}
	if _, ok := hciCommandReply(p, 0x2008); ok {
		t.Fatal("unrelated command status accepted")
	}
}

func TestIntervalCompletionIdentityAndTiming(t *testing.T) {
	p := []byte{4, 0x3e, 10, 3, 0, 7, 0, 6, 0, 0, 0, 100, 0}
	if ready, err := intervalCompletion(p, 7, 6); !ready || err != nil {
		t.Fatal("correct completion rejected", err)
	}
	if ready, err := intervalCompletion(p, 8, 6); ready || err != nil {
		t.Fatal("other connection accepted")
	}
	if ready, err := intervalCompletion(p, 7, 56); ready || err != nil {
		t.Fatal("initial interval accepted")
	}
	p[4] = 0x0c
	if ready, err := intervalCompletion(p, 7, 6); ready || err == nil {
		t.Fatal("failure accepted")
	}
	if _, err := intervalCompletion(p[:8], 7, 6); err == nil {
		t.Fatal("truncated completion accepted")
	}
}
