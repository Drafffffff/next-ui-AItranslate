package main

import (
	"bufio"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"log"
	"os"
	"strconv"
	"strings"
	"time"
)

type Task struct {
	ID     string `json:"id"`
	Title  string `json:"title"`
	Status string `json:"status"`
	Unread int    `json:"unread"`
}
type Reminder struct {
	ID    string `json:"id,omitempty"`
	Task  string `json:"task"`
	Title string `json:"title"`
	Kind  string `json:"kind"`
}
type Approval struct {
	ID             string  `json:"id"`
	Task           string  `json:"task"`
	Title          string  `json:"title"`
	Tool           string  `json:"tool"`
	Summary        string  `json:"summary"`
	ExpiresAt      float64 `json:"expiresAt"`
	AllowAvailable bool    `json:"allowAvailable"`
}
type Reply struct {
	Revision  string `json:"revision,omitempty"`
	Task      string `json:"task"`
	Title     string `json:"title"`
	Text      string `json:"text"`
	Page      int    `json:"page"`
	Pages     int    `json:"pages"`
	Truncated bool   `json:"truncated"`
}
type ControlState struct {
	TargetID       string     `json:"targetID"`
	DraftAvailable *bool      `json:"draftAvailable"`
	FocusPending   bool       `json:"focusPending"`
	RemoteAllowed  bool       `json:"remoteAllowed"`
	Mode           string     `json:"mode"`
	Available      bool       `json:"codexAvailable"`
	Target         string     `json:"target"`
	Tasks          []Task     `json:"tasks"`
	Unread         int        `json:"unread"`
	Hint           string     `json:"controlHint"`
	Alerts         []Reminder `json:"alerts"`
	TaskPage       int        `json:"taskPage"`
	TaskPages      int        `json:"taskPages"`
	ReplyAvailable bool       `json:"replyAvailable"`
	Approval       *Approval  `json:"approval"`
	Reply          *Reply     `json:"reply"`
}
type Battery struct {
	Percent  *int `json:"percent"`
	Charging bool `json:"charging"`
}
type controlAssembler struct {
	id          uint32
	next, total int
	data        []byte
	began       time.Time
}

// The epoch is captured when a notification is accepted. An ACK must never
// acquire a later connection's token while waiting for the telemetry sender.
type notificationReceipt struct {
	Event string
	Token string
}

func (r notificationReceipt) payload() []byte {
	b, _ := json.Marshal(map[string]any{"op": "notifyAck", "event": r.Event, "token": r.Token})
	return b
}

