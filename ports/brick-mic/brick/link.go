package main

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Tina Linux 4.9 requests the adapter's preferred interval on incoming LE
// connections. Its 50–70ms defaults cause audio to queue. Keep a 15ms preference
// only while this application owns the service, and retain originals across
// daemon crashes so the native host can also restore them.
func audioLinkPreference(directory, runtime string) (func(), error) {
	noop := func() {}
	read := func(name string) (int, error) {
		b, err := os.ReadFile(filepath.Join(directory, name))
		if err != nil {
			return 0, err
		}
		v, err := strconv.Atoi(strings.TrimSpace(string(b)))
		if err != nil || v < 6 || v > 3200 {
			return 0, errors.New("invalid BLE interval")
		}
		return v, nil
	}
	min, err := read("conn_min_interval")
	if err != nil {
		return noop, err
	}
	max, err := read("conn_max_interval")
	if err != nil {
		return noop, err
	}
	backup := filepath.Join(runtime, "link-intervals")
	if b, err := os.ReadFile(backup); err == nil {
		if _, err = fmt.Sscanf(string(b), "%d %d", &min, &max); err != nil || min < 6 || max < min || max > 3200 {
			return noop, errors.New("invalid saved BLE intervals")
		}
	} else if !os.IsNotExist(err) {
		return noop, err
	} else if err = os.WriteFile(backup, []byte(fmt.Sprintf("%d %d\n", min, max)), 0600); err != nil {
		return noop, err
	}
	write := func(name string, value int) error {
		return os.WriteFile(filepath.Join(directory, name), []byte(fmt.Sprintf("%d\n", value)), 0600)
	}
	restore := func() {
		// Widen the maximum first so min <= max throughout restoration.
		a := write("conn_max_interval", max)
		b := write("conn_min_interval", min)
		if (a == nil && b == nil) || (os.IsNotExist(a) && os.IsNotExist(b)) {
			_ = os.Remove(backup)
		}
	}
	if err = write("conn_min_interval", 12); err == nil {
		err = write("conn_max_interval", 12)
	}
	if err != nil {
		restore()
		return noop, err
	}
	return restore, nil
}
