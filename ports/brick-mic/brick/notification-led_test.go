package main

import (
	"bytes"
	"context"
	"errors"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"
)

func TestNotificationLEDTriggerOrderAndRestore(t *testing.T) {
	for _, kind := range []string{"completed", "waiting", "failed"} {
		t.Run(kind, func(t *testing.T) {
			state := map[string][]byte{"max_scale": []byte("0\n"), "effect_cycles_m": []byte("-1\n"), "effect_duration_m": []byte("1000\n"), "effect_rgb_hex_m": []byte("110011 \n"), "effect_m": []byte("4\n")}
			original := make(map[string][]byte)
			for k, v := range state {
				original[k] = bytes.Clone(v)
			}
			var writes []string
			write := func(path string, value []byte) error {
				name := filepath.Base(path)
				writes = append(writes, name)
				state[name] = bytes.Clone(value)
				if name == "effect_m" && strings.TrimSpace(string(value)) == "5" {
					color, cycles, duration := "abc123", "3", "180"
					if kind == "waiting" {
						color, duration = "ffb547", "450"
					}
					if kind == "failed" {
						color, cycles = "ff554d", "2"
					}
					for n, want := range map[string]string{"max_scale": "40", "effect_rgb_hex_m": color, "effect_cycles_m": cycles, "effect_duration_m": duration} {
						if strings.TrimSpace(string(state[n])) != want {
							t.Fatalf("trigger latched stale %s: %s", n, state[n])
						}
					}
				}
				return nil
			}
			restore, err := startNotificationLED("/led", kind, "abc123", func(path string) ([]byte, error) { return state[filepath.Base(path)], nil }, write)
			if err != nil {
				t.Fatal(err)
			}
			if err := restore(); err != nil {
				t.Fatal(err)
			}
			order := []string{"max_scale", "effect_cycles_m", "effect_duration_m", "effect_rgb_hex_m", "effect_m"}
			if !reflect.DeepEqual(writes, append(append([]string{}, order...), order...)) {
				t.Fatal("trigger was not last", writes)
			}
			if !reflect.DeepEqual(state, original) {
				t.Fatal("original LED settings were not restored", state)
			}
		})
	}
}

func TestNotificationLEDWriteFailureRollsBack(t *testing.T) {
	state := map[string][]byte{}
	for _, name := range []string{"max_scale", "effect_cycles_m", "effect_duration_m", "effect_rgb_hex_m", "effect_m"} {
		state[name] = []byte("original-" + name)
	}
	failed := false
	_, err := startNotificationLED("/led", "completed", "ffffff", func(path string) ([]byte, error) { return bytes.Clone(state[filepath.Base(path)]), nil }, func(path string, value []byte) error {
		name := filepath.Base(path)
		if name == "effect_duration_m" && !failed {
			failed = true
			return errors.New("driver rejected value")
		}
		state[name] = bytes.Clone(value)
		return nil
	})
	if err == nil {
		t.Fatal("write failure was swallowed")
	}
	for name, value := range state {
		if string(value) != "original-"+name {
			t.Fatal("partial LED configuration leaked", name)
		}
	}
}

// Opt-in only: exercises the actual notification routine on an idle Brick.
func TestNotificationHardwareRestore(t *testing.T) {
	if os.Getenv("BRICK_MIC_HARDWARE_TEST") != "1" {
		t.Skip("requires idle Brick hardware")
	}
	paths := []string{"/sys/class/gpio/gpio227/value"}
	for _, name := range []string{"max_scale", "effect_cycles_m", "effect_duration_m", "effect_rgb_hex_m", "effect_m", "max_scale_f1f2", "effect_f1", "effect_f2"} {
		paths = append(paths, "/sys/class/led_anim/"+name)
	}
	saved := map[string][]byte{}
	for _, path := range paths {
		b, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		saved[path] = b
	}
	for _, cancel := range []bool{false, true} {
		ctx, stop := context.WithCancel(context.Background())
		if cancel {
			stop()
		}
		done := make(chan struct{})
		go func() { (&Mic{}).alert("completed", ctx); close(done) }()
		if !cancel {
			active := false
			for i := 0; i < 20; i++ {
				b, _ := os.ReadFile("/sys/class/led_anim/max_scale")
				if strings.TrimSpace(string(b)) == "40" {
					active = true
					break
				}
				time.Sleep(5 * time.Millisecond)
			}
			if !active {
				<-done
				t.Fatal("notification brightness not applied")
			}
			var first []byte
			changed := false
			for i := 0; i < 12; i++ {
				b, err := os.ReadFile("/sys/class/led_anim/frame")
				if err != nil {
					<-done
					t.Fatal(err)
				}
				if i == 0 {
					first = b
				} else if !bytes.Equal(first, b) {
					changed = true
				}
				time.Sleep(40 * time.Millisecond)
			}
			if !changed {
				<-done
				t.Fatal("LED frames did not animate")
			}
			t.Log("notification brightness applied and LED frames animate")
		}
		<-done
		stop()
		for _, path := range paths {
			b, err := os.ReadFile(path)
			if err != nil || !bytes.Equal(b, saved[path]) {
				t.Fatalf("hardware not restored: %s", path)
			}
		}
	}
}
