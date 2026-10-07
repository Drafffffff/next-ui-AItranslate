package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"time"
)

var errDuplexQueueFull = errors.New("duplex queue full")
var transportHello = []byte("NUIBLE2\x00")

type dataStream interface {
	io.ReadWriteCloser
	SetWriteDeadline(time.Time) error
}

// The worker forwards the existing NextUI stream unchanged. It does not
// accept a game identity or invoke core callbacks; the frontend still does
// the protocol 3 ROM/core/serial-mode handshake on the emulation thread.
func bridgeSession(ctx context.Context, endpoint duplexEndpoint, payload int, epoch uint32, host bool, stream dataStream, ready func() error) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	d, err := newDuplexLink(epoch, payload)
	if err != nil {
		return err
	}
	result := make(chan error, 1)
	go func() { result <- d.Run(ctx, endpoint) }()
	watchDone := make(chan struct{})
	go func() {
		select {
		case <-ctx.Done():
			stream.Close()
		case <-watchDone:
		}
	}()
	defer func() { cancel(); stream.Close(); <-result; close(watchDone) }()
	send := func(sendCtx context.Context, data []byte) error {
		deadline := time.NewTimer(5 * time.Second)
		defer deadline.Stop()
		for {
			if sendCtx.Err() != nil {
				return sendCtx.Err()
			}
			err := d.Send(data)
			if !errors.Is(err, errDuplexQueueFull) {
				return err
			}
			select {
			case <-sendCtx.Done():
				return sendCtx.Err()
			case <-deadline.C:
				return fmt.Errorf("BLE send backlog timed out")
			case <-time.After(time.Millisecond):
			}
		}
	}
	if !host {
		if err := send(ctx, transportHello); err != nil {
			return err
		}
	}
	select {
	case <-ctx.Done():
		return ctx.Err()
	case hello, ok := <-d.Messages():
		if !ok {
			return fmt.Errorf("BLE channel ended before readiness")
		}
		if !bytes.Equal(hello, transportHello) {
			return fmt.Errorf("BLE transport version mismatch")
		}
	}
	if host {
		if err := send(ctx, transportHello); err != nil {
			return err
		}
	}
	for d.QueuedBytes() != 0 {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(time.Millisecond):
		}
	}
	if err := ready(); err != nil {
		return err
	}
	return bridgeStream(ctx, d, stream, send)
}

func bridgeStream(ctx context.Context, d *duplexLink, stream dataStream, send func(context.Context, []byte) error) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	results := make(chan error, 2)
	go func() {
		hello := make([]byte, 160)
		if _, err := io.ReadFull(stream, hello); err != nil {
			results <- err
			return
		}
		if err := send(ctx, hello); err != nil {
			results <- err
			return
		}
		for {
			header := make([]byte, 8)
			if _, err := io.ReadFull(stream, header); err != nil {
				results <- err
				return
			}
			typ, length := binary.BigEndian.Uint32(header), binary.BigEndian.Uint32(header[4:])
			if typ < 1 || typ > 5 || length > maxMessage {
				results <- fmt.Errorf("invalid local NextUI wire frame")
				return
			}
			// Bound encoding overhead even at MTU 23. A maximum-sized NextUI
			// packet plus its header exceeds a single BLE message; split only
			// the transport chunks, preserving every byte and its order.
			packet := make([]byte, 8+int(length))
			copy(packet, header)
			if _, err := io.ReadFull(stream, packet[8:]); err != nil {
				results <- err
				return
			}
			for offset := 0; offset < len(packet); offset += 4096 {
				end := offset + 4096
				if end > len(packet) {
					end = len(packet)
				}
				if err := send(ctx, packet[offset:end]); err != nil {
					results <- err
					return
				}
			}
		}
	}()
	go func() {
		for {
			select {
			case <-ctx.Done():
				results <- ctx.Err()
				return
			case message, ok := <-d.Messages():
				if !ok {
					results <- fmt.Errorf("BLE peer channel closed")
					return
				}
				if err := stream.SetWriteDeadline(time.Now().Add(time.Second)); err != nil {
					results <- err
					return
				}
				for len(message) > 0 {
					n, err := stream.Write(message)
					if err != nil {
						results <- err
						return
					}
					if n <= 0 {
						results <- io.ErrShortWrite
						return
					}
					message = message[n:]
				}
			}
		}
	}()
	var err error
	consumed := false
	select {
	case err = <-results:
		consumed = true
	case <-ctx.Done():
		err = ctx.Err()
	}
	cancel()
	stream.Close()
	// If cancellation won the select, both pumps still need to be joined.
	if !consumed {
		<-results
	}
	<-results
	return err
}
