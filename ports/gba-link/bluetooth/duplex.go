package main

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"sync"
	"time"
)

// This worker transports complete opaque messages in both directions. The
// existing NextUI handshake, ROM/core identity and RFU envelopes stay above it.
// It does not start an emulated link or declare a game compatible.
const duplexBudget = 262144
const frameFragmentAck = 3

type duplexEndpoint interface {
	packetEndpoint
	io.Closer
}

type duplexMessage struct {
	data []byte
	cost int
}

type duplexPending struct {
	sequence         uint32
	frames           [][]byte
	index            int
	cost             int
	started          time.Time
	bootstrapWaiting bool
}

type duplexLink struct {
	epoch           uint32
	payload         int
	mu              sync.Mutex
	queued          int
	peakQueued      int
	sent, confirmed uint64
	ackMaxUs        int64
	closed          bool
	running         bool
	in              chan duplexMessage
	out             chan []byte
}

func newDuplexLink(epoch uint32, payload int) (*duplexLink, error) {
	if epoch == 0 || payload <= frameHeader || payload > 512 {
		return nil, fmt.Errorf("invalid duplex session or negotiated payload")
	}
	return &duplexLink{epoch: epoch, payload: payload, in: make(chan duplexMessage, 8), out: make(chan []byte, 4)}, nil
}

// Send is bounded and never waits for the radio. The caller must treat any
// rejection as a terminal link error; dropping a GBA packet is unsafe.
func (d *duplexLink) Send(message []byte) error {
	if len(message) > maxMessage {
		return fmt.Errorf("duplex message too large")
	}
	fragments := (len(message) + 8 + d.payload - frameHeader - 1) / (d.payload - frameHeader)
	cost := len(message) + 8 + fragments*frameHeader
	d.mu.Lock()
	defer d.mu.Unlock()
	if d.closed {
		return fmt.Errorf("duplex link closed")
	}
	if d.queued+cost > duplexBudget {
		return fmt.Errorf("duplex send queue full: %w", errDuplexQueueFull)
	}
	item := duplexMessage{data: append([]byte{}, message...), cost: cost}
	select {
	case d.in <- item:
		d.queued += cost
		d.sent++
		if d.queued > d.peakQueued {
			d.peakQueued = d.queued
		}
		return nil
	default:
		return fmt.Errorf("duplex message queue full: %w", errDuplexQueueFull)
	}
}

func (d *duplexLink) retire(cost int, elapsed time.Duration) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.queued -= cost
	d.confirmed++
	if elapsed.Microseconds() > d.ackMaxUs {
		d.ackMaxUs = elapsed.Microseconds()
	}
}

type duplexStats struct {
	QueuedBytes, PeakQueuedBytes int
	Sent, Confirmed              uint64
	AckMaxUs                     int64
}

func (d *duplexLink) Stats() duplexStats {
	d.mu.Lock()
	defer d.mu.Unlock()
	return duplexStats{d.queued, d.peakQueued, d.sent, d.confirmed, d.ackMaxUs}
}
func (d *duplexLink) QueuedBytes() int        { d.mu.Lock(); defer d.mu.Unlock(); return d.queued }
func (d *duplexLink) Messages() <-chan []byte { return d.out }

