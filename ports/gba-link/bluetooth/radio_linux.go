package main

import (
	"context"
	"encoding/binary"
	"sync"

	"golang.org/x/sys/unix"
)

// Read-only observation: never changes scan parameters, powers, filters in the
// controller, or another application's BlueZ discovery session.
func advertisementObserver(ctx context.Context) (<-chan radioRoom, func(), error) {
	fd, err := unix.Socket(unix.AF_BLUETOOTH, unix.SOCK_RAW|unix.SOCK_CLOEXEC|unix.SOCK_NONBLOCK, unix.BTPROTO_HCI)
	if err != nil {
		return nil, nil, err
	}
	if err := unix.Bind(fd, &unix.SockaddrHCI{Dev: 0, Channel: 0}); err != nil {
		unix.Close(fd)
		return nil, nil, err
	}
	filter := make([]byte, 16)
	binary.LittleEndian.PutUint32(filter, 1<<4)
	binary.LittleEndian.PutUint32(filter[8:], 1<<(0x3e-32))
	if err := unix.SetsockoptString(fd, 0, 2, string(filter)); err != nil {
		unix.Close(fd)
		return nil, nil, err
	}
	updates := make(chan radioRoom, 64)
	stop, finished := make(chan struct{}), make(chan struct{})
	go func() {
		defer close(finished)
		defer close(updates)
		buf := make([]byte, 260)
		for {
			select {
			case <-ctx.Done():
				return
			case <-stop:
				return
			default:
			}
			poll := []unix.PollFd{{Fd: int32(fd), Events: unix.POLLIN}}
			if _, err := unix.Poll(poll, 100); err != nil {
				if err == unix.EINTR {
					continue
				}
				return
			}
			if poll[0].Revents&(unix.POLLERR|unix.POLLHUP|unix.POLLNVAL) != 0 {
				return
			}
			if poll[0].Revents&unix.POLLIN == 0 {
				continue
			}
			n, err := unix.Read(fd, buf)
			if err == unix.EAGAIN || err == unix.EINTR {
				continue
			}
			if err != nil {
				return
			}
			for _, room := range advertisementRooms(buf[:n]) {
				select {
				case updates <- room:
				default:
				}
			}
		}
	}()
	var once sync.Once
	cleanup := func() { once.Do(func() { close(stop); <-finished; unix.Close(fd) }) }
	return updates, cleanup, nil
}
