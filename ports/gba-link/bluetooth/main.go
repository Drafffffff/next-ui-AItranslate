// Standalone BLE diagnostic. Owns only its GATT application and advertisement.
package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"sort"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/godbus/dbus/v5"
	"github.com/godbus/dbus/v5/prop"
)

const serviceUUID = "ba1c0010-7e89-4c31-a2d0-4f923cb1a100"
const txUUID = "ba1c0011-7e89-4c31-a2d0-4f923cb1a100"
const rxUUID = "ba1c0012-7e89-4c31-a2d0-4f923cb1a100"
const app dbus.ObjectPath = "/com/nextui/gbalink_probe"
const service dbus.ObjectPath = app + "/service"
const tx dbus.ObjectPath = service + "/tx"
const rx dbus.ObjectPath = service + "/rx"
const advert dbus.ObjectPath = app + "/advert"
const adapter dbus.ObjectPath = "/org/bluez/hci0"
const charInterface = "org.bluez.GattCharacteristic1"

type properties = map[string]map[string]dbus.Variant
type objects = map[dbus.ObjectPath]properties

type Manager struct{ Objects objects }

func (m *Manager) GetManagedObjects() (objects, *dbus.Error) { return m.Objects, nil }

type Advertisement struct{}

func (*Advertisement) Release() *dbus.Error { return nil }

type Peripheral struct {
	conn      *dbus.Conn
	mu        sync.Mutex
	notifying bool
	peer      dbus.ObjectPath
	packets   uint64
	mtu       uint16
}
type Characteristic struct {
	Peripheral *Peripheral
	TX         bool
}