func (a *controlAssembler) append(id uint32, part, total int, data string) ([]byte, error) {
	if total < 1 || total > 256 || part < 0 || part >= total {
		return nil, errors.New("invalid control fragment")
	}
	if part == 0 {
		*a = controlAssembler{id: id, total: total, began: time.Now()}
	}
	if a.id != id || a.total != total || a.next != part || time.Since(a.began) > 5*time.Second {
		*a = controlAssembler{}
		return nil, errors.New("stale control fragment")
	}
	b, e := base64.StdEncoding.DecodeString(data)
	if e != nil || len(b) > 72 || len(a.data)+len(b) > 16384 {
		*a = controlAssembler{}
		return nil, errors.New("oversized control payload")
	}
	a.data = append(a.data, b...)
	a.next++
	if a.next == total {
		out := a.data
		*a = controlAssembler{}
		return out, nil
	}
	return nil, nil
}
func (m *Mic) controlAction(command string) error {
	parts := strings.Split(strings.TrimPrefix(command, "control:"), ":")
	if len(parts) > 3 || len(parts) == 0 {
		return errors.New("invalid command")
	}
	op := parts[0]
	allowed := map[string]bool{"mode": true, "tasks": true, "choose": true, "left": true, "right": true, "up": true, "down": true, "delete": true, "submit": true, "undo": true, "redo": true, "copy": true, "paste": true, "all": true, "newline": true, "space": true, "enter": true, "read": true, "reply": true, "approval": true}
	if (op == "space" || op == "enter") && len(parts) != 1 {
		return errors.New("invalid editing key")
	}
	if !allowed[op] {
		return errors.New("unsupported command")
	}
	m.mu.Lock()
	if !m.state.Connected || !m.controls || !m.remote {
		m.mu.Unlock()
		return errors.New("请连接新版 Mac 应用")
	}
	if m.state.State == "recording" || m.state.State == "processing" {
		m.mu.Unlock()
		return errors.New("请先结束或取消录音")
	}
	token := m.token
	payload := map[string]any{"op": op, "token": token}
	if op == "approval" {
		a := m.state.Control.Approval
		if len(parts) != 3 || a == nil || a.ID != parts[1] || a.ExpiresAt <= float64(time.Now().Unix()) || (parts[2] != "allow" && parts[2] != "deny") || (parts[2] == "allow" && !a.AllowAvailable) {
			m.mu.Unlock()
			return errors.New("审批已变化或过期，请重新查看")
		}
		payload["id"], payload["arg"] = parts[1], parts[2]
	} else if op == "reply" {
		if len(parts) == 3 && parts[1] == "seen" {
			r := m.state.Control.Reply
			if r == nil || r.Task != m.state.Control.TargetID || r.Revision == "" || parts[2] != r.Revision {
				m.mu.Unlock()
				return errors.New("reply changed")
			}
			payload["arg"], payload["revision"] = "seen", r.Revision
		} else if len(parts) != 2 || (parts[1] != "open" && parts[1] != "next" && parts[1] != "prev" && parts[1] != "close") {
			m.mu.Unlock()
			return errors.New("invalid reply action")
		} else {
			payload["arg"] = parts[1]
		}
	} else if op == "choose" {
		if len(parts) != 2 {
			m.mu.Unlock()
			return errors.New("missing task")
		}
		id := parts[1]
		found := false
		for _, task := range m.state.Control.Tasks {
			if task.ID == id {
				found = true
				break
			}
		}
		if !found {
			for _, a := range m.state.Control.Alerts {
				if a.Task == id {
					found = true
					break
				}
			}
		}
		if !found {
			m.mu.Unlock()
			return errors.New("任务列表已变化，请重新选择")
		}
		payload["id"] = id
	} else if len(parts) > 1 {
		payload["arg"] = parts[1]
	}
	m.mu.Unlock()
	b, _ := json.Marshal(payload)
	return m.send(6, 0, 0, b)
}

