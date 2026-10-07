package main

import (
	"bytes"
	"context"
	"errors"
	"io"
	"sync"
	"testing"
	"time"
)

type memoryPackets struct {
	in, out        chan []byte
	done, peerDone chan struct{}
	once           sync.Once
}

func memoryPacketPair() (*memoryPackets, *memoryPackets) {
	a, b := make(chan []byte, 32), make(chan []byte, 32)
	x, y := make(chan struct{}), make(chan struct{})
	return &memoryPackets{in: a, out: b, done: x, peerDone: y}, &memoryPackets{in: b, out: a, done: y, peerDone: x}
}
func (m *memoryPackets) Read(b []byte) (int, error) {
	select {
	case <-m.done:
		return 0, io.EOF
	case <-m.peerDone:
		return 0, io.EOF
	case p := <-m.in:
		if len(p) > len(b) {
			return 0, io.ErrShortBuffer
		}
		return copy(b, p), nil
	}
}
func (m *memoryPackets) Write(b []byte) (int, error) {
	select {
	case <-m.done:
		return 0, io.ErrClosedPipe
	case <-m.peerDone:
		return 0, io.ErrClosedPipe
	case m.out <- append([]byte{}, b...):
		return len(b), nil
	}
}
func (m *memoryPackets) SetReadDeadline(time.Time) error  { return nil }
func (m *memoryPackets) SetWriteDeadline(time.Time) error { return nil }
func (m *memoryPackets) Close() error                     { m.once.Do(func() { close(m.done) }); return nil }

func TestDuplexSimultaneousFragmentedTraffic(t *testing.T) {
	for _, payload := range []int{20, 512} {
		x, y := memoryPacketPair()
		a, _ := newDuplexLink(987, payload)
		b, _ := newDuplexLink(987, payload)
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		results := make(chan error, 2)
		go func() { results <- a.Run(ctx, x) }()
		go func() { results <- b.Run(ctx, y) }()
		for batch := 0; batch < 12; batch++ {
			var wantA, wantB [][]byte
			for i := 0; i < 4; i++ {
				wantA = append(wantA, bytes.Repeat([]byte{byte(batch + i)}, 104+i*97))
				wantB = append(wantB, bytes.Repeat([]byte{byte(batch + i + 50)}, 256+i*47))
				if err := a.Send(wantA[i]); err != nil {
					t.Fatal(err)
				}
				if err := b.Send(wantB[i]); err != nil {
					t.Fatal(err)
				}
			}
			for i := 0; i < 4; i++ {
				select {
				case got := <-a.Messages():
					if !bytes.Equal(got, wantB[i]) {
						t.Fatal("A payload/order")
					}
				case <-ctx.Done():
					t.Fatal("A stalled")
				}
				select {
				case got := <-b.Messages():
					if !bytes.Equal(got, wantA[i]) {
						t.Fatal("B payload/order")
					}
				case <-ctx.Done():
					t.Fatal("B stalled")
				}
			}
			deadline := time.Now().Add(time.Second)
			for a.QueuedBytes() != 0 || b.QueuedBytes() != 0 {
				if time.Now().After(deadline) {
					t.Fatal("ACK backlog")
				}
				time.Sleep(time.Millisecond)
			}
		}
		for _, link := range []*duplexLink{a, b} {
			stats := link.Stats()
			if stats.Sent != 48 || stats.Confirmed != 48 || stats.QueuedBytes != 0 || stats.PeakQueuedBytes <= 0 || stats.PeakQueuedBytes > duplexBudget {
				t.Fatalf("bounded completed traffic statistics: %+v", stats)
			}
		}
		cancel()
		for i := 0; i < 2; i++ {
			select {
			case <-results:
			case <-time.After(time.Second):
				t.Fatal("cancellation leaked worker")
			}
		}
		if a.QueuedBytes() != 0 || b.QueuedBytes() != 0 {
			t.Fatal("cancel did not retire queue")
		}
		if err := a.Send([]byte{1}); err == nil {
			t.Fatal("closed session reused")
		}
	}
}