func (c *Characteristic) StartNotify() *dbus.Error {
	if !c.TX {
		return dbus.NewError("org.bluez.Error.NotSupported", nil)
	}
	c.Peripheral.mu.Lock()
	c.Peripheral.notifying = true
	c.Peripheral.mu.Unlock()
	fmt.Println("SUBSCRIBED")
	return nil
}
func (c *Characteristic) StopNotify() *dbus.Error {
	c.Peripheral.mu.Lock()
	c.Peripheral.notifying = false
	c.Peripheral.peer = ""
	c.Peripheral.mu.Unlock()
	fmt.Println("UNSUBSCRIBED")
	return nil
}
func (c *Characteristic) WriteValue(value []byte, options map[string]dbus.Variant) *dbus.Error {
	if c.TX {
		return dbus.NewError("org.bluez.Error.NotSupported", nil)
	}
	device, ok := options["device"].Value().(dbus.ObjectPath)
	mtu, mtuOK := options["mtu"].Value().(uint16)
	if !ok || !mtuOK || mtu < 23 || len(value) > int(mtu)-3 || len(value) < 8 {
		return dbus.NewError("org.bluez.Error.InvalidValueLength", nil)
	}
	p := c.Peripheral
	p.mu.Lock()
	defer p.mu.Unlock()
	if !p.notifying || (p.peer != "" && p.peer != device) {
		return dbus.NewError("org.bluez.Error.NotPermitted", nil)
	}
	if p.peer == "" {
		p.peer = device
		p.mtu = mtu
		fmt.Printf("READY mtu=%d peer=%s\n", mtu, device)
	}
	// Echo exact fragments; errors terminate the test rather than claiming delivery.
	if err := p.conn.Emit(tx, "org.freedesktop.DBus.Properties.PropertiesChanged", charInterface, map[string]dbus.Variant{"Value": dbus.MakeVariant(value)}, []string{}); err != nil {
		return dbus.NewError("org.bluez.Error.Failed", []any{err.Error()})
	}
	p.packets++
	return nil
}
func export(conn *dbus.Conn, path dbus.ObjectPath, values properties, methods any, iface string) error {
	if methods != nil {
		if err := conn.Export(methods, path, iface); err != nil {
			return err
		}
	}
	specs := map[string]map[string]*prop.Prop{}
	for iface, vs := range values {
		specs[iface] = map[string]*prop.Prop{}
		for k, v := range vs {
			specs[iface][k] = &prop.Prop{Value: v.Value(), Emit: prop.EmitTrue}
		}
	}
	_, err := prop.Export(conn, path, specs)
	return err
}
func call(ctx context.Context, conn *dbus.Conn, path dbus.ObjectPath, method string, args ...any) *dbus.Call {
	timeout, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	return conn.Object("org.bluez", path).CallWithContext(timeout, method, 0, args...)
}
func managed(ctx context.Context, conn *dbus.Conn) (objects, error) {
	var result objects
	err := call(ctx, conn, "/", "org.freedesktop.DBus.ObjectManager.GetManagedObjects").Store(&result)
	return result, err
}
func host(ctx context.Context, conn *dbus.Conn) error {
	p := &Peripheral{conn: conn}
	objs := objects{
		service: {"org.bluez.GattService1": {"UUID": dbus.MakeVariant(serviceUUID), "Primary": dbus.MakeVariant(true), "Includes": dbus.MakeVariant([]dbus.ObjectPath{})}},
		tx:      {charInterface: {"UUID": dbus.MakeVariant(txUUID), "Service": dbus.MakeVariant(service), "Flags": dbus.MakeVariant([]string{"notify"}), "Value": dbus.MakeVariant([]byte{}), "Notifying": dbus.MakeVariant(false)}},
		rx:      {charInterface: {"UUID": dbus.MakeVariant(rxUUID), "Service": dbus.MakeVariant(service), "Flags": dbus.MakeVariant([]string{"write", "write-without-response"})}},
	}
	if err := conn.Export(&Manager{objs}, app, "org.freedesktop.DBus.ObjectManager"); err != nil {
		return err
	}
	for path, vs := range objs {
		var methods any
		iface := "org.bluez.GattService1"
		if path != service {
			methods = &Characteristic{p, path == tx}
			iface = charInterface
		}
		if err := export(conn, path, vs, methods, iface); err != nil {
			return err
		}
	}
	adv := properties{"org.bluez.LEAdvertisement1": {"Type": dbus.MakeVariant("peripheral"), "ServiceUUIDs": dbus.MakeVariant([]string{serviceUUID}), "LocalName": dbus.MakeVariant("GBA Link"), "Discoverable": dbus.MakeVariant(true)}}
	if err := export(conn, advert, adv, &Advertisement{}, "org.bluez.LEAdvertisement1"); err != nil {
		return err
	}
	if err := call(ctx, conn, adapter, "org.bluez.GattManager1.RegisterApplication", app, map[string]dbus.Variant{}).Err; err != nil {
		return fmt.Errorf("register GATT: %w", err)
	}
	defer call(context.Background(), conn, adapter, "org.bluez.GattManager1.UnregisterApplication", app)
	if err := prepareLegacy(ctx, conn); err != nil {
		return err
	}
	if err := call(ctx, conn, adapter, "org.bluez.LEAdvertisingManager1.RegisterAdvertisement", advert, map[string]dbus.Variant{}).Err; err != nil {
		return fmt.Errorf("advertise: %w", err)
	}
	defer call(context.Background(), conn, adapter, "org.bluez.LEAdvertisingManager1.UnregisterAdvertisement", advert)
	fmt.Println("ROOM_READY")
	<-ctx.Done()
	p.mu.Lock()
	defer p.mu.Unlock()
	fmt.Printf("HOST_SUMMARY fragments=%d mtu=%d\n", p.packets, p.mtu)
	return nil
}
func findRoom(ctx context.Context, conn *dbus.Conn, expected string) (dbus.ObjectPath, error) {
	filter := map[string]dbus.Variant{"Transport": dbus.MakeVariant("le"), "UUIDs": dbus.MakeVariant([]string{serviceUUID})}
	if err := call(ctx, conn, adapter, "org.bluez.Adapter1.SetDiscoveryFilter", filter).Err; err != nil {
		return "", err
	}
	if err := call(ctx, conn, adapter, "org.bluez.Adapter1.StartDiscovery").Err; err != nil {
		return "", err
	}
	defer call(context.Background(), conn, adapter, "org.bluez.Adapter1.StopDiscovery")
	for ctx.Err() == nil {
		objs, err := managed(ctx, conn)
		if err != nil {
			return "", err
		}
		for path, ifaces := range objs {
			d, ok := ifaces["org.bluez.Device1"]
			if !ok {
				continue
			}
			address, _ := d["Address"].Value().(string)
			uuids, _ := d["UUIDs"].Value().([]string)
			if expected != "" && !strings.EqualFold(address, expected) {
				continue
			}
			for _, uuid := range uuids {
				if strings.EqualFold(uuid, serviceUUID) {
					fmt.Printf("FOUND address=%s path=%s\n", address, path)
					return path, nil
				}
			}
		}
		select {
		case <-ctx.Done():
		case <-time.After(200 * time.Millisecond):
		}
	}
	return "", fmt.Errorf("room discovery: %w", ctx.Err())
}
func central(ctx context.Context, conn *dbus.Conn, expected string, count, length int, delay time.Duration) error {
	device, err := findRoom(ctx, conn, expected)
	if err != nil {
		return err
	}
	// Do not take ownership of or disconnect an already connected device.
	objs, err := managed(ctx, conn)
	if err != nil {
		return err
	}
	if connected, _ := objs[device]["org.bluez.Device1"]["Connected"].Value().(bool); connected {
		return fmt.Errorf("peer already connected; probe will not reuse/disconnect another connection")
	}
	// A cancelled D-Bus Connect can leave BlueZ's operation alive. Cancel only
	// this newly owned peer attempt on every path, including timeout.
	defer call(context.Background(), conn, device, "org.bluez.Device1.Disconnect")
	if err := call(ctx, conn, device, "org.bluez.Device1.Connect").Err; err != nil {
		return err
	}
	var txPath, rxPath dbus.ObjectPath
	for ctx.Err() == nil {
		objs, err = managed(ctx, conn)
		if err != nil {
			return err
		}
		for path, ifs := range objs {
			if !strings.HasPrefix(string(path), string(device)+"/") {
				continue
			}
			v := ifs[charInterface]
			uuid, _ := v["UUID"].Value().(string)
			switch strings.ToLower(uuid) {
			case txUUID:
				txPath = path
			case rxUUID:
				rxPath = path
			}
		}
		if txPath != "" && rxPath != "" {
			break
		}
		select {
		case <-ctx.Done():
		case <-time.After(100 * time.Millisecond):
		}
	}
	if txPath == "" || rxPath == "" {
		return fmt.Errorf("GATT discovery timeout")
	}
	// Acquire APIs return the actual exchanged MTU and avoid per-packet D-Bus.
	var notifyFD, writeFD dbus.UnixFD
	var notifyMTU, writeMTU uint16
	if err := call(ctx, conn, txPath, charInterface+".AcquireNotify", map[string]dbus.Variant{}).Store(&notifyFD, &notifyMTU); err != nil {
		return fmt.Errorf("acquire notify: %w", err)
	}
	reader := os.NewFile(uintptr(notifyFD), "BLE notify")
	defer reader.Close()
	if err := call(ctx, conn, rxPath, charInterface+".AcquireWrite", map[string]dbus.Variant{}).Store(&writeFD, &writeMTU); err != nil {
		return fmt.Errorf("acquire write: %w", err)
	}
	writer := os.NewFile(uintptr(writeFD), "BLE write")
	defer writer.Close()
	ioDone := make(chan struct{})
	defer close(ioDone)
	go func() {
		select {
		case <-ctx.Done():
			reader.Close()
			writer.Close()
		case <-ioDone:
		}
	}()
	payload := int(notifyMTU) - 3
	if n := int(writeMTU) - 3; n < payload {
		payload = n
	}
	if payload < 8 {
		return fmt.Errorf("invalid negotiated MTU")
	}
	fmt.Printf("READY notify_mtu=%d write_mtu=%d payload=%d\n", notifyMTU, writeMTU, payload)
	samples := make([]int64, 0, count)
	response := make([]byte, 65536)
	fragments := 0
	for i := 0; i < count; i++ {
		data := make([]byte, length)
		for j := range data {
			data[j] = byte(i + j)
		}
		start := time.Now()
		// Bounded stop-and-wait per fragment. Sequence and offset detect corruption,
		// loss, duplication or stale notifications; not a production stream yet.
		for offset := 0; offset < length; {
			n := length - offset
			if n > payload-8 {
				n = payload - 8
			}
			packet := make([]byte, n+8)
			binary.BigEndian.PutUint32(packet, uint32(i))
			binary.BigEndian.PutUint32(packet[4:], uint32(offset))
			copy(packet[8:], data[offset:offset+n])
			if err := writer.SetWriteDeadline(time.Now().Add(5 * time.Second)); err != nil {
				return err
			}
			if n, err := writer.Write(packet); err != nil || n != len(packet) {
				return fmt.Errorf("write fragment: %d %v", n, err)
			}
			if err := reader.SetReadDeadline(time.Now().Add(5 * time.Second)); err != nil {
				return err
			}
			nread, err := reader.Read(response)
			if err != nil {
				return fmt.Errorf("notify: %w", err)
			}
			if !bytes.Equal(packet, response[:nread]) {
				return fmt.Errorf("fragment integrity failed exchange=%d offset=%d", i, offset)
			}
			offset += n
			fragments++
		}
		samples = append(samples, time.Since(start).Microseconds())
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(delay):
		}
	}
	sort.Slice(samples, func(i, j int) bool { return samples[i] < samples[j] })
	fmt.Printf("PASS exchanges=%d fragments=%d bytes_each_way=%d p50=%d p95=%d p99=%d max=%d\n", count, fragments, count*length, samples[count/2], samples[count*95/100], samples[count*99/100], samples[count-1])
	return nil
}
func main() {
	role := flag.String("role", "host", "host or join")
	peer := flag.String("peer", "", "optional expected room MAC")
	count := flag.Int("count", 100, "exchanges")
	length := flag.Int("bytes", 104, "payload bytes")
	delay := flag.Duration("delay", 0, "delay after each reply")
	duration := flag.Duration("duration", 2*time.Minute, "total timeout")
	flag.Parse()
	if (*role != "host" && *role != "join") || *count < 10 || *count > 100000 || *length < 8 || *length > 65536 || *delay < 0 {
		fmt.Fprintln(os.Stderr, "invalid arguments")
		os.Exit(2)
	}
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer cancel()
	ctx, timeout := context.WithTimeout(ctx, *duration)
	defer timeout()
	conn, err := dbus.ConnectSystemBus()
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer conn.Close()
	if *role == "host" {
		err = host(ctx, conn)
	} else {
		err = central(ctx, conn, *peer, *count, *length, *delay)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
