package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestChosenHostNeverFallsBack(t *testing.T) {
	t.Setenv("BRICK_MIC_HOSTS", filepath.Join(t.TempDir(), "receivers.json"))
	m := &Mic{}
	a := Host{ID: "mac-host-123", Name: "Mac mini"}
	b := Host{ID: "linux-host-123", Name: "bazzite"}
	if m.acceptHost(a) || m.acceptHost(b) {
		t.Fatal("first pairing granted control without handheld selection")
	}
	if err := m.hostAction("hosts:choose:" + a.ID); err != nil {
		t.Fatal(err)
	}
	if !m.acceptHost(a) || m.acceptHost(b) {
		t.Fatal("another receiver stole selected pairing")
	}
	if err := m.hostAction("hosts:discover"); err != nil {
		t.Fatal(err)
	}
	if m.acceptHost(b) || m.acceptHost(a) {
		t.Fatal("discovery granted control")
	}
	if err := m.hostAction("hosts:choose:" + b.ID); err != nil {
		t.Fatal(err)
	}
	if m.acceptHost(a) || !m.acceptHost(b) || m.acceptHost(Host{}) {
		t.Fatal("selection did not exclude other/legacy receivers")
	}
	loaded := &Mic{}
	if err := loaded.loadHosts(); err != nil {
		t.Fatal(err)
	}
	if loaded.state.Hosts.Selected != b.ID || len(loaded.state.Hosts.Known) != 2 || loaded.state.Hosts.Active != "" {
		t.Fatal("preferences did not survive restart")
	}
	if m.hostAction("hosts:choose:unknown-host") == nil {
		t.Fatal("unknown computer selected")
	}
}
func TestDiscoveryBoundedAndBusySelectionBlocked(t *testing.T) {
	t.Setenv("BRICK_MIC_HOSTS", filepath.Join(t.TempDir(), "receivers.json"))
	m := &Mic{state: State{State: "recording"}}
	if m.hostAction("hosts:discover") == nil {
		t.Fatal("disrupted live recording")
	}
	m.state.State = "ready"
	m.state.Hosts.Discovering = true
	m.discoveryUntil = time.Now().Add(-time.Second)
	if m.snapshot().Hosts.Discovering {
		t.Fatal("discovery never timed out")
	}
	if m.acceptHost(Host{ID: "invalid", Name: "x\nspoof"}) {
		t.Fatal("invalid display identity accepted")
	}
	file := hostsFile()
	if err := os.WriteFile(file, []byte(`{"selected":"unknown","known":[]}`), 0600); err != nil {
		t.Fatal(err)
	}
	if m.loadHosts() == nil {
		t.Fatal("corrupt preferences caused open pairing")
	}
}

func TestLastComputerChoiceSurvivesBothDirectionsAndForeignAttempts(t *testing.T) {
	t.Setenv("BRICK_MIC_HOSTS", filepath.Join(t.TempDir(), "receivers.json"))
	mac := Host{ID: "mac-host-123", Name: "Mac mini"}
	linux := Host{ID: "linux-host-123", Name: "bazzite"}
	m := &Mic{}
	m.acceptHost(mac)
	m.acceptHost(linux)
	for _, chosen := range []Host{linux, mac, linux, mac} {
		if err := m.hostAction("hosts:choose:" + chosen.ID); err != nil {
			t.Fatal(err)
		}
		restarted := &Mic{}
		if err := restarted.loadHosts(); err != nil {
			t.Fatal(err)
		}
		other := mac
		if chosen.ID == mac.ID {
			other = linux
		}
		if restarted.state.Hosts.Selected != chosen.ID || restarted.acceptHost(other) || !restarted.acceptHost(chosen) {
			t.Fatal("another computer overwrote the last choice after restart")
		}
		// An absent selected computer must not silently replace the saved target.
		fresh := &Mic{}
		if err := fresh.loadHosts(); err != nil || fresh.state.Hosts.Selected != chosen.ID {
			t.Fatal("foreign connection attempt changed the saved preference")
		}
		m = restarted
	}
}
