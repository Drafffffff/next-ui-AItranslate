package main

import (
	"context"
	"fmt"
	"github.com/godbus/dbus/v5"
	"time"
)

func centralRaw(_ context.Context, _ *dbus.Conn, _ string, _, _ int, _ time.Duration) error {
	return fmt.Errorf("raw ATT transport is supported on Brick Linux only")
}
