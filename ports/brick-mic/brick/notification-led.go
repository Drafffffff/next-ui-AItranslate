package main

import (
	"errors"
	"fmt"
	"path/filepath"
)

// effect_m is a start trigger: parameters must precede it, also when restoring.
// max_scale controls the top bar; F1/F2 battery indicators have a separate scale.
func startNotificationLED(root, kind, color string, read func(string) ([]byte, error), write func(string, []byte) error) (func() error, error) {
	names := []string{"max_scale", "effect_cycles_m", "effect_duration_m", "effect_rgb_hex_m", "effect_m"}
	saved := make(map[string][]byte, len(names))
	for _, name := range names {
		value, err := read(filepath.Join(root, name))
		if err != nil {
			return nil, fmt.Errorf("read %s: %w", name, err)
		}
		saved[name] = value
	}
	restore := func() error {
		var errs []error
		for _, name := range names {
			if err := write(filepath.Join(root, name), saved[name]); err != nil {
				errs = append(errs, fmt.Errorf("restore %s: %w", name, err))
			}
		}
		return errors.Join(errs...)
	}
	cycles, duration := "3", "180"
	if kind == "waiting" {
		color, duration = "ffb547", "450"
	} else if kind == "failed" {
		color, cycles = "ff554d", "2"
	}
	// Brick's driver accepts at most 60; 40 is visible without maximum intensity.
	values := []string{"40", cycles, duration, color, "5"}
	for i, name := range names {
		if err := write(filepath.Join(root, name), []byte(values[i]+"\n")); err != nil {
			return nil, errors.Join(fmt.Errorf("write %s: %w", name, err), restore())
		}
	}
	return restore, nil
}
