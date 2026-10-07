package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/godbus/dbus/v5"
	"golang.org/x/sys/unix"
)

type workerControl struct {
	file *os.File
	mu   sync.Mutex
}

func (c *workerControl) rooms(rooms []roomCandidate) error {
	if len(rooms) > 8 {
		return fmt.Errorf("too many rooms")
	}
	packet := make([]byte, 2+83*len(rooms))
	packet[0] = 'R'
	packet[1] = byte(len(rooms))
	for i, room := range rooms {
		if len(room.address) != 17 || len(room.label) > 63 {
			return fmt.Errorf("invalid room record")
		}
		offset := 2 + i*83
		copy(packet[offset:offset+18], room.address)
		copy(packet[offset+18:offset+82], room.label)
		packet[offset+82] = byte(room.rssi)
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if err := c.file.SetWriteDeadline(time.Now().Add(time.Second)); err != nil {
		return err
	}
	n, err := c.file.Write(packet)
	if err == nil && n != len(packet) {
		err = fmt.Errorf("short room-list write")
	}
	return err
}

func (c *workerControl) send(kind byte, text string) error {
	if len(text) > 159 {
		text = text[:159]
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if err := c.file.SetWriteDeadline(time.Now().Add(time.Second)); err != nil {
		return err
	}
	packet := append([]byte{kind}, []byte(text)...)
	n, err := c.file.Write(packet)
	if err == nil && n != len(packet) {
		err = fmt.Errorf("short frontend control write")
	}
	return err
}

func inheritedSocket(fd, kind int, name string) (*os.File, error) {
	if fd < 3 || fd > 1024 {
		return nil, fmt.Errorf("invalid inherited %s FD", name)
	}
	typ, err := unix.GetsockoptInt(fd, unix.SOL_SOCKET, unix.SO_TYPE)
	if err != nil || typ != kind {
		return nil, fmt.Errorf("invalid inherited %s socket", name)
	}
	if err := unix.SetNonblock(fd, true); err != nil {
		return nil, err
	}
	return os.NewFile(uintptr(fd), name), nil
}

func waitingRoomText(address string) string {
	if !validRoomAddress(address) {
		return "Waiting for another Brick..."
	}
	clean := strings.ReplaceAll(strings.ToUpper(address), ":", "")
	return "GBA Link " + clean[len(clean)-4:] + "\nWaiting for another Brick..."
}

// Every session is a separate child with private inherited sockets. The
// frontend can cancel/reap it without touching bluetoothd, other users or
// their connections. No local TCP listener or filesystem socket is exposed.
func serveFrontend(ctx context.Context, conn *dbus.Conn, role, peer string, dataFD, controlFD int) error {
	if dataFD == controlFD {
		return fmt.Errorf("frontend FDs must be distinct")
	}
	stream, err := inheritedSocket(dataFD, unix.SOCK_STREAM, "frontend data")
	if err != nil {
		return err
	}
	defer stream.Close()
	controlFile, err := inheritedSocket(controlFD, unix.SOCK_DGRAM, "frontend control")
	if err != nil {
		return err
	}
	defer controlFile.Close()
	control := &workerControl{file: controlFile}
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	watchDone := make(chan struct{})
	go func() {
		defer close(watchDone)
		ticker := time.NewTicker(time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				stream.Close()
				return
			case <-ticker.C:
				if err := control.send('K', ""); err != nil {
					cancel()
					stream.Close()
					return
				}
			}
		}
	}()
	defer func() { cancel(); stream.Close(); <-watchDone }()
	devices, radioErr := managed(ctx, conn)
	if radioErr != nil {
		control.send('E', "Bluetooth service unavailable. Try again.")
		return radioErr
	}
	powered, present := devices[adapter]["org.bluez.Adapter1"]["Powered"]
	enabled, _ := powered.Value().(bool)
	if !present || !enabled {
		control.send('E', "Turn on Bluetooth, then try again.")
		return fmt.Errorf("Bluetooth adapter not powered")
	}
	address, _ := devices[adapter]["org.bluez.Adapter1"]["Address"].Value().(string)
	ready := func() error { return control.send('H', "Checking game compatibility...") }
	if role == "serve-scan" {
		if err = control.send('S', "Searching nearby rooms..."); err == nil {
			search, stop := context.WithTimeout(ctx, 12*time.Second)
			defer stop()
			_, err = discoverRooms(search, conn, "", control.rooms)
		}
	} else if role == "serve-host" {
		err = peripheralSession(ctx, conn, func() error { return control.send('W', waitingRoomText(address)) }, func(ctx context.Context, endpoint duplexEndpoint, payload int, epoch uint32) error {
			return bridgeSession(ctx, endpoint, payload, epoch, true, stream, ready)
		})
	} else {
		if err = control.send('C', "Connecting to room..."); err == nil {
			err = centralSelectedSession(ctx, conn, peer, control.send, func(ctx context.Context, endpoint duplexEndpoint, payload int) error {
				epoch, err := randomDuplexEpoch()
				if err != nil {
					return err
				}
				return bridgeSession(ctx, endpoint, payload, epoch, false, stream, ready)
			})
		}
	}
	if err != nil && ctx.Err() == nil {
		message := "Bluetooth link ended. Try again."
		if errors.Is(err, errTransportVersion) {
			message = "Update GBA Link on both consoles, then reopen the game."
		} else if role == "serve-scan" {
			if errors.Is(err, context.DeadlineExceeded) {
				message = "No nearby rooms. Create a room on the other console."
			} else {
				message = "Bluetooth search failed. Try again."
			}
		} else if role == "serve-join" && errors.Is(err, context.DeadlineExceeded) {
			message = "Connection timed out. Search again."
		}
		control.send('E', message)
	}
	return err
}
