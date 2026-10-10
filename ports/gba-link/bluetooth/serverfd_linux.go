package main

import (
	"fmt"
	"os"

	"github.com/godbus/dbus/v5"
	"golang.org/x/sys/unix"
)

func (c *Characteristic) acquireFD(options map[string]dbus.Variant, notify bool) (dbus.UnixFD, uint16, *dbus.Error) {
	if !probeServerFD || c.TX != notify {
		return 0, 0, dbus.NewError("org.bluez.Error.NotSupported", nil)
	}
	device, ok := options["device"].Value().(dbus.ObjectPath)
	mtu, mtuOK := options["mtu"].Value().(uint16)
	if !ok || !mtuOK || mtu < 23 || mtu > 517 {
		return 0, 0, dbus.NewError("org.bluez.Error.InvalidArguments", nil)
	}
	p := c.Peripheral
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.peer != "" && p.peer != device {
		return 0, 0, dbus.NewError("org.bluez.Error.NotPermitted", nil)
	}
	if (notify && p.notifyWriter != nil) || (!notify && p.writeAcquired) {
		return 0, 0, dbus.NewError("org.bluez.Error.NotPermitted", nil)
	}
	fds, err := unix.Socketpair(unix.AF_UNIX, unix.SOCK_SEQPACKET|unix.SOCK_NONBLOCK|unix.SOCK_CLOEXEC, 0)
	if err != nil {
		return 0, 0, dbus.NewError("org.bluez.Error.Failed", []any{err.Error()})
	}
	local, remote := os.NewFile(uintptr(fds[0]), "server local"), os.NewFile(uintptr(fds[1]), "BlueZ exported FD")
	p.files = append(p.files, local, remote)
	p.peer = device
	p.mtu = mtu
	if notify {
		p.notifyWriter = local
		p.notifying = true
	} else {
		p.writeAcquired = true
		go func() {
			buffer := make([]byte, 513)
			for {
				n, err := local.Read(buffer)
				if err != nil {
					return
				}
				if n == 0 || n > int(mtu)-3 || n > 512 {
					fmt.Println("SERVER_FD_ERROR invalid write length")
					return
				}
				if err := c.WriteValue(buffer[:n], options); err != nil {
					fmt.Println("SERVER_FD_ERROR", err)
					return
				}
			}
		}()
	}
	fmt.Printf("SERVER_FD_ACQUIRED notify=%t mtu=%d\n", notify, mtu)
	return dbus.UnixFD(fds[1]), mtu, nil
}
func (c *Characteristic) AcquireNotify(options map[string]dbus.Variant) (dbus.UnixFD, uint16, *dbus.Error) {
	return c.acquireFD(options, true)
}
func (c *Characteristic) AcquireWrite(options map[string]dbus.Variant) (dbus.UnixFD, uint16, *dbus.Error) {
	return c.acquireFD(options, false)
}
