package main

import (
	"bufio"
	"context"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"math"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/godbus/dbus/v5"
	"github.com/godbus/dbus/v5/prop"
)

const serviceUUID = "BA1C0000-7E89-4C31-A2D0-4F923CB1A100"
const audioUUID = "BA1C0001-7E89-4C31-A2D0-4F923CB1A100"
const controlUUID = "BA1C0002-7E89-4C31-A2D0-4F923CB1A100"
const root = dbus.ObjectPath("/com/nextui/brickmic")
const service = dbus.ObjectPath("/com/nextui/brickmic/service0")
const audio = dbus.ObjectPath("/com/nextui/brickmic/service0/audio")
const control = dbus.ObjectPath("/com/nextui/brickmic/service0/control")
const advert = dbus.ObjectPath("/com/nextui/brickmic/advert")
const adapter = dbus.ObjectPath("/org/bluez/hci0")

type properties = map[string]map[string]dbus.Variant
type objects = map[dbus.ObjectPath]properties
type Manager struct{ Objects objects }

func (m *Manager) GetManagedObjects() (objects, *dbus.Error) { return m.Objects, nil }

type Advertisement struct{}

func (*Advertisement) Release() *dbus.Error { return nil }

type State struct {
	Hosts     Hosts        `json:"hosts"`
	State     string       `json:"state"`
	Connected bool         `json:"connected"`
	Session   uint16       `json:"session"`
	RMS       int          `json:"rms"`
	Frames    uint32       `json:"frames"`
	Seconds   float64      `json:"seconds"`
	Text      string       `json:"text"`
	Error     string       `json:"error"`
	Packet    int          `json:"packet"`
	Control   ControlState `json:"control"`
	Battery   Battery      `json:"battery"`
}
type Mic struct {
	discoveryUntil    time.Time
	lastHostLog       string
	lastHostLogAt     time.Time
	mu                sync.Mutex
	wire              sync.Mutex
	conn              *dbus.Conn
	props             *prop.Properties
	state             State
	notify            bool
	receiver          string
	cancel            context.CancelFunc
	done              chan struct{}
	probeFile         string
	probeSecs         int
	probeOnce         sync.Once
	stoppedAt         time.Time
	serviceReady      bool
	gattRegistered    bool
	advertRegistered  bool
	legacyConfigured  bool
	acks              bool
	ackFrames         uint32
	controls          bool
	remote            bool
	token             string
	assembly          controlAssembler
	events            map[string]bool
	eventOrder        []string
	alerts            chan string
	notifyAcks        chan notificationReceipt
	screenAsleep      bool
	deepReserved      bool
	disconnectedSince time.Time
	telemetryKick     chan struct{}
	alertCancel       context.CancelFunc
}

// Retain only this app's peer across launches until reboot. Each UI launch has
// a different runtime directory, so a runtime-only record cannot recover reentry.
func receiverFile() string {
	runtime := os.Getenv("BRICK_MIC_RUNTIME")
	if !strings.HasPrefix(runtime, "/tmp/brick-mic-") {
		return ""
	}
	if os.Getenv("BRICK_MIC_RECEIVER_RECORD") == "/tmp/brick-mic-receiver" {
		return "/tmp/brick-mic-receiver"
	}
	return runtime + "/receiver"
}

