package main

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"sort"
	"sync"
	"time"

	"github.com/godbus/dbus/v5"
)

var probeDuplex bool

func ownedPeerDisconnected(signal *dbus.Signal, peer dbus.ObjectPath) bool {
	if signal == nil || peer == "" || signal.Path != peer || signal.Name != "org.freedesktop.DBus.Properties.PropertiesChanged" {
		return false
	}
	var iface string
	var values map[string]dbus.Variant
	var invalid []string
	if dbus.Store(signal.Body, &iface, &values, &invalid) != nil || iface != "org.bluez.Device1" {
		return false
	}
	connected, ok := values["Connected"].Value().(bool)
	return ok && !connected
}

// Dedicated single-session endpoint. The helper process owns the exported
// GATT application and closes it on exit; it does not reuse diagnostic FDs.
type peripheralPackets struct {
	p       *Peripheral
	packets chan []byte
	ready   chan uint32
	done    chan struct{}
	once    sync.Once
	started bool // accessed only under the peripheral mutex
}

func newPeripheralPackets(p *Peripheral) *peripheralPackets {
	return &peripheralPackets{p: p, packets: make(chan []byte, 16), ready: make(chan uint32, 1), done: make(chan struct{})}
}

func (e *peripheralPackets) accept(packet []byte) error {
	minimum := frameHeader
	if e.started && bleTransportVersion == "4" && len(packet) > 0 && packet[0] == compactTag {
		minimum = compactHeader
	}
	if len(packet) < minimum || len(packet) > int(e.p.mtu)-3 {
		return fmt.Errorf("invalid peripheral packet")
	}
	if !e.started {
		if len(packet) <= frameHeader || packet[0] != 1 || packet[1] != frameData || binary.BigEndian.Uint32(packet[8:]) != 1 || binary.BigEndian.Uint32(packet[12:]) != 0 {
			return fmt.Errorf("invalid first duplex packet")
		}
		epoch := binary.BigEndian.Uint32(packet[4:])
		if epoch == 0 {
			return fmt.Errorf("invalid session epoch")
		}
		e.started = true
		e.ready <- epoch
	}
	select {
	case <-e.done:
		return io.ErrClosedPipe
	default:
	}
	select {
	case e.packets <- append([]byte{}, packet...):
		return nil
	default:
		return fmt.Errorf("peripheral packet queue full")
	}
}
func (e *peripheralPackets) Read(buffer []byte) (int, error) {
	select {
	case <-e.done:
		return 0, io.EOF
	case p := <-e.packets:
		if len(p) > len(buffer) {
			return 0, io.ErrShortBuffer
		}
		return copy(buffer, p), nil
	}
}
func (e *peripheralPackets) Write(packet []byte) (int, error) {
	select {
	case <-e.done:
		return 0, io.ErrClosedPipe
	default:
	}
	e.p.mu.Lock()
	defer e.p.mu.Unlock()
	if !e.p.notifying {
		return 0, io.ErrClosedPipe
	}
	if err := e.p.sendNotification(packet); err != nil {
		return 0, err
	}
	return len(packet), nil
}
func (e *peripheralPackets) SetReadDeadline(time.Time) error  { return nil }
func (e *peripheralPackets) SetWriteDeadline(time.Time) error { return nil }
func (e *peripheralPackets) Close() error                     { e.once.Do(func() { close(e.done) }); return nil }

func duplexHost(ctx context.Context, conn *dbus.Conn, count, length int) error {
	return peripheralSession(ctx, conn, nil, func(ctx context.Context, endpoint duplexEndpoint, payload int, epoch uint32) error {
		return duplexBenchmark(ctx, endpoint, payload, epoch, count, length, true)
	})
}

