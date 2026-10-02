package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestAudioIntervalOwnership(t *testing.T) {
	dir, runtime := t.TempDir(), t.TempDir()
	for name, value := range map[string]string{"conn_min_interval": "40\n", "conn_max_interval": "56\n"} {
		if err := os.WriteFile(filepath.Join(dir, name), []byte(value), 0600); err != nil {
			t.Fatal(err)
		}
	}
	check := func(name, expected string) {
		t.Helper()
		b, err := os.ReadFile(filepath.Join(dir, name))
		if err != nil || strings.TrimSpace(string(b)) != expected {
			t.Fatalf("%s=%q: %v", name, b, err)
		}
	}
	_, err := audioLinkPreference(dir, runtime)
	if err != nil {
		t.Fatal(err)
	}
	check("conn_min_interval", "12")
	check("conn_max_interval", "12")
	// Restart after a crash must preserve the pre-app preferences, rather than
	// treating the app's own 15ms values as the originals.
	restore, err := audioLinkPreference(dir, runtime)
	if err != nil {
		t.Fatal(err)
	}
	restore()
	check("conn_min_interval", "40")
	check("conn_max_interval", "56")
	if _, err := os.Stat(filepath.Join(runtime, "link-intervals")); !os.IsNotExist(err) {
		t.Fatal("backup was not consumed")
	}
	// Hosts without Tina debugfs must degrade safely without creating a backup.
	if _, err := audioLinkPreference(filepath.Join(dir, "missing"), runtime); err == nil {
		t.Fatal("missing controller accepted")
	}
}
