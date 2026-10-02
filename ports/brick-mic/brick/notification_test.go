package main

import (
	"encoding/json"
	"strings"
	"testing"

	"github.com/godbus/dbus/v5"
)

func notifyPayload(event, kind string, ids []string) []byte {
	b, _ := json.Marshal(map[string]any{"op": "notify", "event": event, "kind": kind, "events": ids})
	return b
}

func TestReconnectNotificationIsSilentButReceipted(t *testing.T) {
	m := &Mic{token: "0123456789abcdef", alerts: make(chan string, 1)}
	packet := []byte(`{"op":"notify","event":"catchup-one","kind":"completed","events":["offline-one"],"silent":true}`)
	if err := m.applyControl(packet); err != nil {
		t.Fatal(err)
	}
	if len(m.alerts) != 0 || len(m.notifyAcks) != 1 || !m.events["offline-one"] {
		t.Fatal("history sync buzzed or lost its receipt")
	}
	if err := m.applyControl(notifyPayload("retry", "completed", []string{"offline-one"})); err != nil || len(m.alerts) != 0 {
		t.Fatal("replayed history buzzed", err)
	}
	if err := m.applyControl(notifyPayload("fresh", "completed", []string{"live-two"})); err != nil || len(m.alerts) != 1 {
		t.Fatal("live completion was silenced", err)
	}
}

func TestNotificationBatchDeduplicatesUnderlyingEvents(t *testing.T) {
	m := &Mic{token: "0123456789abcdef", alerts: make(chan string, 8)}
	first := notifyPayload("batch-one", "completed", []string{"task-a:turn-one:stop", "task-b:turn-one:stop"})
	if err := m.applyControl(first); err != nil {
		t.Fatal(err)
	}
	// A reconnect's batch identity may change. Only actual hook IDs determine
	// whether the handheld vibrates, and a duplicate still receives an ACK.
	duplicate := notifyPayload("batch-two", "completed", []string{"task-a:turn-one:stop", "task-b:turn-one:stop"})
	if err := m.applyControl(duplicate); err != nil || len(m.alerts) != 1 {
		t.Fatal("duplicate batch queued another alert", err, len(m.alerts))
	}
	if len(m.notifyAcks) != 2 {
		t.Fatal("duplicate delivery did not produce another receipt")
	}
	mixed := notifyPayload("batch-three", "waiting", []string{"task-a:turn-one:stop", "task-c:approval-one"})
	if err := m.applyControl(mixed); err != nil || len(m.alerts) != 2 || len(m.events) != 3 {
		t.Fatal("mixed batch should queue exactly one new alert", err, len(m.alerts), len(m.events))
	}
	if err := m.applyControl(mixed); err != nil || len(m.alerts) != 2 {
		t.Fatal("mixed retry queued another alert", err)
	}
}

func TestNotificationReceiptKeepsAcceptedEpoch(t *testing.T) {
	m := &Mic{notify: true, token: "old-epoch-token1", alerts: make(chan string, 8)}
	if err := m.applyControl(notifyPayload("accepted-batch", "completed", []string{"stable-hook"})); err != nil {
		t.Fatal(err)
	}
	peer := dbus.ObjectPath("/org/bluez/hci0/dev_01_02_03_04_05_06")
	c := &Characteristic{m: m}
	if err := c.WriteValue([]byte(`{"op":"hello","version":2,"token":"new-epoch-token2","packet":182}`), map[string]dbus.Variant{"device": dbus.MakeVariant(peer)}); err != nil {
		t.Fatal(err)
	}
	firstReceipt := <-m.notifyAcks
	var receipt map[string]string
	if err := json.Unmarshal(firstReceipt.payload(), &receipt); err != nil {
		t.Fatal(err)
	}
	if receipt["op"] != "notifyAck" || receipt["event"] != "accepted-batch" || receipt["token"] != "old-epoch-token1" {
		t.Fatal("delayed ACK adopted a new epoch", receipt)
	}
	if err := m.applyControl(notifyPayload("reconnected-batch", "completed", []string{"stable-hook"})); err != nil || len(m.alerts) != 1 {
		t.Fatal("hello cleared notification deduplication", err, len(m.alerts))
	}
	secondReceipt := <-m.notifyAcks
	if secondReceipt.Event != "reconnected-batch" || secondReceipt.Token != "new-epoch-token2" {
		t.Fatal("duplicate wasn't ACKed against the receiving connection", secondReceipt)
	}
}

