// Standalone BLE diagnostic. Owns only its GATT application and advertisement.
package main

import (
	"bytes"
	"context"
	"crypto/rand"
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
	conn          *dbus.Conn
	mu            sync.Mutex
	notifying     bool
	peer          dbus.ObjectPath
	packets       uint64
	mtu           uint16
	assembly      *assembly
	files         []*os.File
	notifyWriter  *os.File
	writeAcquired bool
	duplex        *peripheralPackets
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
	c.Peripheral.assembly = nil
	if c.Peripheral.duplex != nil {
		c.Peripheral.duplex.Close()
	}
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
	if p.duplex != nil {
		if err := p.duplex.accept(value); err != nil {
			return dbus.NewError("org.bluez.Error.Failed", []any{err.Error()})
		}
		return nil
	}
	if len(value) <= frameHeader {
		return dbus.NewError("org.bluez.Error.InvalidValueLength", nil)
	}
	if p.assembly == nil {
		p.assembly = newAssembly(binary.BigEndian.Uint32(value[4:]))
	}
	expectedSequence := p.assembly.next
	message, ack, err := p.assembly.accept(value)
	if err != nil {
		fmt.Printf("FRAME_REJECT expected=%d received=%d\n", expectedSequence, binary.BigEndian.Uint32(value[8:]))
		return dbus.NewError("org.bluez.Error.Failed", []any{err.Error()})
	}
	// Echo only validated fragments. The extra completion ACK is emitted only
	// after the whole message passes bounded assembly and CRC verification.
	if err := p.sendNotification(value); err != nil {
		return dbus.NewError("org.bluez.Error.Failed", []any{err.Error()})
	}
	if ack != nil {
		if err := p.sendNotification(ack); err != nil {
			return dbus.NewError("org.bluez.Error.Failed", []any{err.Error()})
		}
	}
	if message != nil {
		p.packets++
	}
	return nil
}

// Called with the peripheral mutex held. The optional FD backend is a
// single-session measurement path; all returned FD copies are owned until
// probe teardown and must not be reused as a production reconnect lifecycle.
func (p *Peripheral) sendNotification(value []byte) error {
	if p.notifyWriter != nil {
		p.notifyWriter.SetWriteDeadline(time.Now().Add(time.Second))
		n, err := p.notifyWriter.Write(value)
		if err != nil {
			return err
		}
		if n != len(value) {
			return fmt.Errorf("short notification write")
		}
		return nil
	}
	return p.conn.Emit(tx, "org.freedesktop.DBus.Properties.PropertiesChanged", charInterface, map[string]dbus.Variant{"Value": dbus.MakeVariant(value)}, []string{})
}

func (p *Peripheral) closeFDs() {
	p.mu.Lock()
	defer p.mu.Unlock()
	for _, file := range p.files {
		file.Close()
	}
	p.files = nil
	p.notifyWriter = nil
	p.notifying = false
	p.assembly = nil
	p.peer = ""
	if p.duplex != nil {
		p.duplex.Close()
	}
}

var probeServerFD bool

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
	return hostPeripheral(ctx, conn, p)
}

