package main

import (
	"encoding/base64"
	"encoding/json"
	"github.com/godbus/dbus/v5"
	"os"
	"testing"
	"time"
)

func TestControlFragmentIsolation(t *testing.T) {
	var a controlAssembler
	d := base64.StdEncoding.EncodeToString([]byte("hello"))
	if b, e := a.append(1, 0, 2, d); e != nil || b != nil {
		t.Fatal(b, e)
	}
	if _, e := a.append(2, 1, 2, d); e == nil {
		t.Fatal("mixed logical messages accepted")
	}
	if _, e := a.append(3, 0, 257, d); e == nil {
		t.Fatal("unbounded fragments accepted")
	}
	b, e := a.append(4, 0, 1, d)
	if e != nil || string(b) != "hello" {
		t.Fatal(string(b), e)
	}
}
func TestControlPeerAndEpoch(t *testing.T) {
	peer := dbus.ObjectPath("/org/bluez/hci0/dev_01_02_03_04_05_06")
	m := &Mic{receiver: string(peer), controls: true, token: "0123456789abcdef", alerts: make(chan string, 8)}
	c := &Characteristic{m: m}
	payload := base64.StdEncoding.EncodeToString([]byte(`{"op":"state","state":{"mode":"codex","codexAvailable":true,"target":"task"}}`))
	// Fragment into <=72 bytes.
	raw, _ := base64.StdEncoding.DecodeString(payload)
	send := func(token string, device dbus.ObjectPath) *dbus.Error {
		var err *dbus.Error
		for i := 0; i < len(raw); i += 72 {
			end := i + 72
			if end > len(raw) {
				end = len(raw)
			}
			b, _ := json.Marshal(map[string]any{"op": "c", "token": token, "id": 1, "p": i / 72, "n": (len(raw) + 71) / 72, "d": base64.StdEncoding.EncodeToString(raw[i:end])})
			err = c.WriteValue(b, map[string]dbus.Variant{"device": dbus.MakeVariant(device)})
			if err != nil {
				return err
			}
		}
		return err
	}
	if send("stale", peer) == nil {
		t.Fatal("old connection controls accepted")
	}
	if send(m.token, dbus.ObjectPath("/org/bluez/hci0/dev_11_12_13_14_15_16")) == nil {
		t.Fatal("different receiver controls accepted")
	}
	if e := send(m.token, peer); e != nil || m.state.Control.Mode != "codex" {
		t.Fatal(e, m.state.Control)
	}
}
func TestBatteryUnknownAndValid(t *testing.T) {
	p := t.TempDir()
	if readBattery(p).Percent != nil {
		t.Fatal("missing battery is not zero")
	}
	os.WriteFile(p+"/capacity", []byte("95\n"), 0600)
	os.WriteFile(p+"/status", []byte("Charging\n"), 0600)
	b := readBattery(p)
	if b.Percent == nil || *b.Percent != 95 || !b.Charging {
		t.Fatal(b)
	}
	os.WriteFile(p+"/capacity", []byte("101"), 0600)
	if readBattery(p).Percent != nil {
		t.Fatal("invalid reading accepted")
	}
}
func TestNotificationDedup(t *testing.T) {
	m := &Mic{alerts: make(chan string, 8)}
	b := []byte(`{"op":"notify","event":"task:turn:stop","kind":"completed"}`)
	if m.applyControl(b) != nil || m.applyControl(b) != nil || len(m.alerts) != 1 {
		t.Fatal("duplicate reminder")
	}
}

func TestHelloReadinessAndRemoteGate(t *testing.T) {
	peer := dbus.ObjectPath("/org/bluez/hci0/dev_01_02_03_04_05_06")
	m := &Mic{notify: true, alerts: make(chan string, 8)}
	c := &Characteristic{m: m}
	hello, _ := json.Marshal(map[string]any{"op": "hello", "version": 2, "token": "0123456789abcdef", "packet": 182, "remote": false})
	if c.WriteValue(hello, nil) == nil {
		t.Fatal("hello without a receiver was accepted")
	}
	if e := c.WriteValue(hello, map[string]dbus.Variant{"device": dbus.MakeVariant(peer)}); e != nil || m.state.State != "service" {
		t.Fatal(e, m.state.State)
	}
	if m.start(false) == nil {
		t.Fatal("recording before control handshake was accepted")
	}
	if e := m.applyControl([]byte(`{"op":"state","state":{"mode":"ordinary","remoteAllowed":true}}`)); e != nil {
		t.Fatal(e)
	}
	if m.state.State != "ready" || m.state.Control.RemoteAllowed {
		t.Fatal("untrusted peer enabled remote editing")
	}
	if m.controlAction("control:left") == nil {
		t.Fatal("non-remote profile can send keyboard commands")
	}
	m.remote = true
	m.state.State = "recording"
	if m.controlAction("control:submit") == nil {
		t.Fatal("recording allowed an overlapping send")
	}
}
func TestPartialControlStateKeepsTaskPage(t *testing.T) {
	m := &Mic{remote: true, state: State{Control: ControlState{Mode: "codex", Tasks: []Task{{ID: "task-id", Title: "title"}}}}}
	e := m.applyControl([]byte(`{"op":"state","state":{"mode":"codex","controlHint":"done","remoteAllowed":true}}`))
	if e != nil || len(m.state.Control.Tasks) != 1 {
		t.Fatal("hint delta discarded task membership", e)
	}
}