func TestNotificationFullQueueCanRetry(t *testing.T) {
	m := &Mic{token: "0123456789abcdef", alerts: make(chan string, 1)}
	m.alerts <- "waiting"
	b := notifyPayload("reliable-batch", "completed", []string{"unseen-hook"})
	if m.applyControl(b) == nil {
		t.Fatal("saturated alert queue should reject delivery")
	}
	if m.events["unseen-hook"] || len(m.notifyAcks) != 0 {
		t.Fatal("rejected delivery was marked seen or ACKed")
	}
	<-m.alerts
	if err := m.applyControl(b); err != nil || len(m.alerts) != 1 || !m.events["unseen-hook"] || len(m.notifyAcks) != 1 {
		t.Fatal("retry after capacity recovered lost the alert", err)
	}
}

func TestNotificationFullReceiptQueueDoesNotAlert(t *testing.T) {
	m := &Mic{alerts: make(chan string, 1), notifyAcks: make(chan notificationReceipt, 1)}
	m.notifyAcks <- notificationReceipt{Event: "previous", Token: "previous-epoch"}
	if m.applyControl(notifyPayload("batch", "completed", []string{"hook"})) == nil || m.events["hook"] || len(m.alerts) != 0 {
		t.Fatal("receipt backpressure accepted an unacknowledgeable alert")
	}
	<-m.notifyAcks
	if err := m.applyControl(notifyPayload("batch", "completed", []string{"hook"})); err != nil || len(m.alerts) != 1 {
		t.Fatal("receipt backpressure could not recover", err)
	}
}

func TestNotificationValidationDoesNotPoisonDeduplication(t *testing.T) {
	tooMany := make([]string, 101)
	for i := range tooMany {
		tooMany[i] = "hook"
	}
	cases := [][]byte{
		notifyPayload("batch", "unknown", []string{"hook"}),
		notifyPayload("", "completed", []string{"hook"}),
		notifyPayload(strings.Repeat("b", 161), "completed", []string{"hook"}),
		notifyPayload("batch", "completed", nil),
		notifyPayload("batch", "completed", []string{}),
		notifyPayload("batch", "completed", []string{""}),
		notifyPayload("batch", "completed", []string{strings.Repeat("h", 161)}),
		notifyPayload("batch", "completed", tooMany),
		[]byte(`{"op":"notify","event":"batch","kind":"completed","events":"hook"}`),
	}
	for _, b := range cases {
		m := &Mic{alerts: make(chan string, 1)}
		if m.applyControl(b) == nil || len(m.events) != 0 || len(m.alerts) != 0 || len(m.notifyAcks) != 0 {
			t.Fatalf("invalid delivery changed reminder state: %s", b)
		}
	}
	// Duplicate IDs inside one accepted batch count as a single underlying event.
	m := &Mic{alerts: make(chan string, 1)}
	if err := m.applyControl(notifyPayload("batch", "completed", []string{"hook", "hook"})); err != nil || len(m.eventOrder) != 1 {
		t.Fatal("same-batch repetition altered the deduplication history", err)
	}
}

func TestNotificationLegacyEventAcknowledgedOnce(t *testing.T) {
	m := &Mic{token: "0123456789abcdef", alerts: make(chan string, 1)}
	b := []byte(`{"op":"notify","event":"task:turn:stop","kind":"completed"}`)
	if err := m.applyControl(b); err != nil {
		t.Fatal(err)
	}
	if err := m.applyControl(b); err != nil || len(m.alerts) != 1 || len(m.notifyAcks) != 2 {
		t.Fatal("legacy event delivery was not idempotent", err)
	}
	for len(m.notifyAcks) != 0 {
		r := <-m.notifyAcks
		if r.Event != "task:turn:stop" || r.Token != m.token {
			t.Fatal("legacy receipt lost its event", r)
		}
	}
}
