package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"io"
	"net"
	"testing"
	"time"
)

func TestBridgePreservesNextUIStreamAndMaxPacket(t *testing.T) {
	for _, payload := range []int{20, 512} {
		x, y := memoryPacketPair()
		hostApp, hostWorker := net.Pipe()
		joinApp, joinWorker := net.Pipe()
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		ready := make(chan bool, 2)
		results := make(chan error, 2)
		go func() {
			results <- bridgeSession(ctx, x, payload, 99, true, hostWorker, func() error { ready <- true; return nil })
		}()
		go func() {
			results <- bridgeSession(ctx, y, payload, 99, false, joinWorker, func() error { ready <- false; return nil })
		}()
		for i := 0; i < 2; i++ {
			select {
			case <-ready:
			case <-ctx.Done():
				t.Fatal("bilateral readiness stalled")
			}
		}
		var expected [][]byte
		for _, tag := range []byte{0x37, 0xa8} {
			hello := bytes.Repeat([]byte{tag}, 160) // opaque identity, including intentional mismatch bytes
			var wire bytes.Buffer
			wire.Write(hello)
			for _, length := range []int{0, 104, 65536} {
				var h [8]byte
				binary.BigEndian.PutUint32(h[:], 1)
				binary.BigEndian.PutUint32(h[4:], uint32(length))
				wire.Write(h[:])
				wire.Write(bytes.Repeat([]byte{tag}, length))
			}
			expected = append(expected, wire.Bytes())
		}
		writes := make(chan error, 2)
		go func() { _, err := hostApp.Write(expected[0]); writes <- err }()
		go func() { _, err := joinApp.Write(expected[1]); writes <- err }()
		reads := make(chan []byte, 2)
		go func() {
			b := make([]byte, len(expected[1]))
			_, err := io.ReadFull(hostApp, b)
			if err != nil {
				b = nil
			}
			reads <- b
		}()
		go func() {
			b := make([]byte, len(expected[0]))
			_, err := io.ReadFull(joinApp, b)
			if err != nil {
				b = nil
			}
			reads <- b
		}()
		seen := map[byte]bool{}
		for i := 0; i < 2; i++ {
			select {
			case b := <-reads:
				if len(b) == 0 {
					t.Fatal("bridge ended before complete byte stream")
				}
				tag := b[0]
				idx := 0
				if tag == 0xa8 {
					idx = 1
				}
				if !bytes.Equal(b, expected[idx]) || seen[tag] {
					t.Fatal("stream corrupted, reordered or duplicated")
				}
				seen[tag] = true
			case <-ctx.Done():
				t.Fatal("stream transfer stalled")
			}
		}
		for i := 0; i < 2; i++ {
			if err := <-writes; err != nil {
				t.Fatal(err)
			}
		}
		cancel()
		hostApp.Close()
		joinApp.Close()
		for i := 0; i < 2; i++ {
			select {
			case <-results:
			case <-time.After(time.Second):
				t.Fatal("bridge leaked pump")
			}
		}
	}
}

func TestBridgeCancelAndMalformedLocalFrame(t *testing.T) {
	for _, badHeader := range []bool{false, true} {
		x, y := memoryPacketPair()
		a, aw := net.Pipe()
		b, bw := net.Pipe()
		ctx, cancel := context.WithCancel(context.Background())
		ready := make(chan struct{}, 2)
		results := make(chan error, 2)
		go func() {
			results <- bridgeSession(ctx, x, 512, 72, true, aw, func() error { ready <- struct{}{}; return nil })
		}()
		go func() {
			results <- bridgeSession(ctx, y, 512, 72, false, bw, func() error { ready <- struct{}{}; return nil })
		}()
		for i := 0; i < 2; i++ {
			select {
			case <-ready:
			case <-time.After(time.Second):
				t.Fatal("setup")
			}
		}
		if badHeader {
			go func() { packet := make([]byte, 168); binary.BigEndian.PutUint32(packet[160:], 99); a.Write(packet) }()
			// Drain the forwarded opaque HELLO so the bad local frame can end
			// its own worker even while the other application is otherwise idle.
			buffer := make([]byte, 160)
			b.SetReadDeadline(time.Now().Add(time.Second))
			io.ReadFull(b, buffer)
			select {
			case err := <-results:
				if err == nil {
					t.Fatal("bad local header accepted")
				}
			case <-time.After(time.Second):
				t.Fatal("bad header did not terminate")
			}
		}
		cancel()
		a.Close()
		b.Close()
		n := 2
		if badHeader {
			n = 1
		}
		for i := 0; i < n; i++ {
			select {
			case <-results:
			case <-time.After(time.Second):
				t.Fatal("cancel blocked on local stream")
			}
		}
	}
}