// Each Run owns one endpoint and one session. Cancellation closes only this
// endpoint, joins its reader, and retires every queue. Reconnect uses a fresh
// worker and epoch, never old FDs or buffered messages.
func (d *duplexLink) Run(ctx context.Context, endpoint duplexEndpoint) error {
	d.mu.Lock()
	if d.running || d.closed {
		d.mu.Unlock()
		return fmt.Errorf("duplex session already used")
	}
	d.running = true
	d.mu.Unlock()
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	reads := make(chan []byte, 16)
	failure := make(chan error, 1)
	done := make(chan struct{})
	go func() {
		defer close(done)
		buffer := make([]byte, 513)
		for {
			n, err := endpoint.Read(buffer)
			if err != nil {
				select {
				case failure <- err:
				default:
				}
				return
			}
			if n <= 0 || n > d.payload {
				select {
				case failure <- fmt.Errorf("invalid duplex frame length"):
				default:
				}
				return
			}
			packet := append([]byte{}, buffer[:n]...)
			select {
			case reads <- packet:
			case <-ctx.Done():
				return
			}
		}
	}()
	watchDone := make(chan struct{})
	writes := make(chan []byte)
	written := make(chan error, 1)
	writerDone := make(chan struct{})
	go func() {
		defer close(writerDone)
		for {
			select {
			case <-ctx.Done():
				return
			case packet := <-writes:
				err := endpoint.SetWriteDeadline(time.Now().Add(time.Second))
				if err == nil {
					var n int
					n, err = endpoint.Write(packet)
					if err == nil && n != len(packet) {
						err = io.ErrShortWrite
					}
				}
				select {
				case written <- err:
				case <-ctx.Done():
					return
				}
				if err != nil {
					return
				}
			}
		}
	}()
	go func() {
		select {
		case <-ctx.Done():
			endpoint.Close()
		case <-watchDone:
		}
	}()
	defer func() {
		cancel()
		endpoint.Close()
		<-done
		<-writerDone
		close(watchDone)
		d.mu.Lock()
		d.closed = true
		d.queued = 0
		d.mu.Unlock()
		for len(d.in) > 0 {
			<-d.in
		}
	drain:
		for {
			select {
			case <-d.out:
			default:
				break drain
			}
		}
		close(d.out)
	}()
	var controls [][]byte
	write := func(packet []byte) error {
		if len(controls) >= 64 {
			return fmt.Errorf("duplex control queue full")
		}
		controls = append(controls, packet)
		return nil
	}
	writerBusy := false
	received := newAssembly(d.epoch)
	var pending []duplexPending
	type delivery struct {
		message, ack []byte
		sequence     uint32
	}
	var deliveries []delivery
	next := uint32(1)
	bootstrap := true
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()
	for {
		var deliver chan<- []byte
		var delivered []byte
		if len(deliveries) > 0 {
			deliver = d.out
			delivered = deliveries[0].message
		}
		// Only the initial message is fragment-paced. This lets BlueZ finish
		// asynchronous AcquireWrite before a following write arrives, even at
		// default MTU 23. Completion opens the bounded four-message window.
		// One writer keeps ACK order and never blocks the receive dispatcher.
		// This is necessary when both players send multi-fragment bursts.
		var output chan<- []byte
		var outgoing []byte
		dataIndex := -1
		if !writerBusy {
			if len(controls) > 0 {
				outgoing = controls[0]
			} else {
				for i := range pending {
					p := &pending[i]
					if p.index < len(p.frames) && !p.bootstrapWaiting {
						outgoing = p.frames[p.index]
						dataIndex = i
						break
					}
				}
			}
			if outgoing != nil {
				output = writes
			}
		}
		var input <-chan duplexMessage
		limit := 4
		if bootstrap {
			limit = 1
		}
		if len(pending) < limit {
			input = d.in
		}
		select {
		case deliver <- delivered:
			if err := write(deliveries[0].ack); err != nil {
				return err
			}
			deliveries[0] = delivery{}
			deliveries = deliveries[1:]
		case output <- outgoing:
			writerBusy = true
			if dataIndex < 0 {
				controls[0] = nil
				controls = controls[1:]
			} else {
				pending[dataIndex].index++
				if bootstrap {
					pending[dataIndex].bootstrapWaiting = true
				}
			}
		case err := <-written:
			writerBusy = false
			if err != nil {
				return fmt.Errorf("duplex write: %w", err)
			}
		case <-ctx.Done():
			return ctx.Err()
		case err := <-failure:
			if ctx.Err() != nil {
				return ctx.Err()
			}
			return fmt.Errorf("duplex receive: %w", err)
		case item := <-input:
			if next == ^uint32(0) {
				return fmt.Errorf("duplex sequence exhausted")
			}
			frames, err := fragmentMessage(d.payload, d.epoch, next, item.data)
			if err != nil {
				return err
			}
			pending = append(pending, duplexPending{sequence: next, frames: frames, cost: item.cost, started: time.Now()})
			next++
		case packet := <-reads:
			if len(packet) < frameHeader {
				return fmt.Errorf("short duplex frame")
			}
			seq := binary.BigEndian.Uint32(packet[8:])
			switch packet[1] {
			case frameData:
				message, ack, err := received.accept(packet)
				if err != nil {
					return err
				}
				if seq == 1 {
					end := binary.BigEndian.Uint32(packet[12:]) + uint32(len(packet)-frameHeader)
					if err := write(frameHeaderBytes(frameFragmentAck, d.epoch, seq, end)); err != nil {
						return err
					}
				}
				if message != nil {
					// ACK only after handing the complete message to the bounded consumer
					// queue. Withhold completion while it is full, but continue servicing
					// outbound ACKs/data: blocking this dispatcher can deadlock both peers.
					// Each compliant sender has at most four unconfirmed messages, so four
					// staged deliveries plus the four-slot consumer queue remain bounded.
					if len(deliveries) >= 4 {
						return fmt.Errorf("duplex peer exceeded receive window")
					}
					deliveries = append(deliveries, delivery{message, ack, seq})
				} else if ack != nil {
					queued := false
					for _, item := range deliveries {
						if item.sequence == seq {
							queued = true
							break
						}
					}
					if !queued {
						if err := write(ack); err != nil {
							return err
						}
					}
				}
			case frameFragmentAck:
				if !bootstrap || len(pending) != 1 || seq != 1 || !pending[0].bootstrapWaiting {
					return fmt.Errorf("unexpected bootstrap ACK")
				}
				p := &pending[0]
				frame := p.frames[p.index-1]
				end := binary.BigEndian.Uint32(frame[12:]) + uint32(len(frame)-frameHeader)
				expected := frameHeaderBytes(frameFragmentAck, d.epoch, seq, end)
				if string(packet) != string(expected) {
					return fmt.Errorf("invalid bootstrap ACK")
				}
				p.bootstrapWaiting = false
			case frameAck:
				if len(pending) == 0 || !validAck(packet, d.epoch, pending[0].sequence) || pending[0].index != len(pending[0].frames) || pending[0].bootstrapWaiting {
					expected := uint32(0)
					index, total := 0, 0
					waiting := false
					if len(pending) > 0 {
						expected = pending[0].sequence
						index = pending[0].index
						total = len(pending[0].frames)
						waiting = pending[0].bootstrapWaiting
					}
					return fmt.Errorf("invalid duplex completion ACK expected=%d received=%d fragments=%d/%d bootstrap_waiting=%t", expected, seq, index, total, waiting)
				}
				d.retire(pending[0].cost, time.Since(pending[0].started))
				pending[0] = duplexPending{}
				pending = pending[1:]
				bootstrap = false
			default:
				return fmt.Errorf("invalid duplex frame kind")
			}
		case <-ticker.C:
			if len(pending) > 0 && time.Since(pending[0].started) > 5*time.Second {
				return fmt.Errorf("duplex acknowledgement timeout")
			}
		}
	}
}