func rememberReceiver(path string) error {
	file := receiverFile()
	if file == "" || !validReceiver(path) {
		return nil
	}
	// Atomic replacement avoids following an existing symlink or leaving a
	// truncated record when the process is interrupted.
	tmp, err := os.CreateTemp("/tmp", "brick-mic-peer-*")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	_, err = tmp.WriteString(path)
	closeErr := tmp.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return closeErr
	}
	return os.Rename(tmp.Name(), file)
}
func validReceiver(path string) bool {
	const prefix = "/org/bluez/hci0/dev_"
	if !strings.HasPrefix(path, prefix) {
		return false
	}
	_, err := net.ParseMAC(strings.ReplaceAll(strings.TrimPrefix(path, prefix), "_", ":"))
	return err == nil && dbus.ObjectPath(path).IsValid()
}
func resetPreviousReceiver(conn *dbus.Conn) error {
	file := receiverFile()
	if file == "" {
		return nil
	}
	info, err := os.Lstat(file)
	if err != nil || !info.Mode().IsRegular() || info.Size() > 128 {
		return nil
	}
	bytes, err := os.ReadFile(file)
	if err != nil || len(bytes) > 128 {
		return nil
	}
	path := strings.TrimSpace(string(bytes))
	if !validReceiver(path) {
		return nil
	}
	// Linux 4.9 can restart the old advertisement immediately on disconnect.
	// Disable the app's compatibility advertisement before releasing its peer.
	if tinaLegacyKernel() {
		if err := legacyAdvertisingCommand("0x000a", []byte{0}); err != nil {
			return err
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	device := conn.Object("org.bluez", dbus.ObjectPath(path))
	var connected dbus.Variant
	if err = device.CallWithContext(ctx, "org.freedesktop.DBus.Properties.Get", 0,
		"org.bluez.Device1", "Connected").Store(&connected); err != nil {
		return nil
	}
	if on, _ := connected.Value().(bool); !on {
		return nil
	}
	if err = device.CallWithContext(ctx, "org.bluez.Device1.Disconnect", 0).Err; err != nil {
		return fmt.Errorf("cannot release previous microphone connection: %w", err)
	}
	log.Print("Released previous Brick Mic receiver before restarting GATT")
	return nil
}

func (m *Mic) snapshot() State {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.state.Hosts.Discovering && time.Now().After(m.discoveryUntil) {
		m.state.Hosts.Discovering = false
	}
	s := m.state
	s.Hosts.Known = append([]Host(nil), m.state.Hosts.Known...)
	return s
}
func (m *Mic) start(probe bool) error {
	m.mu.Lock()
	if !m.state.Connected || !m.notify {
		m.mu.Unlock()
		return errors.New("请先连接 Mac 上的 Brick Mic")
	}
	if m.state.State == "service" || m.state.State == "recording" || m.state.State == "processing" {
		m.mu.Unlock()
		return errors.New("上一轮还未结束")
	}
	if m.done != nil {
		select {
		case <-m.done:
		default:
			m.mu.Unlock()
			return errors.New("上一轮正在关闭，请稍后重试")
		}
	}
	if m.alertCancel != nil {
		m.alertCancel()
		m.alertCancel = nil
	}
	ctx, cancel := context.WithCancel(context.Background())
	m.cancel = cancel
	m.done = make(chan struct{})
	done := m.done
	m.state.Session++
	if m.state.Session == 0 {
		m.state.Session = 1
	}
	sid := m.state.Session
	m.state.State = "recording"
	m.state.Text = ""
	m.state.Error = ""
	m.state.Frames = 0
	m.state.RMS = 0
	m.state.Seconds = 0
	m.stoppedAt = time.Time{}
	m.ackFrames = 0
	m.mu.Unlock()
	go m.capture(ctx, done, sid, probe)
	return nil
}
func (m *Mic) stop(cancelled bool) {
	m.mu.Lock()
	cancel := m.cancel
	if !cancelled && m.state.State == "recording" {
		m.stoppedAt = time.Now()
	}
	sid := m.state.Session
	if cancelled {
		m.state.State = "ready"
		m.state.Text = ""
		m.state.Error = ""
	}
	m.mu.Unlock()
	if cancel != nil {
		cancel()
	}
	if cancelled {
		m.send(4, sid, 0, []byte("{}"))
	}
}
func (m *Mic) send(kind byte, sid, seq uint16, data []byte) error {
	m.wire.Lock()
	defer m.wire.Unlock()
	m.mu.Lock()
	on := m.notify
	packet := m.state.Packet
	m.mu.Unlock()
	if !on {
		return errors.New("蓝牙连接已断开")
	}
	payload := packet - 9
	if payload < 11 {
		payload = 11
	}
	for offset := 0; offset < len(data); offset += payload {
		end := offset + payload
		if end > len(data) {
			end = len(data)
		}
		b := make([]byte, 9+end-offset)
		b[0] = kind
		binary.LittleEndian.PutUint16(b[1:], sid)
		binary.LittleEndian.PutUint16(b[3:], seq)
		binary.LittleEndian.PutUint16(b[5:], uint16(offset))
		binary.LittleEndian.PutUint16(b[7:], uint16(len(data)))
		copy(b[9:], data[offset:end])
		if err := m.conn.Emit(audio, "org.freedesktop.DBus.Properties.PropertiesChanged", "org.bluez.GattCharacteristic1", map[string]dbus.Variant{"Value": dbus.MakeVariant(b)}, []string{}); err != nil {
			return err
		}
	}
	return nil
}
func (m *Mic) fail(sid uint16, message string) {
	m.mu.Lock()
	if m.state.Session == sid {
		m.state.State = "error"
		m.state.Error = message
		m.state.RMS = 0
	}
	m.mu.Unlock()
	_ = m.send(5, sid, 0, []byte(message))
	log.Print(message)
}
func (m *Mic) capture(ctx context.Context, done chan struct{}, sid uint16, probe bool) {
	defer close(done)
	var source io.ReadCloser
	var cmd *exec.Cmd
	if probe && m.probeFile != "" {
		f, err := os.Open(m.probeFile)
		if err != nil {
			m.fail(sid, "无法读取测试音频")
			return
		}
		source = f
	} else {
		// plughw performs any resampling needed by this codec; outgoing PCM is always exactly 16 kHz.
		cmd = exec.CommandContext(ctx, "arecord", "-q", "-D", "plughw:0,0", "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1", "--buffer-time=100000")
		pipe, err := cmd.StdoutPipe()
		if err != nil {
			m.fail(sid, "无法打开录音管道")
			return
		}
		cmd.Stderr = os.Stderr
		if err = cmd.Start(); err != nil {
			m.fail(sid, "无法启动内置麦克风")
			return
		}
		source = pipe
	}
	defer source.Close()
	defer func() {
		if cmd != nil {
			if cmd.Process != nil {
				_ = cmd.Process.Kill()
			}
			_ = cmd.Wait()
		}
	}()
	started := time.Now()
	limit := 60 * time.Second
	if probe && m.probeSecs > 0 {
		limit = time.Duration(m.probeSecs) * time.Second
	}
	begin, _ := json.Marshal(map[string]any{"rate": 16000, "codec": "ima-adpcm", "version": 1, "probe": probe})
	if err := m.send(1, sid, 0, begin); err != nil {
		m.fail(sid, "蓝牙无法发送音频")
		return
	}
	buf := make([]byte, 640)
	frames := uint32(0)
	samples := uint32(0)
	for ctx.Err() == nil && time.Since(started) < limit {
		n, err := io.ReadFull(source, buf)
		n -= n % 2
		if n > 0 {
			var energy float64
			for i := 0; i < n; i += 2 {
				v := float64(int16(binary.LittleEndian.Uint16(buf[i:])))
				energy += v * v
			}
			if e := m.send(2, sid, uint16(frames), encodeADPCM(buf[:n])); e != nil {
				m.fail(sid, "蓝牙音频传输中断")
				return
			}
			frames++
			samples += uint32(n / 2)
			m.mu.Lock()
			m.state.RMS = int(math.Sqrt(energy / float64(n/2)))
			m.state.Frames = frames
			m.state.Seconds = float64(samples) / 16000
			backlog := m.acks && frames-m.ackFrames > 100
			m.mu.Unlock()
			// D-Bus Emit only queues a notification; use the receiver's actual
			// progress to bound backlog even when Emit itself returns immediately.
			if backlog || time.Since(started)-time.Duration(samples)*time.Second/16000 > 2*time.Second {
				m.fail(sid, "蓝牙带宽不足，请重新连接后重试")
				return
			}
			if probe && m.probeFile != "" {
				select {
				case <-ctx.Done():
				case <-time.After(time.Until(started.Add(time.Duration(samples) * time.Second / 16000))):
				}
			}
		}
		if err != nil {
			if ctx.Err() == nil && err != io.EOF && err != io.ErrUnexpectedEOF {
				m.fail(sid, "麦克风读取失败")
				return
			}
			break
		}
	}
	m.mu.Lock()
	cancelled := m.state.State == "ready" || m.state.State == "error" || !m.state.Connected
	if !cancelled {
		m.state.State = "processing"
	}
	m.state.RMS = 0
	m.cancel = nil
	m.mu.Unlock()
	select {
	case m.telemetryKick <- struct{}{}:
	default:
	}
	if !cancelled {
		m.mu.Lock()
		stoppedAt := m.stoppedAt
		m.mu.Unlock()
		releaseMS := 0.0
		if !stoppedAt.IsZero() {
			releaseMS = float64(time.Since(stoppedAt).Microseconds()) / 1000
		}
		end, _ := json.Marshal(map[string]any{"frames": frames, "samples": samples, "release_to_end_ms": releaseMS})
		_ = m.send(3, sid, uint16(frames), end)
	}
}

type Characteristic struct {
	m     *Mic
	audio bool
}

func (c *Characteristic) ReadValue(options map[string]dbus.Variant) ([]byte, *dbus.Error) {
	data := []byte("Brick Mic v1")
	offset := 0
	if v, ok := options["offset"]; ok {
		offset = int(v.Value().(uint16))
	}
	if offset > len(data) {
		return nil, dbus.NewError("org.bluez.Error.InvalidOffset", nil)
	}
	return data[offset:], nil
}
func (c *Characteristic) StartNotify() *dbus.Error {
	if !c.audio {
		return dbus.NewError("org.bluez.Error.NotSupported", nil)
	}
	c.m.mu.Lock()
	c.m.notify = true
	c.m.mu.Unlock()
	c.m.props.SetMust("org.bluez.GattCharacteristic1", "Notifying", true)
	return nil
}
func (c *Characteristic) StopNotify() *dbus.Error {
	c.m.mu.Lock()
	c.m.notify = false
	c.m.receiver = ""
	c.m.state.Hosts.Active = ""
	c.m.controls = false
	c.m.remote = false
	c.m.token = ""
	c.m.assembly = controlAssembler{}
	c.m.state.Connected = false
	c.m.disconnectedSince = time.Now()
	c.m.state.State = "disconnected"
	c.m.state.Control = ControlState{Mode: "ordinary"}
	cancel := c.m.cancel
	c.m.mu.Unlock()
	if cancel != nil {
		cancel()
	}
	c.m.props.SetMust("org.bluez.GattCharacteristic1", "Notifying", false)
	return nil
}
func (c *Characteristic) WriteValue(value []byte, options map[string]dbus.Variant) *dbus.Error {
	if c.audio || len(value) > 512 {
		return dbus.NewError("org.bluez.Error.NotSupported", nil)
	}
	var v struct {
		Negotiated   bool   `json:"negotiated"`
		ReceiverID   string `json:"receiverID"`
		ReceiverName string `json:"receiverName"`
		Op           string `json:"op"`
		Packet       int    `json:"packet"`
		Session      uint16 `json:"session"`
		Text         string `json:"text"`
		Final        bool   `json:"final"`
		Acks         bool   `json:"acks"`
		Frame        uint32 `json:"frame"`
		Version      int    `json:"version"`
		Remote       bool   `json:"remote"`
		Token        string `json:"token"`
		ID           uint32 `json:"id"`
		Part         int    `json:"p"`
		Total        int    `json:"n"`
		Data         string `json:"d"`
	}
	if json.Unmarshal(value, &v) != nil {
		return dbus.NewError("org.bluez.Error.InvalidArguments", nil)
	}
	device := ""
	if d, ok := options["device"]; ok {
		device = string(d.Value().(dbus.ObjectPath))
	}
	c.m.mu.Lock()
	defer c.m.mu.Unlock()
	if v.Op != "hello" && (c.m.receiver == "" || c.m.receiver != device) || c.m.receiver != "" && c.m.receiver != device {
		if v.Op == "hello" {
			c.m.logHostHandshake("another_receiver_active")
		}
		return dbus.NewError("org.bluez.Error.NotPermitted", nil)
	}
	switch v.Op {
	case "hello":
		if c.m.deepReserved {
			c.m.logHostHandshake("deep_sleep_pending")
			return dbus.NewError("org.bluez.Error.NotReady", nil)
		}
		if !validReceiver(device) {
			c.m.logHostHandshake("invalid_device_path")
			return dbus.NewError("org.bluez.Error.NotPermitted", nil)
		}
		if !c.m.notify {
			c.m.logHostHandshake("audio_subscription_not_ready")
			return dbus.NewError("org.bluez.Error.NotReady", nil)
		}
		if c.m.state.Hosts.Discovering && time.Now().After(c.m.discoveryUntil) {
			c.m.state.Hosts.Discovering = false
		}
		if !c.m.acceptHost(Host{ID: v.ReceiverID, Name: v.ReceiverName}) {
			go c.m.disconnectHost(device)
			return dbus.NewError("org.bluez.Error.NotPermitted", nil)
		}
		c.m.logHostHandshake("selected_receiver_accepted")
		c.m.state.Hosts.Active = v.ReceiverID
		c.m.receiver = device
		c.m.controls = v.Version == 2 && len(v.Token) == 16
		c.m.remote = c.m.controls && v.Remote
		c.m.token = v.Token
		c.m.assembly = controlAssembler{}
		if receiverFile() != "" {
			if err := rememberReceiver(device); err != nil {
				log.Print("Cannot retain microphone peer for crash recovery")
			}
		}
		c.m.state.Connected = true
		c.m.disconnectedSince = time.Time{}
		c.m.state.State = "ready"
		c.m.state.Control = ControlState{Mode: "ordinary"}
		if c.m.controls {
			c.m.state.State = "service"
		}
		c.m.state.Packet = notificationPacket(v.Packet, v.Negotiated, options)
		c.m.state.Error = ""
		c.m.acks = v.Acks
		c.m.ackFrames = 0
		select {
		case c.m.telemetryKick <- struct{}{}:
		default:
		}
		if v.Negotiated {
			token, packet := c.m.token, c.m.state.Packet
			go func() {
				b, _ := json.Marshal(map[string]any{"op": "transport", "token": token, "packet": packet})
				_ = c.m.send(6, 0, 0, b)
			}()
		}
		go c.m.probeOnce.Do(func() {
			if c.m.probeFile != "" || c.m.probeSecs > 0 {
				time.Sleep(time.Second)
				if err := c.m.start(true); err != nil {
					log.Print(err)
				}
			}
		})
	case "c":
		if !c.m.controls || v.Token != c.m.token || device == "" {
			return dbus.NewError("org.bluez.Error.NotPermitted", nil)
		}
		b, err := c.m.assembly.append(v.ID, v.Part, v.Total, v.Data)
		if err != nil {
			return dbus.NewError("org.bluez.Error.InvalidArguments", nil)
		}
		if b != nil && c.m.applyControl(b) != nil {
			return dbus.NewError("org.bluez.Error.InvalidArguments", nil)
		}
	case "ack":
		if v.Session == c.m.state.Session && v.Frame >= c.m.ackFrames && v.Frame <= c.m.state.Frames {
			c.m.ackFrames = v.Frame
		}
	case "result", "append":
		if v.Session != c.m.state.Session || c.m.state.State == "ready" {
			return nil
		}
		if v.Op == "result" {
			c.m.state.Text = ""
		}
		if len(c.m.state.Text)+len(v.Text) <= 16384 {
			c.m.state.Text += v.Text
		}
		if v.Final {
			c.m.state.State = "ready"
		}
	case "error":
		if v.Session == c.m.state.Session {
			c.m.state.State = "error"
			c.m.state.Error = v.Text
			if c.m.cancel != nil {
				c.m.cancel()
			}
		}
	default:
		return dbus.NewError("org.bluez.Error.NotSupported", nil)
	}
	return nil
}
func (m *Mic) export(path dbus.ObjectPath, values properties, methods any, iface string) (*prop.Properties, error) {
	if methods != nil {
		if err := m.conn.Export(methods, path, iface); err != nil {
			return nil, err
		}
	}
	specs := make(map[string]map[string]*prop.Prop)
	for name, vs := range values {
		specs[name] = map[string]*prop.Prop{}
		for key, v := range vs {
			specs[name][key] = &prop.Prop{Value: v.Value(), Writable: false, Emit: prop.EmitTrue}
		}
	}
	return prop.Export(m.conn, path, specs)
}
func (m *Mic) register() error {
	objects := objects{
		service: {"org.bluez.GattService1": {"UUID": dbus.MakeVariant(serviceUUID), "Primary": dbus.MakeVariant(true), "Includes": dbus.MakeVariant([]dbus.ObjectPath{})}},
		audio:   {"org.bluez.GattCharacteristic1": {"UUID": dbus.MakeVariant(audioUUID), "Service": dbus.MakeVariant(service), "Flags": dbus.MakeVariant([]string{"read", "notify"}), "Value": dbus.MakeVariant([]byte{}), "Notifying": dbus.MakeVariant(false)}},
		control: {"org.bluez.GattCharacteristic1": {"UUID": dbus.MakeVariant(controlUUID), "Service": dbus.MakeVariant(service), "Flags": dbus.MakeVariant([]string{"write"}), "Value": dbus.MakeVariant([]byte{})}},
	}
	if err := m.conn.Export(&Manager{objects}, root, "org.freedesktop.DBus.ObjectManager"); err != nil {
		return err
	}
	for path, values := range objects {
		var methods any
		iface := "org.bluez.GattService1"
		if path != service {
			methods = &Characteristic{m, path == audio}
			iface = "org.bluez.GattCharacteristic1"
		}
		props, err := m.export(path, values, methods, iface)
		if err != nil {
			return err
		}
		if path == audio {
			m.props = props
		}
	}
	adv := properties{"org.bluez.LEAdvertisement1": {"Type": dbus.MakeVariant("peripheral"), "ServiceUUIDs": dbus.MakeVariant([]string{serviceUUID}), "LocalName": dbus.MakeVariant("Brick Mic"), "Includes": dbus.MakeVariant([]string{"tx-power"}), "Discoverable": dbus.MakeVariant(true), "MinInterval": dbus.MakeVariant(uint32(100)), "MaxInterval": dbus.MakeVariant(uint32(200))}}
	if _, err := m.export(advert, adv, &Advertisement{}, "org.bluez.LEAdvertisement1"); err != nil {
		return err
	}
	obj := m.conn.Object("org.bluez", adapter)
	return m.registerEndpoints(func(method string, path dbus.ObjectPath) error {
		return obj.Call(method, 0, path, map[string]dbus.Variant{}).Err
	}, tinaLegacyKernel(), legacyAdvertising)
}

func (m *Mic) registerEndpoints(call func(string, dbus.ObjectPath) error, legacy bool, prepare func() error) error {
	if err := call("org.bluez.GattManager1.RegisterApplication", root); err != nil {
		return err
	}
	m.gattRegistered = true
	// Prepare controller data while advertising is disabled. RegisterAdvertisement
	// can make the Mac connect immediately; changing data afterwards is rejected
	// by Tina's legacy controller once it is in the LE peripheral role.
	if legacy {
		m.legacyConfigured = true
		if err := prepare(); err != nil {
			return fmt.Errorf("prepare legacy BLE advertising: %w", err)
		}
	}
	if err := call("org.bluez.LEAdvertisingManager1.RegisterAdvertisement", advert); err != nil {
		return err
	}
	m.advertRegistered = true
	return nil
}

func legacyAdvertisingCommand(op string, data []byte) error {
	var ocf uint16
	if _, err := fmt.Sscanf(op, "0x%x", &ocf); err != nil {
		return err
	}
	return controllerCommand(0x2000|ocf, data)
}

func legacyAdvertising() error {
	return prepareLegacyAdvertising(legacyAdvertisingCommand)
}

func prepareLegacyAdvertising(command func(string, []byte) error) error {
	if err := command("0x000a", []byte{0}); err != nil {
		return err
	}
	uuid, _ := hex.DecodeString(strings.ReplaceAll(serviceUUID, "-", ""))
	for i, j := 0, len(uuid)-1; i < j; i, j = i+1, j-1 {
		uuid[i], uuid[j] = uuid[j], uuid[i]
	}
	adv := append([]byte{2, 1, 6, 17, 7}, uuid...)
	scan := append([]byte{10, 9}, []byte("Brick Mic")...)
	for i, b := range [][]byte{adv, scan} {
		packet := make([]byte, 32)
		packet[0] = byte(len(b))
		copy(packet[1:], b)
		if err := command(fmt.Sprintf("0x%04x", 8+i), packet); err != nil {
			return err
		}
	}
	log.Print("Prepared Tina Linux 4.9 advertising data before enabling advertisement")
	return nil
}

// Stop publishing before disconnecting: otherwise the Mac can reconnect to
// the old controller payload before the next GATT registration on Linux 4.9.
func (m *Mic) unregister() {
	m.mu.Lock()
	peer := m.receiver
	m.mu.Unlock()
	if !validReceiver(peer) {
		if b, err := os.ReadFile(receiverFile()); err == nil && len(b) <= 128 {
			peer = strings.TrimSpace(string(b))
		}
	}
	if m.legacyConfigured {
		if err := legacyAdvertisingCommand("0x000a", []byte{0}); err != nil {
			log.Print("Cannot stop microphone compatibility advertisement")
		}
	}
	obj := m.conn.Object("org.bluez", adapter)
	call := func(object dbus.BusObject, method string, args ...any) error {
		limit := 500 * time.Millisecond
		if method == "org.bluez.Device1.Disconnect" {
			limit = 3 * time.Second
		}
		ctx, cancel := context.WithTimeout(context.Background(), limit)
		defer cancel()
		return object.CallWithContext(ctx, method, 0, args...).Err
	}
	if m.advertRegistered {
		_ = call(obj, "org.bluez.LEAdvertisingManager1.UnregisterAdvertisement", advert)
	}
	if m.legacyConfigured {
		// Removing the management instance can itself reapply controller state.
		_ = legacyAdvertisingCommand("0x000a", []byte{0})
	}
	if validReceiver(peer) {
		if err := call(m.conn.Object("org.bluez", dbus.ObjectPath(peer)), "org.bluez.Device1.Disconnect"); err == nil {
			log.Print("Released microphone receiver on exit")
		}
	}
	if m.gattRegistered {
		_ = call(obj, "org.bluez.GattManager1.UnregisterApplication", root)
	}
}
func (m *Mic) ipc(path string) error {
	if info, err := os.Lstat(path); err == nil {
		if info.Mode()&os.ModeSocket == 0 {
			return errors.New("IPC path exists and is not a socket")
		}
		if c, err := net.DialTimeout("unix", path, time.Second); err == nil {
			c.Close()
			return errors.New("Brick Mic already running")
		}
		os.Remove(path)
	}
	l, err := net.Listen("unix", path)
	if err != nil {
		return err
	}
	if err = os.Chmod(path, 0600); err != nil {
		l.Close()
		return err
	}
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			go func() {
				defer c.Close()
				_ = c.SetDeadline(time.Now().Add(2 * time.Second))
				line, _ := bufio.NewReader(io.LimitReader(c, 128)).ReadString('\n')
				command := strings.TrimSpace(line)
				if strings.HasPrefix(command, "control:") {
					if err := m.controlAction(command); err != nil {
						fmt.Fprintln(c, err)
						return
					}
				}
				if strings.HasPrefix(command, "hosts:") {
					if err := m.hostAction(command); err != nil {
						fmt.Fprintln(c, err)
						return
					}
				}
				switch command {
				case "ping":
					m.mu.Lock()
					ready := m.serviceReady
					m.mu.Unlock()
					if ready {
						fmt.Fprintln(c, "ready")
					} else {
						fmt.Fprintln(c, "starting")
					}
					return
				case "start":
					if err := m.start(false); err != nil {
						fmt.Fprintln(c, err)
						return
					}
				case "stop":
					m.stop(false)
				case "cancel":
					m.stop(true)
				case "sleep":
					m.stop(true)
					m.mu.Lock()
					m.screenAsleep = true
					m.mu.Unlock()
				case "wake":
					m.mu.Lock()
					m.screenAsleep = false
					m.deepReserved = false
					m.mu.Unlock()
					select {
					case m.telemetryKick <- struct{}{}:
					default:
					}
				case "prepare-deep-sleep":
					if m.reserveDeepSleep() {
						fmt.Fprintln(c, "asleep-ready")
					} else {
						fmt.Fprintln(c, "blocked")
					}
					return
				case "abort-deep-sleep":
					m.mu.Lock()
					m.deepReserved = false
					m.mu.Unlock()
				case "quit":
					_ = syscall.Kill(os.Getpid(), syscall.SIGTERM)
				}
				data, _ := json.Marshal(m.snapshot())
				fmt.Fprintln(c, string(data))
			}()
		}
	}()
	return nil
}
func main() {
	socket := flag.String("socket", "/tmp/brick-mic.sock", "local control socket")
	probeFile := flag.String("probe-file", "", "raw 16kHz S16LE test audio; never enabled by the normal launcher")
	probeSecs := flag.Int("probe-seconds", 0, "capture once after connecting, diagnostic mode only")
	ctl := flag.String("ctl", "", "send start/stop/cancel/status to an existing daemon")
	flag.Parse()
	if *ctl != "" {
		c, err := net.Dial("unix", *socket)
		if err != nil {
			log.Fatal(err)
		}
		defer c.Close()
		_ = c.SetDeadline(time.Now().Add(2 * time.Second))
		fmt.Fprintln(c, *ctl)
		io.Copy(os.Stdout, c)
		return
	}
	conn, err := dbus.ConnectSystemBus()
	if err != nil {
		log.Fatal(err)
	}
	defer conn.Close()
	// Catch exit requests during registration too, so partial startup reaches
	// normal teardown rather than leaving the controller linked to the Mac.
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGTERM, syscall.SIGINT)
	defer signal.Stop(sig)
	restoreLink := func() {}
	if runtime := os.Getenv("BRICK_MIC_RUNTIME"); strings.HasPrefix(runtime, "/tmp/brick-mic-") {
		var linkErr error
		restoreLink, linkErr = audioLinkPreference("/sys/kernel/debug/bluetooth/hci0", runtime)
		if linkErr != nil {
			log.Print("Audio interval preference unavailable; using system defaults")
		} else {
			log.Print("Requested 15ms BLE audio interval; original preferences retained")
		}
	}
	defer restoreLink()
	if err = resetPreviousReceiver(conn); err != nil {
		restoreLink()
		log.Fatal(err)
	}
	m := &Mic{disconnectedSince: time.Now(), conn: conn, state: State{State: "disconnected", Packet: 20}, probeFile: *probeFile, probeSecs: *probeSecs, alerts: make(chan string, 8), notifyAcks: make(chan notificationReceipt, 64), telemetryKick: make(chan struct{}, 1)}
	if err = m.loadHosts(); err != nil {
		log.Fatal("Receiver preferences: ", err)
	}
	if err = m.ipc(*socket); err != nil {
		restoreLink()
		log.Fatal(err)
	}
	defer os.Remove(*socket)
	defer m.unregister()
	if err = m.register(); err != nil {
		m.unregister()
		restoreLink()
		log.Fatal("Bluetooth service: ", err)
	}
	m.mu.Lock()
	m.serviceReady = true
	m.state.Control.Mode = "ordinary"
	m.mu.Unlock()
	ctx, cancelBackground := context.WithCancel(context.Background())
	var background sync.WaitGroup
	background.Add(2)
	defer func() { cancelBackground(); background.Wait() }()
	go func() { defer background.Done(); m.telemetry(ctx) }()
	go func() { defer background.Done(); m.notifications(ctx) }()
	log.Print("Brick Mic BLE service ready; microphone stays off until A is pressed")
	<-sig
	m.stop(true)
	m.mu.Lock()
	done := m.done
	m.mu.Unlock()
	if done != nil {
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	}
}