func hostPeripheral(ctx context.Context, conn *dbus.Conn, p *Peripheral) error {
	defer p.closeFDs()
	objs := objects{
		service: {"org.bluez.GattService1": {"UUID": dbus.MakeVariant(serviceUUID), "Primary": dbus.MakeVariant(true), "Includes": dbus.MakeVariant([]dbus.ObjectPath{})}},
		tx:      {charInterface: {"UUID": dbus.MakeVariant(txUUID), "Service": dbus.MakeVariant(service), "Flags": dbus.MakeVariant([]string{"notify"}), "Value": dbus.MakeVariant([]byte{}), "Notifying": dbus.MakeVariant(false)}},
		rx:      {charInterface: {"UUID": dbus.MakeVariant(rxUUID), "Service": dbus.MakeVariant(service), "Flags": dbus.MakeVariant([]string{"write", "write-without-response"})}},
	}
	if probeServerFD {
		objs[tx][charInterface]["NotifyAcquired"] = dbus.MakeVariant(false)
		objs[rx][charInterface]["WriteAcquired"] = dbus.MakeVariant(false)
	}
	if err := conn.Export(&Manager{objs}, app, "org.freedesktop.DBus.ObjectManager"); err != nil {
		return err
	}
	for path, vs := range objs {
		var methods any
		iface := "org.bluez.GattService1"
		if path != service {
			methods = &Characteristic{Peripheral: p, TX: path == tx}
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
	// Close local FDs before potentially slow D-Bus unregister calls.
	defer p.closeFDs()
	fmt.Println("ROOM_READY")
	<-ctx.Done()
	p.mu.Lock()
	defer p.mu.Unlock()
	fmt.Printf("HOST_SUMMARY messages=%d mtu=%d\n", p.packets, p.mtu)
	return nil
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
	fmt.Println("DEVICE_CONNECT_RETURNED")
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
		state := objs[device]["org.bluez.Device1"]
		return fmt.Errorf("GATT discovery timeout (connected=%v services_resolved=%v)", state["Connected"].Value(), state["ServicesResolved"].Value())
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
	if payload > 512 {
		payload = 512
	}
	if payload <= frameHeader {
		return fmt.Errorf("invalid negotiated MTU")
	}
	address, _ := objs[device]["org.bluez.Device1"]["Address"].Value().(string)
	if err := requestFastInterval(ctx, address); err != nil {
		return fmt.Errorf("owned link pacing: %w", err)
	}
	fmt.Printf("READY notify_mtu=%d write_mtu=%d payload=%d\n", notifyMTU, writeMTU, payload)
	return probeDataExchange(ctx, reader, writer, payload, count, length, delay)
}

type packetEndpoint interface {
	Read([]byte) (int, error)
	Write([]byte) (int, error)
	SetReadDeadline(time.Time) error
	SetWriteDeadline(time.Time) error
}

var probeRunDuration time.Duration
var probeEventLength uint16

func probeDataExchange(ctx context.Context, reader, writer packetEndpoint, payload, count, length int, delay time.Duration) error {
	if probeWindow > 1 {
		return probeWindowExchange(ctx, reader, writer, payload, count, length, delay)
	}
	testStart := time.Now()
	samples := make([]int64, 0, count)
	response := make([]byte, 65536)
	fragments := 0
	var nonce [4]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return err
	}
	epoch := binary.BigEndian.Uint32(nonce[:])
	if epoch == 0 {
		epoch = 1
	}
	echo := newAssembly(epoch)
	for i := 0; i < count; i++ {
		data := make([]byte, length)
		for j := range data {
			data[j] = byte(i + j)
		}
		start := time.Now()
		packets, err := fragmentMessage(payload, epoch, uint32(i+1), data)
		if err != nil {
			return err
		}
		var complete []byte
		// Diagnostic baseline: wait for each echo. Production queue pacing is
		// selected after hardware measurements, not inferred from this probe.
		for _, packet := range packets {
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
				return fmt.Errorf("fragment echo mismatch exchange=%d", i)
			}
			complete, _, err = echo.accept(response[:nread])
			if err != nil {
				return err
			}
			fragments++
		}
		if !bytes.Equal(complete, data) {
			return fmt.Errorf("whole-message integrity failed")
		}
		nread, err := reader.Read(response)
		if err != nil {
			return fmt.Errorf("completion ACK: %w", err)
		}
		if !validAck(response[:nread], epoch, uint32(i+1)) {
			return fmt.Errorf("invalid completion ACK")
		}

		samples = append(samples, time.Since(start).Microseconds())
		if probeRunDuration > 0 && time.Since(testStart) >= probeRunDuration {
			break
		}
		if probeRunDuration > 0 && len(samples)%1000 == 0 {
			fmt.Printf("PROGRESS exchanges=%d elapsed_ms=%d\n", len(samples), time.Since(testStart).Milliseconds())
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(delay):
		}
	}
	if probeRunDuration > 0 && time.Since(testStart) < probeRunDuration {
		return fmt.Errorf("exchange limit reached before requested measurement duration")
	}
	count = len(samples)
	sort.Slice(samples, func(i, j int) bool { return samples[i] < samples[j] })
	fmt.Printf("PASS exchanges=%d fragments=%d bytes_each_way=%d elapsed_ms=%d p50=%d p95=%d p99=%d max=%d\n", count, fragments, count*length, time.Since(testStart).Milliseconds(), samples[count/2], samples[count*95/100], samples[count*99/100], samples[count-1])
	return nil
}

func main() {
	window := flag.Int("window", 1, "maximum pending diagnostic messages, 1..16")
	eventLength := flag.Uint("event-length", 0, "informational event length hint in 625us units, 0..12")
	serverFD := flag.Bool("server-fd", false, "single-session server FD measurement backend")
	runFor := flag.Duration("run-for", 0, "minimum continuous measurement duration; count is the safety limit")
	role := flag.String("role", "host", "host or join")
	peer := flag.String("peer", "", "optional expected room MAC")
	count := flag.Int("count", 100, "exchanges")
	length := flag.Int("bytes", 104, "payload bytes")
	delay := flag.Duration("delay", 0, "delay after each reply")
	duration := flag.Duration("duration", 2*time.Minute, "total timeout")
	flag.Parse()
	if *window < 1 || *window > 16 {
		fmt.Fprintln(os.Stderr, "invalid diagnostic window")
		os.Exit(2)
	}
	probeWindow = *window
	if *eventLength > 12 {
		fmt.Fprintln(os.Stderr, "event length exceeds requested connection interval")
		os.Exit(2)
	}
	probeEventLength = uint16(*eventLength)
	if *runFor < 0 || *runFor > 20*time.Minute {
		fmt.Fprintln(os.Stderr, "invalid measurement duration")
		os.Exit(2)
	}
	probeRunDuration = *runFor
	probeServerFD = *serverFD
	if (*role != "host" && *role != "join" && *role != "join-raw" && *role != "duplex-host" && *role != "duplex-join") || *count < 10 || *count > 100000 || *length < 8 || *length > 65536 || *delay < 0 {
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
	if *role == "duplex-host" {
		err = duplexHost(ctx, conn, *count, *length)
	} else if *role == "duplex-join" {
		probeDuplex = true
		err = centralRaw(ctx, conn, *peer, *count, *length, *delay)
	} else if *role == "host" {
		err = host(ctx, conn)
	} else if *role == "join-raw" {
		err = centralRaw(ctx, conn, *peer, *count, *length, *delay)
	} else {
		err = central(ctx, conn, *peer, *count, *length, *delay)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
