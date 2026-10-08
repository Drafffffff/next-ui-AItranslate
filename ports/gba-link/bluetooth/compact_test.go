package main

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestCompactRoundTripAndFallback(t *testing.T) {
	for _, payload := range []int{20, 23, 64, 512} {
		for _, size := range []int{0, 1, 7, 34, 104, 499, 500, 4096, 65536} {
			data := bytes.Repeat([]byte{0x5a}, size)
			frames, err := compactFragments(payload, 44, 2, data)
			if err != nil {
				t.Fatal(err)
			}
			a := newAssembly(44)
			a.next = 2
			var got []byte
			for _, packet := range frames {
				if len(packet) > payload {
					t.Fatal("negotiated payload exceeded")
				}
				if packet[0] == compactTag {
					packet, err = expandCompact(packet)
					if err != nil {
						t.Fatal(err)
					}
				}
				message, _, err := a.accept(packet)
				if err != nil {
					t.Fatal(err)
				}
				if message != nil {
					got = message
				}
			}
			if !bytes.Equal(got, data) || a.next != 3 {
				t.Fatalf("payload=%d size=%d roundtrip", payload, size)
			}
		}
	}
	frames, _ := compactFragments(512, 44, 1, []byte("NUIBLE4\x00"))
	if frames[0][0] != 1 {
		t.Fatal("bootstrap must retain legacy framing")
	}
}

func TestCompactRejectsCorruptionSessionAndSequence(t *testing.T) {
	frames, _ := compactFragments(512, 44, 2, []byte("identity and RFU data"))
	for _, which := range []string{"crc", "payload", "epoch", "sequence", "truncate"} {
		packet := append([]byte{}, frames[0]...)
		switch which {
		case "crc":
			packet[9] ^= 1
		case "payload":
			packet[len(packet)-1] ^= 1
		case "epoch":
			binary.BigEndian.PutUint32(packet[1:], 45)
		case "sequence":
			binary.BigEndian.PutUint32(packet[5:], 3)
		case "truncate":
			packet = packet[:len(packet)-1]
		}
		expanded, err := expandCompact(packet)
		if err != nil {
			t.Fatal(err)
		}
		a := newAssembly(44)
		a.next = 2
		if _, _, err = a.accept(expanded); err == nil {
			t.Fatal("accepted", which)
		}
	}
	for n := 0; n < compactHeader; n++ {
		if _, err := expandCompact(frames[0][:n]); err == nil {
			t.Fatal("short envelope accepted")
		}
	}
	packet := append([]byte{}, frames[0]...)
	binary.BigEndian.PutUint32(packet[5:], 1)
	if _, err := expandCompact(packet); err == nil {
		t.Fatal("compact bootstrap accepted")
	}
}

// Byte/segmentation accounting, not a simulated or measured radio latency.
func TestCompactPadded104WireCost(t *testing.T) {
	packet := make([]byte, 112)
	binary.BigEndian.PutUint32(packet, 1)
	binary.BigEndian.PutUint32(packet[4:], 104)
	for i := 0; i < 24; i++ {
		packet[8+i] = byte(i + 1)
	}
	encoded, err := encodeBridgeChunk(packet)
	if err != nil {
		t.Fatal(err)
	}
	old, _ := fragmentMessage(512, 44, 2, encoded)
	candidate, _ := compactFragments(512, 44, 2, encoded)
	cost := func(frames [][]byte) (valueBytes, aclPackets int) {
		for _, frame := range frames {
			valueBytes += len(frame)
			aclPackets += (len(frame) + 7 + 26) / 27
		}
		return
	}
	oldBytes, oldACL := cost(old)
	newBytes, newACL := cost(candidate)
	if oldBytes != 58 || newBytes != 47 || oldACL != 3 || newACL != 2 {
		t.Fatalf("unexpected cost %d/%d -> %d/%d", oldBytes, oldACL, newBytes, newACL)
	}
	t.Logf("104-byte padded payload: encoded=%d ATT value bytes %d -> %d, 27-byte ACL accounting %d -> %d; events/latency unmeasured", len(encoded), oldBytes, newBytes, oldACL, newACL)
}

// Actual protocol-3 RFU host shape: 8-byte wire header, 8-byte exchange ID,
// RFU1/type/info prefix (12 bytes), payload and zero padding up to 104 bytes.
func TestCompactTaggedRFUWireCosts(t *testing.T) {
	for _, size := range []int{0, 4, 8, 12, 16, 24, 40, 92} {
		packet := make([]byte, 120)
		binary.BigEndian.PutUint32(packet, 4)
		binary.BigEndian.PutUint32(packet[4:], 112)
		binary.BigEndian.PutUint64(packet[8:], 123)
		copy(packet[16:], []byte("RFU1"))
		binary.BigEndian.PutUint32(packet[20:], 5)
		binary.BigEndian.PutUint32(packet[24:], uint32(size))
		for i := 0; i < size; i++ {
			packet[28+i] = byte(i + 1)
		}
		encoded, err := encodeBridgeChunk(packet)
		if err != nil {
			t.Fatal(err)
		}
		old, _ := fragmentMessage(512, 44, 2, encoded)
		candidate, _ := compactFragments(512, 44, 2, encoded)
		count := func(frames [][]byte) int {
			n := 0
			for _, p := range frames {
				n += (len(p) + 7 + 26) / 27
			}
			return n
		}
		if count(candidate) > count(old) {
			t.Fatal("RFU segmentation regressed")
		}
		expanded, err := expandCompact(candidate[0])
		if err != nil {
			t.Fatal(err)
		}
		a := newAssembly(44)
		a.next = 2
		message, _, err := a.accept(expanded)
		if err != nil {
			t.Fatal(err)
		}
		restored, err := decodeBridgeChunk(message)
		if err != nil || !bytes.Equal(restored, packet) {
			t.Fatal("RFU bytes changed")
		}
		t.Logf("RFU data=%d encoded=%d old_value=%d new_value=%d acl=%d->%d", size, len(encoded), len(old[0]), len(candidate[0]), count(old), count(candidate))
	}
}

func TestCompactPeripheralRequiresBootstrapAndVariant(t *testing.T) {
	e := newPeripheralPackets(&Peripheral{mtu: 517})
	short, _ := compactFragments(512, 44, 2, nil)
	if err := e.accept(short[0]); err == nil {
		t.Fatal("compact before bootstrap")
	}
	first, _ := fragmentMessage(512, 44, 1, []byte("NUIBLE4\x00"))
	if err := e.accept(first[0]); err != nil {
		t.Fatal(err)
	}
	err := e.accept(short[0])
	if (err == nil) != (bleTransportVersion == "4" || bleTransportVersion == "5") {
		t.Fatalf("compact variant=%s err=%v", bleTransportVersion, err)
	}
}