// Caller holds mu; only a hello-bound receiver can publish state or notifications.
func (m *Mic) applyControl(b []byte) error {
	var v struct {
		Op     string          `json:"op"`
		State  ControlState    `json:"state"`
		Event  string          `json:"event"`
		Events json.RawMessage `json:"events"`
		Kind   string          `json:"kind"`
		Silent bool            `json:"silent"`
	}
	if err := json.Unmarshal(b, &v); err != nil {
		return err
	}
	switch v.Op {
	case "state":
		if len(v.State.TargetID) > 64 || v.State.Mode != "ordinary" && v.State.Mode != "codex" || len(v.State.Tasks) > 12 || len(v.State.Alerts) > 5 || v.State.Unread < 0 || v.State.Unread > 999 {
			return errors.New("invalid state")
		}
		for _, t := range v.State.Tasks {
			if len(t.ID) > 64 || len(t.Title) > 256 || t.Unread < 0 || t.Unread > 999 {
				return errors.New("invalid task")
			}
		}
		var raw struct {
			State map[string]json.RawMessage `json:"state"`
		}
		_ = json.Unmarshal(b, &raw)
		if _, present := raw.State["tasks"]; !present {
			v.State.Tasks = m.state.Control.Tasks
		}
		for _, a := range v.State.Alerts {
			if len(a.ID) > 160 || len(a.Task) > 64 || len(a.Title) > 256 {
				return errors.New("invalid reminder")
			}
		}
		if a := v.State.Approval; a != nil {
			if len(a.ID) > 64 || len(a.Task) > 64 || len(a.Title) > 256 || len(a.Tool) > 128 || len(a.Summary) > 4800 || a.ExpiresAt <= 0 {
				return errors.New("invalid approval")
			}
		}
		if r := v.State.Reply; r != nil {
			if len(r.Task) > 64 || len(r.Title) > 256 || len(r.Text) > 2000 || len(r.Revision) > 64 || r.Page < 0 || r.Pages < 1 || r.Pages > 64 || r.Page >= r.Pages {
				return errors.New("invalid reply")
			}
		}
		v.State.RemoteAllowed = v.State.RemoteAllowed && m.remote
		if !v.State.RemoteAllowed {
			v.State.TargetID = ""
			v.State.FocusPending = false
			v.State.DraftAvailable = nil
			v.State.Approval = nil
			v.State.Reply = nil
			v.State.ReplyAvailable = false
		}
		m.state.Control = v.State
		if m.state.State == "service" {
			m.state.State = "ready"
		}
	case "notify":
		if len(v.Event) < 1 || len(v.Event) > 160 {
			return errors.New("invalid event")
		}
		if v.Kind != "completed" && v.Kind != "waiting" && v.Kind != "failed" {
			return errors.New("invalid notification kind")
		}
		ids := []string{v.Event}
		if v.Events != nil {
			if err := json.Unmarshal(v.Events, &ids); err != nil || len(ids) < 1 || len(ids) > 100 {
				return errors.New("invalid notification events")
			}
		}
		for _, id := range ids {
			if len(id) < 1 || len(id) > 160 {
				return errors.New("invalid notification event")
			}
		}
		if m.events == nil {
			m.events = map[string]bool{}
		}
		if m.notifyAcks == nil {
			m.notifyAcks = make(chan notificationReceipt, 64)
		}
		// applyControl holds mu, which serializes all ACK producers. The
		// telemetry pump can only free queue space while we reserve this slot.
		if len(m.notifyAcks) == cap(m.notifyAcks) {
			return errors.New("notification receipt queue full")
		}
		newEvent := false
		for _, id := range ids {
			newEvent = newEvent || !m.events[id]
		}
		if newEvent {
			if !v.Silent {
				select {
				case m.alerts <- v.Kind:
				default:
					return errors.New("notification queue full")
				}
			}
			// Only accepted deliveries become seen. Silent reconnect snapshots
			// are receipted without entering the hardware alert queue; live
			// deliveries can retry after a saturated queue without losing them.
			for _, id := range ids {
				if !m.events[id] {
					m.events[id] = true
					m.eventOrder = append(m.eventOrder, id)
				}
			}
			for len(m.eventOrder) > 4096 {
				delete(m.events, m.eventOrder[0])
				m.eventOrder = m.eventOrder[1:]
			}
		}
		m.notifyAcks <- notificationReceipt{Event: v.Event, Token: m.token}
	default:
		return errors.New("unsupported control payload")
	}
	return nil
}

// Keep a pending BLE handshake from racing the decision to suspend this app.
// A failed BlueZ read is unknown, not proof of disconnection.
func (m *Mic) reserveDeepSleep() bool {
	if m.conn == nil {
		return false
	}
	return m.reserveDeepSleepWith(func() bool {
		ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
		defer cancel()
		var managed objects
		if err := m.conn.Object("org.bluez", "/").CallWithContext(ctx, "org.freedesktop.DBus.ObjectManager.GetManagedObjects", 0).Store(&managed); err != nil {
			return false
		}
		for _, interfaces := range managed {
			if device, ok := interfaces["org.bluez.Device1"]; ok {
				connected, known := device["Connected"]
				if !known || connected.Value() != false {
					return false
				}
			}
		}
		return true
	})
}
func (m *Mic) reserveDeepSleepWith(disconnected func() bool) bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	if !m.screenAsleep || m.state.Connected || m.receiver != "" || m.state.State == "recording" || m.deepReserved || m.disconnectedSince.IsZero() || time.Since(m.disconnectedSince) < 5*time.Minute {
		return false
	}
	if !disconnected() {
		return false
	}
	m.deepReserved = true
	return true
}

