package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"syscall"
	"time"
)

var errDuplexQueueFull = errors.New("duplex queue full")
var errTransportVersion = errors.New("BLE transport version mismatch")

// Candidate builds opt in with -ldflags "-X main.bleTransportVersion=4".
// Normal builds keep the installed v3 path until physical validation.
var bleTransportVersion = "3"

// Legacy radios carry only 27 ACL bytes per packet. RFU messages often end
// in zero-filled fixed-size buffers; omit that padding on the radio and restore
// it before the frontend sees any bytes. The length and retained prefix remain
// covered by the existing duplex CRC. Version 4 negotiates this codec before
// forwarding the game identity; old workers fail before starting emulation.
const bridgeChunkLimit = 4096

func encodeBridgeChunk(data []byte) ([]byte, error) {
	if len(data) == 0 || len(data) > bridgeChunkLimit {
		return nil, fmt.Errorf("invalid bridge chunk length")
	}
	end := len(data)
	for end > 0 && data[end-1] == 0 {
		end--
	}
	encoded := make([]byte, 2+end)
	binary.BigEndian.PutUint16(encoded, uint16(len(data)))
	copy(encoded[2:], data[:end])
	return encoded, nil
}

func decodeBridgeChunk(encoded []byte) ([]byte, error) {
	if len(encoded) < 2 {
		return nil, fmt.Errorf("short bridge chunk")
	}
	length := int(binary.BigEndian.Uint16(encoded))
	if length == 0 || length > bridgeChunkLimit || len(encoded)-2 > length {
		return nil, fmt.Errorf("invalid bridge chunk length")
	}
	data := make([]byte, length)
	copy(data, encoded[2:])
	return data, nil
}

type dataStream interface {
	io.ReadWriteCloser
	SetWriteDeadline(time.Time) error
}

// The worker forwards the existing NextUI stream unchanged. It does not
// accept a game identity or invoke core callbacks; the frontend still does
// the protocol 3 ROM/core/serial-mode handshake on the emulation thread.
func bridgeSession(ctx context.Context, endpoint duplexEndpoint, payload int, epoch uint32, host bool, stream dataStream, ready func() error) error {
	if bleTransportVersion != "3" && bleTransportVersion != "4" {
		return fmt.Errorf("unsupported BLE transport build")
	}
	transportHello := []byte("NUIBLE" + bleTransportVersion + "\x00")
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	d, err := newDuplexLink(epoch, payload)
	if err != nil {
		return err
	}
	d.compact = bleTransportVersion == "4"
	result := make(chan error, 1)
	go func() { result <- d.Run(ctx, endpoint) }()
	watchDone := make(chan struct{})
	watchExited := make(chan struct{})
	report := func(final bool) {
		stats := d.Stats()
		fmt.Printf("BLE stream queue: final=%t queued_bytes=%d peak_bytes=%d sent=%d confirmed=%d ack_max_us=%d\n", final, stats.QueuedBytes, stats.PeakQueuedBytes, stats.Sent, stats.Confirmed, stats.AckMaxUs)
	}
	go func() {
		defer close(watchExited)
		ticker := time.NewTicker(5 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				stream.Close()
				return
			case <-watchDone:
				return
			case <-ticker.C:
				report(false)
			}
		}
	}()
	defer func() {
		cancel()
		stream.Close()
		<-result
		close(watchDone)
		<-watchExited
		report(true)
	}()

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
			return errTransportVersion
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
	outgoingDone := make(chan struct{})
	go func() {
		defer close(outgoingDone)
		hello := make([]byte, 160)
		if _, err := io.ReadFull(stream, hello); err != nil {
			results <- err
			return
		}
		encodedHello, err := encodeBridgeChunk(hello)
		if err == nil {
			err = send(ctx, encodedHello)
		}
		if err != nil {
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
			for offset := 0; offset < len(packet); offset += bridgeChunkLimit {
				end := offset + bridgeChunkLimit
				if end > len(packet) {
					end = len(packet)
				}
				encoded, err := encodeBridgeChunk(packet[offset:end])
				if err == nil {
					err = send(ctx, encoded)
				}
				if err != nil {
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
				message, err := decodeBridgeChunk(message)
				if err != nil {
					results <- err
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
	consumed := 0
	select {
	case err = <-results:
		consumed = 1
	case <-ctx.Done():
		err = ctx.Err()
	}
	// EOF/closed frontend can be an identity rejection. Flush already enqueued
	// bytes for at most 750ms in this helper, never on the UI/core thread. Ordinary
	// cancel still cancels ctx immediately; malformed local frames are not drained.
	if consumed == 1 && (errors.Is(err, io.EOF) || errors.Is(err, io.ErrClosedPipe) || errors.Is(err, syscall.EPIPE) || errors.Is(err, os.ErrClosed)) {
		deadline := time.NewTimer(750 * time.Millisecond)
		tick := time.NewTicker(time.Millisecond)
	drain:
		for {
			outgoingFinished := false
			select {
			case <-outgoingDone:
				outgoingFinished = true
			default:
			}
			if outgoingFinished && d.QueuedBytes() == 0 {
				break
			}
			select {
			case <-ctx.Done():
				break drain
			case <-deadline.C:
				break drain
			case <-tick.C:
			case <-results:
				consumed++
			}
		}
		tick.Stop()
		deadline.Stop()
	}
	cancel()
	stream.Close()
	for consumed < 2 {
		<-results
		consumed++
	}

	return err
}
