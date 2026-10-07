package main

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"sort"
	"time"
)

var probeWindow = 1

type pendingEcho struct {
	sequence uint32
	packets  [][]byte
	data     []byte
	fragment int
	complete bool
	start    time.Time
}

// Bounded diagnostic pipelining, not an emulation transport. RTT includes time
// spent behind other messages; throughput improvement alone is not smooth play.
func probeWindowExchange(ctx context.Context, reader, writer packetEndpoint, payload, count, length int, delay time.Duration) error {
	if probeRunDuration != 0 || probeWindow < 2 || probeWindow > 16 || probeWindow*length > 262144 {
		return fmt.Errorf("invalid window measurement bounds")
	}
	var nonce [4]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return err
	}
	epoch := binary.BigEndian.Uint32(nonce[:])
	if epoch == 0 {
		epoch = 1
	}
	echo := newAssembly(epoch)
	pending := make([]pendingEcho, 0, probeWindow)
	samples := make([]int64, 0, count)
	response := make([]byte, 65536)
	sent, fragments, maxPending, queued, maxQueued := 0, 0, 0, 0, 0
	started := time.Now()
	for len(samples) < count {
		if ctx.Err() != nil {
			return ctx.Err()
		}
		for len(pending) < probeWindow && sent < count {
			data := make([]byte, length)
			for j := range data {
				data[j] = byte(sent + j)
			}
			packets, err := fragmentMessage(payload, epoch, uint32(sent+1), data)
			if err != nil {
				return err
			}
			cost := len(data)
			for _, packet := range packets {
				cost += len(packet)
			}
			if queued+cost > 262144 {
				return fmt.Errorf("encoded send queue exceeds diagnostic bound")
			}
			item := pendingEcho{sequence: uint32(sent + 1), packets: packets, data: data, start: time.Now()}
			for _, packet := range packets {
				if err := writer.SetWriteDeadline(time.Now().Add(5 * time.Second)); err != nil {
					return err
				}
				if n, err := writer.Write(packet); err != nil || n != len(packet) {
					return fmt.Errorf("window write: %d %v", n, err)
				}
			}
			pending = append(pending, item)
			queued += cost
			sent++
			if len(pending) > maxPending {
				maxPending = len(pending)
			}
			if queued > maxQueued {
				maxQueued = queued
			}
		}
		if err := reader.SetReadDeadline(time.Now().Add(5 * time.Second)); err != nil {
			return err
		}
		n, err := reader.Read(response)
		if err != nil {
			return err
		}
		item := &pending[0]
		if item.fragment < len(item.packets) {
			if !bytes.Equal(response[:n], item.packets[item.fragment]) {
				return fmt.Errorf("window echo order/integrity mismatch")
			}
			message, _, err := echo.accept(response[:n])
			if err != nil {
				return err
			}
			item.fragment++
			fragments++
			if message != nil {
				if !bytes.Equal(message, item.data) || item.fragment != len(item.packets) {
					return fmt.Errorf("window whole-message integrity failure")
				}
				item.complete = true
			}
			continue
		}
		if !item.complete || !validAck(response[:n], epoch, item.sequence) {
			return fmt.Errorf("window completion ACK mismatch")
		}
		samples = append(samples, time.Since(item.start).Microseconds())
		cost := len(item.data)
		for _, packet := range item.packets {
			cost += len(packet)
		}
		queued -= cost
		pending[0] = pendingEcho{}
		pending = pending[1:]
		if delay > 0 {
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-time.After(delay):
			}
		}
	}
	if queued != 0 || len(pending) != 0 {
		return fmt.Errorf("window queue failed to drain")
	}
	sort.Slice(samples, func(i, j int) bool { return samples[i] < samples[j] })
	fmt.Printf("PASS exchanges=%d fragments=%d bytes_each_way=%d elapsed_ms=%d window=%d max_pending=%d max_queued_bytes=%d final_queued_bytes=%d p50=%d p95=%d p99=%d max=%d\n", count, fragments, count*length, time.Since(started).Milliseconds(), probeWindow, maxPending, maxQueued, queued, samples[count/2], samples[count*95/100], samples[count*99/100], samples[count-1])
	return nil
}