func peripheralSession(ctx context.Context, conn *dbus.Conn, roomReady func() error, handler func(context.Context, duplexEndpoint, int, uint32) error) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	p := &Peripheral{conn: conn}
	p.roomReady = roomReady
	e := newPeripheralPackets(p)
	p.duplex = e
	defer e.Close()
	match := []dbus.MatchOption{dbus.WithMatchSender("org.bluez"), dbus.WithMatchInterface("org.freedesktop.DBus.Properties"), dbus.WithMatchMember("PropertiesChanged"), dbus.WithMatchArg(0, "org.bluez.Device1"), dbus.WithMatchPathNamespace(adapter)}
	if err := conn.AddMatchSignal(match...); err != nil {
		return err
	}
	defer conn.RemoveMatchSignal(match...)
	updates := make(chan *dbus.Signal, 32)
	conn.Signal(updates)
	defer conn.RemoveSignal(updates)
	go func() {
		for {
			select {
			case <-ctx.Done():
				return
			case signal := <-updates:
				p.mu.Lock()
				owned := ownedPeerDisconnected(signal, p.peer)
				p.mu.Unlock()
				if owned {
					fmt.Println("DUPLEX_OWNED_PEER_DISCONNECTED")
					e.Close()
					return
				}
			}
		}
	}()
	result := make(chan error, 1)
	go func() {
		select {
		case <-ctx.Done():
			result <- ctx.Err()
		case epoch := <-e.ready:
			p.mu.Lock()
			payload := int(p.mtu) - 3
			p.mu.Unlock()
			if payload > 512 {
				payload = 512
			}
			err := handler(ctx, e, payload, epoch)
			result <- err
			cancel()
		}
	}()
	err := hostPeripheral(ctx, conn, p)
	select {
	case workerErr := <-result:
		if workerErr != nil && !errors.Is(workerErr, context.Canceled) {
			return workerErr
		}
	default:
	}
	return err
}

func duplexBenchmark(ctx context.Context, endpoint duplexEndpoint, payload int, epoch uint32, count, length int, host bool) error {
	d, err := newDuplexLink(epoch, payload)
	if err != nil {
		return err
	}
	workerCtx, cancel := context.WithCancel(ctx)
	defer cancel()
	result := make(chan error, 1)
	go func() {
		err := d.Run(workerCtx, endpoint)
		if err != nil && !errors.Is(err, context.Canceled) {
			fmt.Fprintf(os.Stderr, "DUPLEX_WORKER_ERROR host=%t error=%v\n", host, err)
		}
		result <- err
	}()
	defer func() { cancel(); <-result }()
	data := func(sequence int, host bool) []byte {
		b := make([]byte, length)
		for i := range b {
			b[i] = byte(sequence + i)
			if host {
				b[i] ^= 0xa5
			}
		}
		return b
	}
	started := time.Now()
	samples := make([]int64, 0, count)
	maxQueued := 0
	for base := 0; base < count; base += 4 {
		n := count - base
		if n > 4 {
			n = 4
		}
		batchStart := time.Now()
		for i := 0; i < n; i++ {
			if err := d.Send(data(base+i, host)); err != nil {
				return err
			}
			if queued := d.QueuedBytes(); queued > maxQueued {
				maxQueued = queued
			}
		}
		for i := 0; i < n; i++ {
			select {
			case <-ctx.Done():
				return ctx.Err()
			case message, ok := <-d.Messages():
				if !ok {
					return fmt.Errorf("duplex worker stopped")
				}
				if !bytes.Equal(message, data(base+i, !host)) {
					return fmt.Errorf("duplex payload/order mismatch")
				}
				samples = append(samples, time.Since(batchStart).Microseconds())
			}
		}
		// Retire the bounded batch before submitting a new one. A benchmark
		// must not turn a slow peer into unbounded local application backlog.
		for d.QueuedBytes() != 0 {
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-time.After(time.Millisecond):
			}
		}
	}
	for d.QueuedBytes() != 0 {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(time.Millisecond):
		}
	}
	sort.Slice(samples, func(i, j int) bool { return samples[i] < samples[j] })
	fmt.Printf("DUPLEX_PASS messages=%d bytes_each_direction=%d elapsed_ms=%d window=4 max_queued_bytes=%d final_queued_bytes=0 p50=%d p95=%d p99=%d max=%d\n", count, count*length, time.Since(started).Milliseconds(), maxQueued, samples[count/2], samples[count*95/100], samples[count*99/100], samples[count-1])
	// Host remains alive until the owned runner ends it, allowing the client
	// to consume the final ACK before its ATT socket disconnects.
	if host {
		<-ctx.Done()
	}
	return nil
}

func randomDuplexEpoch() (uint32, error) {
	var nonce [4]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return 0, err
	}
	epoch := binary.BigEndian.Uint32(nonce[:])
	if epoch == 0 {
		epoch = 1
	}
	return epoch, nil
}