func TestDuplexQueueBounds(t *testing.T) {
	d, _ := newDuplexLink(1, 512)
	message := make([]byte, maxMessage)
	for i := 0; i < 3; i++ {
		if err := d.Send(message); err != nil {
			t.Fatal(err)
		}
	}
	if err := d.Send(message); err == nil {
		t.Fatal("encoded budget overflow accepted")
	}
	if d.QueuedBytes() > duplexBudget {
		t.Fatal("budget exceeded")
	}
	small, _ := newDuplexLink(1, 20)
	if err := small.Send(message); err == nil {
		t.Fatal("MTU-dependent encoded overhead ignored")
	}
	for i := 0; i < 8; i++ {
		if err := small.Send([]byte{1}); err != nil {
			t.Fatal(err)
		}
	}
	if err := small.Send([]byte{1}); err == nil {
		t.Fatal("message count unbounded")
	}
}

func TestDuplexCorruptionPeerExitAndCancel(t *testing.T) {
	for _, kind := range []string{"stale", "crc", "ack", "exit", "cancel", "blocked-write"} {
		x, y := memoryPacketPair()
		d, _ := newDuplexLink(123, 512)
		ctx, cancel := context.WithCancel(context.Background())
		result := make(chan error, 1)
		if kind == "blocked-write" {
			x.out = make(chan []byte)
		}
		go func() { result <- d.Run(ctx, x) }()
		switch kind {
		case "blocked-write":
			if err := d.Send(make([]byte, 104)); err != nil {
				t.Fatal(err)
			}
			time.Sleep(10 * time.Millisecond)
			cancel()
		case "cancel":
			cancel()
		case "exit":
			y.Close()
		case "ack":
			y.Write(frameHeaderBytes(frameAck, 123, 1, 0))
		default:
			epoch := uint32(123)
			if kind == "stale" {
				epoch++
			}
			frames, _ := fragmentMessage(512, epoch, 1, []byte("identity"))
			if kind == "crc" {
				frames[0][len(frames[0])-1] ^= 1
			}
			y.Write(frames[0])
		}
		select {
		case err := <-result:
			if err == nil {
				t.Fatal("failure accepted")
			}
			if kind == "cancel" && !errors.Is(err, context.Canceled) {
				t.Fatalf("cancel: %v", err)
			}
		case <-time.After(time.Second):
			t.Fatal("terminal event did not stop worker")
		}
		cancel()
		y.Close()
		if d.QueuedBytes() != 0 {
			t.Fatal("terminal queue retained")
		}
	}
}

func TestDuplexMaxMessageAndReceiveOverflow(t *testing.T) {
	x, y := memoryPacketPair()
	a, _ := newDuplexLink(543, 512)
	b, _ := newDuplexLink(543, 512)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	results := make(chan error, 2)
	go func() { results <- a.Run(ctx, x) }()
	go func() { results <- b.Run(ctx, y) }()
	want := bytes.Repeat([]byte{0xf3}, maxMessage)
	if err := a.Send(want); err != nil {
		t.Fatal(err)
	}
	if err := b.Send(want); err != nil {
		t.Fatal(err)
	}
	for _, d := range []*duplexLink{a, b} {
		select {
		case got := <-d.Messages():
			if !bytes.Equal(got, want) {
				t.Fatal("max message corrupt")
			}
		case <-ctx.Done():
			t.Fatal("max message blocked")
		}
	}
	cancel()
	for i := 0; i < 2; i++ {
		select {
		case <-results:
		case <-time.After(time.Second):
			t.Fatal("max message cancellation")
		}
	}
	// A peer cannot force the worker to grow its receive queue when the
	// consumer stops. Four delivered plus four staged messages are bounded;
	// a sender bypassing the confirmation window cannot inject a ninth.
	// No partial message is ever exposed on overflow.
	x, y = memoryPacketPair()
	d, _ := newDuplexLink(88, 512)
	ctx, cancel = context.WithCancel(context.Background())
	defer cancel()
	go func() { results <- d.Run(ctx, x) }()
	for seq := uint32(1); seq <= 9; seq++ {
		frames, _ := fragmentMessage(512, 88, seq, []byte{byte(seq)})
		if _, err := y.Write(frames[0]); err != nil {
			t.Fatal(err)
		}
	}
	select {
	case err := <-results:
		if err == nil {
			t.Fatal("receive overflow accepted")
		}
	case <-time.After(time.Second):
		t.Fatal("receive overflow did not stop")
	}
	if _, ok := <-d.Messages(); ok {
		t.Fatal("stale receive queue survived failure")
	}
	y.Close()
}

