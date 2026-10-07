package main

import (
	"testing"
)

func TestPeripheralDuplexBootstrapAndQueueOwnership(t *testing.T) {
	p := &Peripheral{mtu: 517, notifying: true}
	e := newPeripheralPackets(p)
	p.duplex = e
	if err := e.accept(frameHeaderBytes(frameAck, 44, 1, 0)); err == nil {
		t.Fatal("ACK established a session")
	}
	frames, _ := fragmentMessage(512, 44, 1, []byte("local identity"))
	if err := e.accept(frames[0]); err != nil {
		t.Fatal(err)
	}
	if epoch := <-e.ready; epoch != 44 {
		t.Fatal("wrong epoch")
	}
	buffer := make([]byte, 512)
	if n, err := e.Read(buffer); err != nil || n != len(frames[0]) {
		t.Fatal("first fragment not retained")
	}
	for i := 0; i < 16; i++ {
		if err := e.accept(frames[0]); err != nil {
			t.Fatal(err)
		}
	}
	if err := e.accept(frames[0]); err == nil {
		t.Fatal("unbounded inbound queue")
	}
	e.Close()
	if err := e.accept(frames[0]); err == nil {
		t.Fatal("closed endpoint accepts data")
	}
	// Unsubscription closes this owned worker, without touching any device
	// or restarting bluetoothd. A new session needs a new application.
	p2 := &Peripheral{mtu: 517, notifying: true}
	e2 := newPeripheralPackets(p2)
	p2.duplex = e2
	c := &Characteristic{Peripheral: p2, TX: true}
	if err := c.StopNotify(); err != nil {
		t.Fatal(err)
	}
	if _, err := e2.Read(buffer); err == nil {
		t.Fatal("unsubscription left reader blocked")
	}
}
