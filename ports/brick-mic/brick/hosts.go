package main

import (
	"encoding/json"
	"errors"
	"github.com/godbus/dbus/v5"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"
	"unicode"
)

// Computer identity is independent of the ephemeral BlueZ recovery record.
// An unavailable chosen computer never causes an automatic switch to another.
type Host struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}
type Hosts struct {
	Selected    string `json:"selected"`
	Active      string `json:"active"`
	Discovering bool   `json:"discovering"`
	Known       []Host `json:"known"`
}

var hostID = regexp.MustCompile(`^[A-Za-z0-9_-]{8,64}$`)

func validHost(h Host) bool {
	return hostID.MatchString(h.ID) && len(h.Name) > 0 && len(h.Name) <= 96 && !strings.ContainsFunc(h.Name, unicode.IsControl)
}
func hostsFile() string { return os.Getenv("BRICK_MIC_HOSTS") }
func (m *Mic) loadHosts() error {
	file := hostsFile()
	if file == "" {
		return nil
	}
	b, err := os.ReadFile(file)
	if os.IsNotExist(err) {
		return nil
	}
	if err != nil {
		return err
	}
	var h Hosts
	if len(b) > 4096 || json.Unmarshal(b, &h) != nil || len(h.Known) > 12 {
		return errors.New("invalid receiver configuration")
	}
	found := h.Selected == ""
	seen := map[string]bool{}
	for _, v := range h.Known {
		if !validHost(v) || seen[v.ID] {
			return errors.New("invalid receiver identity")
		}
		seen[v.ID] = true
		if v.ID == h.Selected {
			found = true
		}
	}
	if !found {
		return errors.New("unknown selected receiver")
	}
	h.Active = ""
	h.Discovering = false
	m.state.Hosts = h
	return nil
}
func (m *Mic) saveHosts() error {
	file := hostsFile()
	if file == "" {
		return nil
	}
	h := m.state.Hosts
	h.Active = ""
	h.Discovering = false
	b, err := json.Marshal(h)
	if err != nil {
		return err
	}
	if err = os.MkdirAll(filepath.Dir(file), 0700); err != nil {
		return err
	}
	f, err := os.CreateTemp(filepath.Dir(file), ".receivers-*")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
	if _, err = f.Write(b); err != nil {
		f.Close()
		return err
	}
	if err = f.Sync(); err != nil {
		f.Close()
		return err
	}
	if err = f.Close(); err != nil {
		return err
	}
	return os.Rename(f.Name(), file)
}

// Caller holds mu. Discovery registers candidates, never grants keyboard control.
func (m *Mic) acceptHost(h Host) bool {
	if !validHost(h) {
		return m.state.Hosts.Selected == "" && !m.state.Hosts.Discovering && h.ID == ""
	}
	before := m.state.Hosts
	known := false
	for _, v := range m.state.Hosts.Known {
		if v.ID == h.ID {
			known = true
		}
	}
	if !known {
		if !m.state.Hosts.Discovering && m.state.Hosts.Selected != "" {
			return false
		}
		if len(m.state.Hosts.Known) >= 12 {
			return false
		}
		m.state.Hosts.Known = append(m.state.Hosts.Known, h)
		if m.saveHosts() != nil {
			m.state.Hosts = before
			return false
		}
	}
	return !m.state.Hosts.Discovering && m.state.Hosts.Selected == h.ID
}
func (m *Mic) hostAction(command string) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.state.State == "recording" || m.state.State == "processing" {
		return errors.New("请先结束本次输入")
	}
	old := m.state.Hosts
	switch {
	case command == "hosts:discover":
		m.state.Hosts.Discovering = true
		m.discoveryUntil = time.Now().Add(30 * time.Second)
	case command == "hosts:stop":
		m.state.Hosts.Discovering = false
		m.discoveryUntil = time.Time{}
	case strings.HasPrefix(command, "hosts:choose:"):
		id := strings.TrimPrefix(command, "hosts:choose:")
		found := false
		for _, h := range m.state.Hosts.Known {
			if h.ID == id {
				found = true
			}
		}
		if !found {
			return errors.New("请选择已发现的电脑")
		}
		m.state.Hosts.Selected = id
		m.state.Hosts.Discovering = false
		m.discoveryUntil = time.Time{}
		if err := m.saveHosts(); err != nil {
			m.state.Hosts = old
			return errors.New("连接偏好保存失败")
		}
	default:
		return errors.New("unknown receiver action")
	}
	m.state.Error = ""
	peer := m.receiver
	if peer != "" && (m.state.Hosts.Discovering || m.state.Hosts.Active != m.state.Hosts.Selected) {
		// Invalidate old epoch before asynchronous Disconnect. Old result/control
		// writes are rejected until a fresh hello from the selected computer.
		m.receiver = ""
		m.token = ""
		m.controls = false
		m.remote = false
		m.state.Connected = false
		m.state.Hosts.Active = ""
		m.state.Text = ""
		m.state.Control = ControlState{Mode: "ordinary"}
		m.state.State = "disconnected"
		m.disconnectedSince = time.Now()
		go m.disconnectHost(peer)
	}
	return nil
}
func (m *Mic) disconnectHost(peer string) {
	if m.conn != nil && validReceiver(peer) {
		_ = m.conn.Object("org.bluez", dbus.ObjectPath(peer)).Call("org.bluez.Device1.Disconnect", 0).Err
	}
}
