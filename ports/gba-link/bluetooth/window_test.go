package main

import (
	"context"
	"encoding/binary"
	"fmt"
	"testing"
	"time"
)

type echoEndpoint struct {
	packets  chan []byte
	assembly *assembly
	badAck   bool
	writes   int
}

func (e *echoEndpoint) SetReadDeadline(time.Time) error  { return nil }
func (e *echoEndpoint) SetWriteDeadline(time.Time) error { return nil }
func (e *echoEndpoint) Write(p []byte) (int, error) {
	e.writes++
	if e.assembly == nil {
		e.assembly = newAssembly(binary.BigEndian.Uint32(p[4:]))
	}
	_, ack, err := e.assembly.accept(p)
	if err != nil {
		return 0, err
	}
	e.packets <- append([]byte{}, p...)
	if ack != nil {
		if e.badAck {
			ack[4] ^= 1
		}
		e.packets <- ack
	}
	return len(p), nil
}
func (e *echoEndpoint) Read(p []byte) (int, error) {
	select {
	case packet := <-e.packets:
		return copy(p, packet), nil
	case <-time.After(time.Second):
		return 0, fmt.Errorf("test echo timeout")
	}
}
func TestWindowFullMessagesAndStaleAck(t *testing.T) {
	previous := probeWindow
	probeWindow = 4
	defer func() { probeWindow = previous }()
	for _, bad := range []bool{false, true} {
		e := &echoEndpoint{packets: make(chan []byte, 1024), badAck: bad}
		err := probeWindowExchange(context.Background(), e, e, 20, 12, 104, 0)
		if (err != nil) != bad {
			t.Fatal("unexpected stale ACK outcome", bad, err)
		}
	}
}
func TestWindowRejectsEncodedQueueOverflow(t *testing.T) {
	previous := probeWindow
	probeWindow = 4
	defer func() { probeWindow = previous }()
	e := &echoEndpoint{packets: make(chan []byte, 1024)}
	if err := probeWindowExchange(context.Background(), e, e, 20, 10, 65536, 0); err == nil || e.writes != 0 {
		t.Fatal("oversize encoded queue not rejected before writing", err)
	}
}

func TestTimedWindowDrainsAndRejectsEarlyLimit(t *testing.T) {
	previousWindow, previousDuration := probeWindow, probeRunDuration
	probeWindow = 4
	probeRunDuration = 10 * time.Millisecond
	defer func() { probeWindow = previousWindow; probeRunDuration = previousDuration }()
	e := &echoEndpoint{packets: make(chan []byte, 1024)}
	if err := probeWindowExchange(context.Background(), e, e, 512, 100000, 104, 0); err != nil {
		t.Fatal(err)
	}
	if len(e.packets) != 0 {
		t.Fatal("completed with pending responses")
	}
	probeRunDuration = time.Second
	e = &echoEndpoint{packets: make(chan []byte, 1024)}
	if err := probeWindowExchange(context.Background(), e, e, 512, 10, 104, 0); err == nil {
		t.Fatal("early exchange limit counted as a timed pass")
	}
}
