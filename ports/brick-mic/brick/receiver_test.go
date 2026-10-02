package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestReceiverRecordSurvivesRuntimeChange(t *testing.T) {
	t.Setenv("BRICK_MIC_RECEIVER_RECORD", "/tmp/brick-mic-receiver")
	t.Setenv("BRICK_MIC_RUNTIME", "/tmp/brick-mic-first-123")
	first := receiverFile()
	t.Setenv("BRICK_MIC_RUNTIME", "/tmp/brick-mic-second-456")
	if first != receiverFile() || first != "/tmp/brick-mic-receiver" {
		t.Fatal("reentry cannot find previous receiver")
	}
	t.Setenv("BRICK_MIC_RUNTIME", "/tmp/unrelated")
	if receiverFile() != "" {
		t.Fatal("receiver record enabled outside microphone runtime")
	}
}

func TestRememberReceiverIsPrivateAndReplacesSymlink(t *testing.T) {
	dir, err := os.MkdirTemp("/tmp", "brick-mic-test-")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(dir)
	t.Setenv("BRICK_MIC_RUNTIME", dir)
	t.Setenv("BRICK_MIC_RECEIVER_RECORD", "")
	victim := filepath.Join(dir, "untouched")
	if err := os.WriteFile(victim, []byte("original"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(victim, receiverFile()); err != nil {
		t.Fatal(err)
	}
	peer := "/org/bluez/hci0/dev_01_02_03_04_05_06"
	if err := rememberReceiver(peer); err != nil {
		t.Fatal(err)
	}
	info, err := os.Lstat(receiverFile())
	if err != nil || !info.Mode().IsRegular() || info.Mode().Perm() != 0600 {
		t.Fatal("nonprivate peer file", err)
	}
	b, _ := os.ReadFile(victim)
	if string(b) != "original" {
		t.Fatal("receiver write followed a symlink")
	}
	b, _ = os.ReadFile(receiverFile())
	if string(b) != peer {
		t.Fatal("wrong retained receiver")
	}
	if err := rememberReceiver("/org/bluez/hci1/dev_01_02_03_04_05_06"); err != nil {
		t.Fatal(err)
	}
	b, _ = os.ReadFile(receiverFile())
	if string(b) != peer {
		t.Fatal("unrelated adapter overwrote receiver")
	}
}