func TestDeepSleepReservation(t *testing.T) {
	m := &Mic{screenAsleep: true, disconnectedSince: time.Now().Add(-6 * time.Minute)}
	if m.reserveDeepSleepWith(func() bool { return false }) {
		t.Fatal("unknown or live physical connection suspended")
	}
	m.state.Connected = true
	if m.reserveDeepSleepWith(func() bool { return true }) {
		t.Fatal("live app connection suspended")
	}
	m.state.Connected = false
	m.disconnectedSince = time.Now()
	if m.reserveDeepSleepWith(func() bool { return true }) {
		t.Fatal("brief reconnection didn't reset timeout")
	}
	m.disconnectedSince = time.Now().Add(-6 * time.Minute)
	if !m.reserveDeepSleepWith(func() bool { return true }) {
		t.Fatal("offline timeout didn't reserve")
	}
	c := &Characteristic{m: m}
	m.notify = true
	peer := dbus.ObjectPath("/org/bluez/hci0/dev_01_02_03_04_05_06")
	if c.WriteValue([]byte(`{"op":"hello","version":2,"token":"0123456789abcdef"}`), map[string]dbus.Variant{"device": dbus.MakeVariant(peer)}) == nil {
		t.Fatal("handshake raced reserved deep sleep")
	}
}
func TestApprovalAndReplyValidation(t *testing.T) {
	m := &Mic{controls: true, remote: true, state: State{Connected: true, Control: ControlState{Approval: &Approval{ID: "expected", ExpiresAt: float64(time.Now().Unix() + 90), AllowAvailable: false}}}}
	for _, cmd := range []string{"control:approval:other:allow", "control:approval:expected:allow", "control:approval:expected:unknown"} {
		if m.controlAction(cmd) == nil {
			t.Fatal("unsafe approval accepted", cmd)
		}
	}
	m.state.Control.Approval.ExpiresAt = float64(time.Now().Unix() - 1)
	if m.controlAction("control:approval:expected:deny") == nil {
		t.Fatal("expired approval accepted")
	}
	if m.applyControl([]byte(`{"op":"state","state":{"mode":"codex","reply":{"task":"task","text":"page","page":1,"pages":1}}}`)) == nil {
		t.Fatal("invalid page accepted")
	}
	m.remote = false
	if m.applyControl([]byte(`{"op":"state","state":{"mode":"codex","remoteAllowed":true,"replyAvailable":true,"reply":{"task":"task","text":"private","page":0,"pages":1},"approval":{"id":"id","task":"task","summary":"private","expiresAt":9999999999}}}`)) != nil || m.state.Control.Reply != nil || m.state.Control.Approval != nil || m.state.Control.ReplyAvailable {
		t.Fatal("untrusted peer exposed control content")
	}
}

func TestSpaceAndEnterControlGuards(t *testing.T) {
	m := &Mic{controls: true, remote: true}
	m.state.Connected = true
	for _, op := range []string{"space", "enter"} {
		m.state.State = "ready"
		if err := m.controlAction("control:" + op); err == nil || err.Error() != "蓝牙连接已断开" {
			t.Fatal(op, err)
		}
		m.state.State = "recording"
		if err := m.controlAction("control:" + op); err == nil || err.Error() != "请先结束或取消录音" {
			t.Fatal(op, err)
		}
		m.state.State = "processing"
		if err := m.controlAction("control:" + op); err == nil || err.Error() != "请先结束或取消录音" {
			t.Fatal(op, err)
		}
		if err := m.controlAction("control:" + op + ":submit"); err == nil || err.Error() != "invalid editing key" {
			t.Fatal(op, err)
		}
	}
}

func TestTaskUnreadAndReplyRevisionRoundTrip(t *testing.T) {
	m := &Mic{remote: true}
	raw := []byte(`{"op":"state","state":{"mode":"codex","remoteAllowed":true,"targetID":"task","tasks":[{"id":"task","title":"开发","status":"completed","unread":7}],"reply":{"task":"task","title":"开发","text":"回复","revision":"turn-2","page":0,"pages":1}}}`)
	if err := m.applyControl(raw); err != nil {
		t.Fatal(err)
	}
	if m.state.Control.Tasks[0].Unread != 7 || m.state.Control.Reply.Revision != "turn-2" {
		t.Fatal("lost live task fields")
	}
	encoded, _ := json.Marshal(m.state.Control)
	var restored ControlState
	if err := json.Unmarshal(encoded, &restored); err != nil || restored.Reply.Revision != "turn-2" || restored.Tasks[0].Unread != 7 {
		t.Fatal("IPC lost live fields", err)
	}
	m.controls = true
	m.token = "token"
	m.state.Connected = true
	if m.controlAction("control:reply:seen:turn-1") == nil {
		t.Fatal("stale reply read accepted")
	}
}
