package main

import (
	"bytes"
	"encoding/binary"
	"math/rand"
	"testing"
)

func rfuCodecPacket(outer, kind, info uint32, exchange uint64, payload []byte) []byte {
	offset := 8
	if outer != 1 {
		offset = 16
	}
	packet := make([]byte, offset+12+len(payload))
	binary.BigEndian.PutUint32(packet, outer)
	binary.BigEndian.PutUint32(packet[4:], uint32(len(packet)-8))
	if outer != 1 {
		binary.BigEndian.PutUint64(packet[8:], exchange)
	}
	copy(packet[offset:], "RFU1")
	binary.BigEndian.PutUint32(packet[offset+4:], kind)
	binary.BigEndian.PutUint32(packet[offset+8:], info)
	copy(packet[offset+12:], payload)
	return packet
}

func TestRFUCodecExactRoundTrip(t *testing.T) {
	rng := rand.New(rand.NewSource(5))
	packets := [][]byte{[]byte("NUIBLE5\x00"), make([]byte, 160), make([]byte, 4096), {1}, []byte("unknown bytes")}
	for _, outer := range []uint32{1, 4, 5} {
		for kind := uint32(0); kind <= 7; kind++ {
			for _, exchange := range []uint64{0, 1, 127, 128, 0xffffffffffffffff} {
				for _, info := range []uint32{0, 1, 92, 0xff000000, 0xffffffff} {
					for _, length := range []int{0, 4, 22, 92, 1000, 4068} {
						payload := make([]byte, length)
						rng.Read(payload)
						if length > 2 {
							payload[1] = 0
							payload[length-1] = 0
						}
						packets = append(packets, rfuCodecPacket(outer, kind, info, exchange, payload))
					}
				}
			}
		}
	}
	for i, packet := range packets {
		encoded, err := encodeRFUChunk(packet)
		if err != nil {
			t.Fatalf("%d encode %v", i, err)
		}
		restored, err := decodeRFUChunk(encoded)
		if err != nil || !bytes.Equal(packet, restored) {
			t.Fatalf("%d changed packet err=%v", i, err)
		}
	}
}

func TestRFUCodecMalformed(t *testing.T) {
	for _, packet := range [][]byte{nil, {0}, {0, 0, 0}, {2}, {1}, {1, 1}, {1, 0, 12, 0, 0}, {1, 1, 11, 0, 0}, {1, 1, 12, 8, 0}, {1, 4, 20, 1, 6, 0}, {1, 5, 20, 1, 5, 0}, {1, 1, 12, 0, 0, 1}, {1, 0x81, 0, 12, 0, 0}, {1, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 2}, {1, 1, 12, 0, 0xff, 0xff, 0xff, 0xff, 0x10}} {
		if _, err := decodeRFUChunk(packet); err == nil {
			t.Fatalf("accepted malformed %x", packet)
		}
	}
	for _, packet := range [][]byte{nil, make([]byte, 4097)} {
		if _, err := encodeRFUChunk(packet); err == nil {
			t.Fatal("accepted invalid source length")
		}
	}
}

func TestRFUCodecWireCost(t *testing.T) {
	for _, size := range []int{0, 4, 8, 12, 16, 22, 24, 40, 92} {
		payload := make([]byte, 92)
		for i := 0; i < size; i++ {
			payload[i] = byte(i + 1)
		}
		packet := rfuCodecPacket(4, 5, uint32(size), 123, payload)
		v4, _ := encodeBridgeChunk(packet)
		v5, _ := encodeRFUChunk(packet)
		acl4 := (len(v4) + compactHeader + 7 + 26) / 27
		acl5 := (len(v5) + compactHeader + 7 + 26) / 27
		t.Logf("data=%d v4_value=%d v5_value=%d ACL=%d->%d", size, len(v4)+compactHeader, len(v5)+compactHeader, acl4, acl5)
		if len(v5) >= len(v4) {
			t.Fatal("RFU encoding did not shrink")
		}
		if size == 22 && acl5 != 2 {
			t.Fatal("typical host message does not fit two ACL segments")
		}
	}
}

func FuzzRFUCodecDecode(f *testing.F) {
	f.Add([]byte{1, 1, 12, 0, 0})
	f.Add([]byte{0, 0, 1, 42})
	f.Add([]byte{1, 4, 112, 1, 5, 22, 3})
	f.Fuzz(func(t *testing.T, data []byte) {
		restored, err := decodeRFUChunk(data)
		if err != nil {
			return
		}
		if len(restored) == 0 || len(restored) > bridgeChunkLimit {
			t.Fatal("unbounded decode")
		}
		encoded, err := encodeRFUChunk(restored)
		if err != nil {
			t.Fatal(err)
		}
		again, err := decodeRFUChunk(encoded)
		if err != nil || !bytes.Equal(restored, again) {
			t.Fatal("decoded packet cannot round trip")
		}
	})
}