func readBattery(dir string) Battery {
	b := Battery{}
	if raw, e := os.ReadFile(dir + "/capacity"); e == nil {
		if n, e := strconv.Atoi(strings.TrimSpace(string(raw))); e == nil && n >= 0 && n <= 100 {
			b.Percent = &n
		}
	}
	if raw, e := os.ReadFile(dir + "/status"); e == nil {
		b.Charging = strings.TrimSpace(string(raw)) == "Charging"
	}
	return b
}
func sameBattery(a, b Battery) bool {
	if a.Charging != b.Charging {
		return false
	}
	if a.Percent == nil || b.Percent == nil {
		return a.Percent == nil && b.Percent == nil
	}
	return *a.Percent == *b.Percent
}
func themeLedColor() string {
	f, e := os.Open(os.Getenv("BRICK_MIC_SETTINGS"))
	if e != nil {
		return "ffffff"
	}
	defer f.Close()
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if strings.HasPrefix(line, "color1=") {
			hex := strings.TrimPrefix(strings.TrimSpace(strings.TrimPrefix(line, "color1=")), "0x")
			if len(hex) == 6 || len(hex) == 8 {
				if _, e := strconv.ParseUint(hex, 16, 32); e == nil {
					return hex[:6]
				}
			}
		}
	}
	return "ffffff"
}
func (m *Mic) telemetry(ctx context.Context) {
	ticker := time.NewTicker(15 * time.Second)
	defer ticker.Stop()
	lastRead := time.Time{}
	var sent Battery
	sentToken := ""
	sentOnce := false
	for {
		m.mu.Lock()
		asleep := m.screenAsleep
		m.mu.Unlock()
		if !asleep || time.Since(lastRead) >= 60*time.Second {
			force := lastRead.IsZero()
			lastRead = time.Now()
			b := readBattery("/sys/class/power_supply/axp2202-battery")
			m.mu.Lock()
			m.state.Battery = b
			active := m.controls && m.notify && m.state.State != "recording"
			token := m.token
			m.mu.Unlock()
			if active && (!sentOnce || token != sentToken || !sameBattery(sent, b) || force) {
				raw, _ := json.Marshal(map[string]any{"op": "battery", "battery": b, "token": token})
				if m.send(6, 0, 0, raw) == nil {
					sent = b
					sentToken = token
					sentOnce = true
				}
			}
		}
		select {
		case <-ctx.Done():
			return
		case <-m.telemetryKick:
			lastRead = time.Time{}
		case receipt := <-m.notifyAcks:
			_ = m.send(6, 0, 0, receipt.payload())
		case <-ticker.C:
		}
	}
}

// Runs in the daemon, including screen-off standby. Never pauses the audio sender.
func (m *Mic) notifications(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case kind := <-m.alerts:
			for m.snapshot().State == "recording" {
				select {
				case <-ctx.Done():
					return
				case <-time.After(200 * time.Millisecond):
				}
			}
			m.mu.Lock()
			if m.state.State == "recording" {
				m.mu.Unlock()
				select {
				case m.alerts <- kind:
				default:
				}
				continue
			}
			alertCtx, stop := context.WithCancel(ctx)
			m.alertCancel = stop
			m.mu.Unlock()
			m.alert(kind, alertCtx)
			stop()
		}
	}
}
func (m *Mic) alert(kind string, ctx context.Context) {
	restore, err := startNotificationLED("/sys/class/led_anim", kind, themeLedColor(), os.ReadFile,
		func(path string, value []byte) error { return os.WriteFile(path, value, 0600) })
	if err != nil {
		log.Printf("Notification LED: %v", err)
	} else {
		defer func() {
			if err := restore(); err != nil {
				log.Printf("Restore notification LED: %v", err)
			}
		}()
	}
	motor := "/sys/class/gpio/gpio227/value"
	original, e := os.ReadFile(motor)
	if e == nil {
		defer os.WriteFile(motor, original, 0600)
	}
	pulses := 2
	if kind != "completed" {
		pulses = 1
	}
	for i := 0; i < pulses; i++ {
		if e == nil {
			_ = os.WriteFile(motor, []byte("1"), 0600)
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(80 * time.Millisecond):
		}
		if e == nil {
			_ = os.WriteFile(motor, []byte("0"), 0600)
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(120 * time.Millisecond):
		}
	}
	select {
	case <-ctx.Done():
	case <-time.After(1500 * time.Millisecond):
	}
}
