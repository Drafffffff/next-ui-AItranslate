package main

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestNegotiatedMTUFragmentation(t *testing.T) {
	for _, payload := range []int{20, 104, 182, 244, 512} {
		for _, length := range []int{0, 1, 104, 256, 65536} {
			data := make([]byte, length)
			for i := range data {
				data[i] = byte(i * 31)
			}
			frames, err := fragmentMessage(payload, 42, 1, data)
			if err != nil {
				t.Fatal(err)
			}
			receiver := newAssembly(42)
			var delivered, ack []byte
			for _, frame := range frames {
				if len(frame) > payload {
					t.Fatal("frame exceeds negotiated payload")
				}
				delivered, ack, err = receiver.accept(frame)
				if err != nil {
					t.Fatal(err)
				}
			}
			if !bytes.Equal(delivered, data) || !validAck(ack, 42, 1) {
				t.Fatalf("failed bounded delivery payload=%d length=%d", payload, length)
			}
			again, ack, err := receiver.accept(frames[len(frames)-1])
			if err != nil || again != nil || !validAck(ack, 42, 1) {
				t.Fatal("retransmitted completed message delivered twice")
			}
		}
	}
}
func TestCorruptionAndStaleSessions(t *testing.T) {
	frames, _ := fragmentMessage(20, 42, 1, []byte("bounded message"))
	mutated := append([]byte{}, frames[0]...)
	mutated[4] ^= 1
	if _, _, err := newAssembly(42).accept(mutated); err == nil {
		t.Fatal("stale epoch accepted")
	}
	if _, _, err := newAssembly(42).accept(frames[1]); err == nil {
		t.Fatal("missing initial fragment accepted")
	}
	receiver := newAssembly(42)
	for i, frame := range frames {
		copyFrame := append([]byte{}, frame...)
		if i == len(frames)-1 {
			copyFrame[len(copyFrame)-1] ^= 1
		}
		_, _, err := receiver.accept(copyFrame)
		if i == len(frames)-1 && err == nil {
			t.Fatal("corrupted complete message delivered")
		}
	}
	receiver.reset()
	if _, _, err := receiver.accept(frames[0]); err == nil {
		t.Fatal("cancelled assembly accepted data")
	}
}
func TestBoundedLengthAndDuplicateFragments(t *testing.T) {
	frames, _ := fragmentMessage(32, 42, 1, make([]byte, 104))
	receiver := newAssembly(42)
	if _, _, err := receiver.accept(frames[0]); err != nil {
		t.Fatal(err)
	}
	before := len(receiver.buffer)
	if m, _, err := receiver.accept(frames[0]); err != nil || m != nil || len(receiver.buffer) != before {
		t.Fatal("duplicate partial fragment changed assembly")
	}
	malicious := append([]byte{}, frames[0]...)
	binary.BigEndian.PutUint32(malicious[frameHeader:], maxMessage+1)
	if _, _, err := newAssembly(42).accept(malicious); err == nil {
		t.Fatal("oversized length accepted")
	}
	for _, payload := range []int{0, 16, 513} {
		if _, err := fragmentMessage(payload, 42, 1, nil); err == nil {
			t.Fatal("invalid MTU accepted")
		}
	}
	ack := frameHeaderBytes(frameAck, 42, 1, 0)
	if validAck(ack, 43, 1) || validAck(ack, 42, 2) {
		t.Fatal("ack from another session/message accepted")
	}
}