func TestDuplexSlowConsumerBackpressureStillServicesReverseTraffic(t *testing.T) {
	x, y := memoryPacketPair()
	a, _ := newDuplexLink(765, 512)
	b, _ := newDuplexLink(765, 512)
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	results := make(chan error, 2)
	go func() { results <- a.Run(ctx, x) }()
	go func() { results <- b.Run(ctx, y) }()
	for i := 0; i < 8; i++ {
		if err := a.Send([]byte{byte(i)}); err != nil {
			t.Fatal(err)
		}
	}
	deadline := time.Now().Add(time.Second)
	for len(b.out) < 4 || a.Stats().Confirmed < 4 {
		if time.Now().After(deadline) {
			t.Fatal("consumer queue never filled")
		}
		time.Sleep(time.Millisecond)
	}
	time.Sleep(20 * time.Millisecond)
	select {
	case err := <-results:
		t.Fatalf("slow consumer disconnected: %v", err)
	default:
	}
	if a.Stats().Confirmed != 4 || a.QueuedBytes() == 0 {
		t.Fatal("completion did not apply bounded backpressure")
	}
	// ACK/data in the other direction must keep moving while B does not consume.
	if err := b.Send([]byte{99}); err != nil {
		t.Fatal(err)
	}
	select {
	case got := <-a.Messages():
		if !bytes.Equal(got, []byte{99}) {
			t.Fatal("reverse payload")
		}
	case <-ctx.Done():
		t.Fatal("reverse direction deadlocked")
	}
	for i := 0; i < 8; i++ {
		select {
		case got := <-b.Messages():
			if !bytes.Equal(got, []byte{byte(i)}) {
				t.Fatal("delayed payload/order")
			}
		case <-ctx.Done():
			t.Fatal("draining consumer did not restore progress")
		}
	}
	deadline = time.Now().Add(time.Second)
	for a.QueuedBytes() != 0 || b.QueuedBytes() != 0 {
		if time.Now().After(deadline) {
			t.Fatal("confirmation queue did not drain")
		}
		time.Sleep(time.Millisecond)
	}
	cancel()
	for i := 0; i < 2; i++ {
		select {
		case <-results:
		case <-time.After(time.Second):
			t.Fatal("cancel leaked worker")
		}
	}
}

func TestDuplexRemoteEOFDeliversConfirmedBytesButCancelDiscards(t *testing.T) {
	for _, remoteEOF := range []bool{true, false} {
		x, y := memoryPacketPair()
		d, _ := newDuplexLink(311, 512)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		result := make(chan error, 1)
		go func() { result <- d.Run(ctx, x) }()
		frames, _ := fragmentMessage(512, 311, 1, []byte("already received identity"))
		if _, err := y.Write(frames[0]); err != nil {
			t.Fatal(err)
		}
		buffer := make([]byte, 513)
		for {
			n, err := y.Read(buffer)
			if err != nil {
				t.Fatal(err)
			}
			if n >= frameHeader && buffer[1] == frameAck {
				break
			}
		}
		if remoteEOF {
			y.Close()
		} else {
			cancel()
		}
		select {
		case <-result:
		case <-time.After(time.Second):
			t.Fatal("terminal endpoint did not stop")
		}
		data, ok := <-d.Messages()
		if remoteEOF {
			if !ok || !bytes.Equal(data, []byte("already received identity")) {
				t.Fatal("confirmed bytes discarded before consumer read")
			}
			if _, ok := <-d.Messages(); ok {
				t.Fatal("terminal queue not closed")
			}
		} else if ok {
			t.Fatal("cancel exposed a stale message")
		}
		cancel()
		y.Close()
	}
}
