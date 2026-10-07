package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"golang.org/x/sys/unix"
)

const hotspotRoot = "/tmp/nextui-gba-hotspot"
const hotspotMarker = "nextui-gba-hotspot"

type hotspotNetwork struct {
	ID      int    `json:"id"`
	SSID    string `json:"ssid"`
	Enabled bool   `json:"enabled"`
}
type hotspotLease struct {
	Switched  bool             `json:"switched"`
	Role      string           `json:"role"`
	Network   int              `json:"network"`
	Original  int              `json:"original"`
	Networks  []hotspotNetwork `json:"networks"`
	Reconnect bool             `json:"reconnect"`
	Power     string           `json:"power"`
	AP        bool             `json:"ap"`
	Address   bool             `json:"address"`
}

type hotspotLog struct {
	sync.Mutex
	buffer bytes.Buffer
}

func (l *hotspotLog) Write(data []byte) (int, error) {
	l.Lock()
	defer l.Unlock()
	if l.buffer.Len() < 65536 {
		end := len(data)
		if end > 65536-l.buffer.Len() {
			end = 65536 - l.buffer.Len()
		}
		l.buffer.Write(data[:end])
	}
	return len(data), nil
}
func (l *hotspotLog) text() string { l.Lock(); defer l.Unlock(); return l.buffer.String() }

// Commands are fixed argv, never shell strings. Long-running hostapd is our
// child only; the system supplicant and DHCP client are never stopped.
func hotspotCommand(ctx context.Context, name string, args ...string) (string, error) {
	return hotspotCommandLimit(ctx, 1500*time.Millisecond, name, args...)
}

var hotspotExecute = executeHotspotCommand

func hotspotCommandLimit(ctx context.Context, limit time.Duration, name string, args ...string) (string, error) {
	return hotspotExecute(ctx, limit, name, args...)
}

func executeHotspotCommand(ctx context.Context, limit time.Duration, name string, args ...string) (string, error) {
	limited, cancel := context.WithTimeout(ctx, limit)
	defer cancel()
	cmd := exec.CommandContext(limited, name, args...)
	cmd.WaitDelay = 100 * time.Millisecond
	output, err := cmd.CombinedOutput()
	if err != nil {
		if limited.Err() != nil {
			err = limited.Err()
		}
		return "", fmt.Errorf("%s failed: %w", name, err)
	}
	if len(output) > 262144 {
		return "", fmt.Errorf("oversized network command response")
	}
	return strings.TrimSpace(string(output)), nil
}
func hotspotWPA(ctx context.Context, args ...string) (string, error) {
	return hotspotCommand(ctx, "wpa_cli", append([]string{"-p", "/etc/wifi/sockets", "-i", "wlan0"}, args...)...)
}
func hotspotOK(ctx context.Context, args ...string) error {
	s, err := hotspotWPA(ctx, args...)
	if err != nil {
		return err
	}
	if s != "OK" {
		return fmt.Errorf("supplicant refused %s", args[0])
	}
	return nil
}
func hotspotFields(text string) map[string]string {
	out := map[string]string{}
	for _, line := range strings.Split(text, "\n") {
		p := strings.SplitN(line, "=", 2)
		if len(p) == 2 {
			out[p[0]] = p[1]
		}
	}
	return out
}
func hotspotWait(ctx context.Context, d time.Duration) error {
	timer := time.NewTimer(d)
	defer timer.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-timer.C:
		return nil
	}
}
func hotspotWrite(name string, value any) error {
	raw, err := json.Marshal(value)
	if err != nil {
		return err
	}
	f, err := os.CreateTemp(hotspotRoot, ".state-")
	if err != nil {
		return err
	}
	path := f.Name()
	defer os.Remove(path)
	if _, err = f.Write(raw); err == nil {
		err = f.Sync()
	}
	if closeErr := f.Close(); err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	return os.Rename(path, filepath.Join(hotspotRoot, name))
}
func (lease *hotspotLease) persist() error { return hotspotWrite("lease.json", lease) }
func hotspotOwnNetwork(ctx context.Context, id int) bool {
	if id < 0 {
		return false
	}
	marker, err := hotspotWPA(ctx, "get_network", strconv.Itoa(id), "id_str")
	return err == nil && marker == strconv.Quote(hotspotMarker)
}

// Recovery is restricted to recorded resources and our marked transient entry.
// No save_config/reconfigure, global process kill, IP flush or adapter reset.
func (lease *hotspotLease) restore() error { return lease.restoreAt(hotspotRoot) }
func (lease *hotspotLease) restoreAt(root string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	var errs []error
	command := func(name string, args ...string) {
		_, err := hotspotCommand(ctx, name, args...)
		if err != nil {
			errs = append(errs, err)
		}
	}
	if lease.Address {
		iface, ip := "wlan0", hotspotClientIP
		if lease.Role == "hotspot-host" {
			iface, ip = "wlan1", hotspotHostIP
		}
		// An absent address is already restored; delete no other address/route.
		current, err := hotspotCommand(ctx, "ip", "-4", "-o", "addr", "show", "dev", iface)
		if err != nil {
			errs = append(errs, err)
		} else if strings.Contains(current, "inet "+ip+"/30 ") {
			command("ip", "addr", "del", ip+"/30", "dev", iface)
		}
	}
	if lease.AP {
		command("ip", "link", "set", "wlan1", "down")
	}
	status, _ := hotspotWPA(ctx, "status")
	fields := hotspotFields(status)
	current, _ := strconv.Atoi(fields["id"])
	owned := lease.Network >= 0 && hotspotOwnNetwork(ctx, lease.Network)
	originalMatches := false
	for _, network := range lease.Networks {
		if network.ID == lease.Original {
			ssid, err := hotspotWPA(ctx, "get_network", strconv.Itoa(network.ID), "ssid")
			originalMatches = err == nil && ssid == network.SSID
		}
	}
	takeover := fields["id"] != "" && !(owned && current == lease.Network) && !(originalMatches && current == lease.Original)
	if owned {
		if err := hotspotOK(ctx, "remove_network", strconv.Itoa(lease.Network)); err != nil {
			errs = append(errs, err)
		}
	}
	if lease.Switched && !takeover {
		if lease.Original >= 0 {
			for _, network := range lease.Networks {
				if network.ID != lease.Original {
					continue
				}
				ssid, err := hotspotWPA(ctx, "get_network", strconv.Itoa(network.ID), "ssid")
				if err == nil && ssid == network.SSID {
					if err = hotspotOK(ctx, "select_network", strconv.Itoa(network.ID)); err != nil {
						errs = append(errs, err)
					}
				}
			}
		} else if lease.Reconnect {
			if err := hotspotOK(ctx, "reconnect"); err != nil {
				errs = append(errs, err)
			}
		}
		// SELECT_NETWORK disables other entries. Restore their original enabled
		// flags, but only if the original ID still denotes the same saved SSID.
		for _, network := range lease.Networks {
			if !network.Enabled || network.ID == lease.Original {
				continue
			}
			ssid, err := hotspotWPA(ctx, "get_network", strconv.Itoa(network.ID), "ssid")
			if err == nil && ssid == network.SSID {
				if err = hotspotOK(ctx, "enable_network", strconv.Itoa(network.ID)); err != nil {
					errs = append(errs, err)
				}
			}
		}
	}
	if lease.Switched && !takeover && !lease.Reconnect {
		if err := hotspotOK(ctx, "disconnect"); err != nil {
			errs = append(errs, err)
		}
	}
	if lease.Power == "on" {
		command("iw", "dev", "wlan0", "set", "power_save", "on")
	}
	if len(errs) > 0 {
		return errors.Join(errs...)
	}
	return os.Remove(filepath.Join(root, "lease.json"))
}

func acquireHotspot(ctx context.Context) (*os.File, error) {
	if err := os.Mkdir(hotspotRoot, 0700); err != nil && !os.IsExist(err) {
		return nil, err
	}
	info, err := os.Lstat(hotspotRoot)
	if err != nil || !info.IsDir() || info.Mode()&os.ModeSymlink != 0 || info.Mode().Perm()&0077 != 0 {
		return nil, fmt.Errorf("unsafe hotspot state directory")
	}
	if stat, ok := info.Sys().(*syscall.Stat_t); !ok || stat.Uid != uint32(os.Getuid()) {
		return nil, fmt.Errorf("foreign hotspot state directory")
	}
	// O_NOFOLLOW also protects the lock from a stale or foreign symlink.
	fd, err := unix.Open(filepath.Join(hotspotRoot, "lock"), unix.O_CREAT|unix.O_RDWR|unix.O_NOFOLLOW, 0600)
	if err != nil {
		return nil, err
	}
	lock := os.NewFile(uintptr(fd), "hotspot lease")
	deadline := time.Now().Add(1500 * time.Millisecond)
	for {
		err = unix.Flock(fd, unix.LOCK_EX|unix.LOCK_NB)
		if err == nil {
			break
		}
		if !errors.Is(err, unix.EWOULDBLOCK) || time.Now().After(deadline) {
			lock.Close()
			return nil, fmt.Errorf("previous room is still closing")
		}
		if err = hotspotWait(ctx, 50*time.Millisecond); err != nil {
			lock.Close()
			return nil, err
		}
	}

	if raw, readErr := os.ReadFile(filepath.Join(hotspotRoot, "lease.json")); readErr == nil {
		var lease hotspotLease
		if len(raw) > 16384 || json.Unmarshal(raw, &lease) != nil || (lease.Role != "hotspot-host" && lease.Role != "hotspot-join") || lease.Network < -1 || lease.Original < -1 || len(lease.Networks) > 64 {
			lock.Close()
			return nil, fmt.Errorf("invalid hotspot recovery record")
		}
		if err = lease.restore(); err != nil {
			lock.Close()
			return nil, fmt.Errorf("previous Wi-Fi restoration failed: %w", err)
		}
	} else if !os.IsNotExist(readErr) {
		lock.Close()
		return nil, readErr
	}
	return lock, nil
}

func hotspotSnapshot(ctx context.Context, role string) (*hotspotLease, error) {
	status, err := hotspotWPA(ctx, "status")
	if err != nil {
		return nil, err
	}
	fields := hotspotFields(status)
	if fields["wpa_state"] == "" {
		return nil, fmt.Errorf("turn on Wi-Fi first")
	}
	lease := &hotspotLease{Role: role, Network: -1, Original: -1, Reconnect: fields["wpa_state"] != "DISCONNECTED"}
	if id, err := strconv.Atoi(fields["id"]); err == nil {
		lease.Original = id
	}
	power, err := hotspotCommand(ctx, "iw", "dev", "wlan0", "get", "power_save")
	if err != nil {
		return nil, err
	}
	lease.Power = strings.TrimSpace(strings.TrimPrefix(power, "Power save:"))
	if lease.Power != "on" && lease.Power != "off" {
		return nil, fmt.Errorf("cannot preserve Wi-Fi power setting")
	}
	networks, err := hotspotWPA(ctx, "list_networks")
	if err != nil {
		return nil, err
	}
	for _, line := range strings.Split(networks, "\n") {
		columns := strings.Split(line, "\t")
		if len(columns) != 4 {
			continue
		}
		id, err := strconv.Atoi(columns[0])
		if err != nil {
			continue
		}
		ssid, err := hotspotWPA(ctx, "get_network", strconv.Itoa(id), "ssid")
		if err != nil {
			return nil, err
		}
		if len(lease.Networks) >= 64 {
			return nil, fmt.Errorf("too many Wi-Fi profiles to preserve")
		}
		lease.Networks = append(lease.Networks, hotspotNetwork{id, ssid, !strings.Contains(columns[3], "[DISABLED]")})
	}
	addresses, err := hotspotCommand(ctx, "ip", "-4", "-o", "addr", "show")
	if err != nil {
		return nil, err
	}
	_, reserved, _ := net.ParseCIDR(hotspotHostIP + "/30")
	for _, line := range strings.Split(addresses, "\n") {
		words := strings.Fields(line)
		for i, word := range words {
			if word == "inet" && i+1 < len(words) {
				ip, network, err := net.ParseCIDR(words[i+1])
				if err == nil && (network.Contains(net.ParseIP(hotspotHostIP)) || reserved.Contains(ip)) {
					return nil, fmt.Errorf("hotspot subnet is already in use")
				}
			}
		}
	}
	return lease, lease.persist()
}

func scanHotspots(ctx context.Context) ([]hotspotRoom, error) {
	deadline, cancel := context.WithTimeout(ctx, 12*time.Second)
	defer cancel()
	for {
		// Kernel scan gets fresh results. Retry only bounded contention with the
		// existing supplicant; a cancelled UI never launches another scan.
		scan, err := hotspotCommandLimit(deadline, 6*time.Second, "iw", "dev", "wlan0", "scan")
		if err == nil {
			rooms := parseHotspotScan(scan)
			if len(rooms) > 0 {
				return rooms, nil
			}
		}
		if err = hotspotWait(deadline, 300*time.Millisecond); err != nil {
			return nil, err
		}
	}
}
func cachedHotspot(peer string) (hotspotRoom, error) {
	var cached struct {
		Created int64         `json:"created"`
		Rooms   []hotspotRoom `json:"rooms"`
	}
	raw, err := os.ReadFile(filepath.Join(hotspotRoot, "rooms.json"))
	if err != nil {
		return hotspotRoom{}, err
	}
	if len(raw) > 16384 || json.Unmarshal(raw, &cached) != nil || time.Now().Unix()-cached.Created > 45 || cached.Created > time.Now().Unix()+1 {
		return hotspotRoom{}, fmt.Errorf("room search expired; search again")
	}
	for _, room := range cached.Rooms {
		expected, err := hotspotSSID(room.Address)
		if err == nil && strings.EqualFold(peer, room.Address) && room.SSID == expected && room.Frequency >= 2412 && room.Frequency <= 2462 && (room.Frequency-2412)%5 == 0 {
			return room, nil
		}
	}
	return hotspotRoom{}, fmt.Errorf("selected room disappeared; search again")
}

func runHotspot(ctx context.Context, role, peer string, control *workerControl) (err error) {
	lock, err := acquireHotspot(ctx)
	if err != nil {
		return err
	}
	defer lock.Close()
	if role == "hotspot-scan" {
		if err = control.send('S', "Searching nearby hotspot rooms..."); err != nil {
			return err
		}
		rooms, err := scanHotspots(ctx)
		if err != nil {
			return err
		}
		if err = hotspotWrite("rooms.json", struct {
			Created int64         `json:"created"`
			Rooms   []hotspotRoom `json:"rooms"`
		}{time.Now().Unix(), rooms}); err != nil {
			return err
		}
		records := make([]roomCandidate, 0, len(rooms))
		for _, room := range rooms {
			records = append(records, roomCandidate{address: room.Address, label: "GBA Link " + strings.TrimPrefix(room.SSID, hotspotPrefix), rssi: room.RSSI})
		}
		if err = control.rooms(records); err != nil {
			return err
		}
		<-ctx.Done()
		return ctx.Err()
	}
	lease, err := hotspotSnapshot(ctx, role)
	if err != nil {
		return err
	}
	defer func() {
		restoreErr := lease.restore()
		if restoreErr != nil {
			fmt.Fprintf(os.Stderr, "HOTSPOT_RESTORE_FAILED %v\n", restoreErr)
			err = errors.Join(err, restoreErr)
		} else {
			fmt.Println("HOTSPOT_RESTORED session_resources=true")
		}
	}()
	if _, err = hotspotCommand(ctx, "iw", "dev", "wlan0", "set", "power_save", "off"); err != nil {
		return err
	}
	if role == "hotspot-host" {
		// Use xradio's real AP interface. Additional virtual aliases do not receive
		// frames correctly on this firmware. Refuse an interface already in use.
		flags, err := os.ReadFile("/sys/class/net/wlan1/flags")
		if err != nil {
			return err
		}
		bits, err := strconv.ParseUint(strings.TrimSpace(string(flags)), 0, 32)
		if err != nil || bits&1 != 0 {
			return fmt.Errorf("Wi-Fi hotspot interface is already in use")
		}
		address, err := os.ReadFile("/sys/class/net/wlan1/address")
		if err != nil {
			return err
		}
		ssid, err := hotspotSSID(strings.TrimSpace(string(address)))
		if err != nil {
			return err
		}
		iface, err := hotspotCommand(ctx, "iw", "dev", "wlan1", "info")
		if err != nil || !strings.Contains(iface, "type AP") {
			return fmt.Errorf("hotspot interface unavailable")
		}
		if err = control.send('S', "Creating hotspot room..."); err != nil {
			return err
		}
		lease.Switched = true
		if err = lease.persist(); err != nil {
			return err
		}
		if err = hotspotOK(ctx, "disconnect"); err != nil {
			return err
		}
		lease.AP = true
		if err = lease.persist(); err != nil {
			return err
		}
		if _, err = hotspotCommand(ctx, "ip", "link", "set", "wlan1", "up"); err != nil {
			return err
		}
		lease.Address = true
		if err = lease.persist(); err != nil {
			return err
		}
		if _, err = hotspotCommand(ctx, "ip", "addr", "add", hotspotHostIP+"/30", "dev", "wlan1"); err != nil {
			return err
		}
		// Dedicated channel 1; no station connection competes with this AP.
		config := fmt.Sprintf("interface=wlan1\ndriver=nl80211\nssid=%s\nhw_mode=g\nchannel=1\nwmm_enabled=1\nieee80211n=1\nwpa=2\nwpa_passphrase=%s\nwpa_key_mgmt=WPA-PSK\nrsn_pairwise=CCMP\nmax_num_sta=1\n", ssid, hotspotPassword)
		path := filepath.Join(hotspotRoot, "hostapd.conf")
		if err = os.WriteFile(path, []byte(config), 0600); err != nil {
			return err
		}
		log := &hotspotLog{}
		cmd := exec.Command("hostapd", path)
		hotspotChild(cmd)
		cmd.Stdout = log
		cmd.Stderr = log
		if err = cmd.Start(); err != nil {
			return err
		}
		exited := make(chan error, 1)
		go func() { exited <- cmd.Wait() }()
		defer func() {
			cmd.Process.Signal(unix.SIGTERM)
			select {
			case <-exited:
			case <-time.After(500 * time.Millisecond):
				cmd.Process.Kill()
				<-exited
			}
			os.Remove(path)
		}()
		ready, cancel := context.WithTimeout(ctx, 8*time.Second)
		defer cancel()
		for !strings.Contains(log.text(), "AP-ENABLED") {
			select {
			case exit := <-exited:
				exited <- exit
				return fmt.Errorf("hotspot daemon stopped before ready")
			default:
			}
			if err = hotspotWait(ready, 50*time.Millisecond); err != nil {
				return err
			}
		}
		fmt.Println("HOTSPOT_AP_READY ssid=" + ssid)
		if err = control.send('P', hotspotHostIP+"\nGBA Link "+strings.TrimPrefix(ssid, hotspotPrefix)); err != nil {
			return err
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case exit := <-exited:
			exited <- exit
			return fmt.Errorf("hotspot daemon exited: %v", exit)
		}
	}
	room, err := cachedHotspot(peer)
	if err != nil {
		return err
	}
	if err = control.send('C', "Connecting to hotspot room..."); err != nil {
		return err
	}
	id, err := hotspotWPA(ctx, "add_network")
	if err != nil {
		return err
	}
	lease.Network, err = strconv.Atoi(id)
	if err != nil || lease.Network < 0 {
		return fmt.Errorf("cannot create temporary Wi-Fi entry")
	}
	if err = hotspotOK(ctx, "set_network", id, "id_str", strconv.Quote(hotspotMarker)); err != nil {
		cleanup, cancel := context.WithTimeout(context.Background(), 1500*time.Millisecond)
		defer cancel()
		hotspotOK(cleanup, "remove_network", id)
		return err
	}
	if err = lease.persist(); err != nil {
		return err
	}
	fields := [][2]string{{"ssid", strconv.Quote(room.SSID)}, {"psk", strconv.Quote(hotspotPassword)}, {"bssid", room.Address}, {"key_mgmt", "WPA-PSK"}, {"proto", "RSN"}, {"pairwise", "CCMP"}, {"scan_freq", strconv.Itoa(room.Frequency)}}
	for _, field := range fields {
		if err = hotspotOK(ctx, "set_network", id, field[0], field[1]); err != nil {
			return err
		}
	}
	lease.Switched = true
	if err = lease.persist(); err != nil {
		return err
	}
	if err = hotspotOK(ctx, "select_network", id); err != nil {
		return err
	}
	connected, cancel := context.WithTimeout(ctx, 25*time.Second)
	defer cancel()
	for {
		status, err := hotspotWPA(connected, "status")
		if err != nil {
			return err
		}
		fields := hotspotFields(status)
		if fields["wpa_state"] == "COMPLETED" && fields["id"] == id && fields["ssid"] == room.SSID && strings.EqualFold(fields["bssid"], room.Address) {
			break
		}
		if err = hotspotWait(connected, 100*time.Millisecond); err != nil {
			return err
		}
	}
	lease.Address = true
	if err = lease.persist(); err != nil {
		return err
	}
	if _, err = hotspotCommand(ctx, "ip", "addr", "add", hotspotClientIP+"/30", "dev", "wlan0"); err != nil {
		return err
	}
	fmt.Println("HOTSPOT_CLIENT_READY ssid=" + room.SSID)
	if err = control.send('P', hotspotHostIP); err != nil {
		return err
	}
	<-ctx.Done()
	return ctx.Err()
}

func serveHotspot(ctx context.Context, role, peer string, dataFD, controlFD int) error {
	if dataFD == controlFD {
		return fmt.Errorf("frontend FDs must be distinct")
	}
	stream, err := inheritedSocket(dataFD, unix.SOCK_STREAM, "hotspot lifetime")
	if err != nil {
		return err
	}
	defer stream.Close()
	file, err := inheritedSocket(controlFD, unix.SOCK_DGRAM, "hotspot control")
	if err != nil {
		return err
	}
	defer file.Close()
	control := &workerControl{file: file}
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	done := make(chan struct{})
	go func() {
		defer close(done)
		ticker := time.NewTicker(500 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				if control.send('K', "") != nil {
					cancel()
					return
				}
			}
		}
	}()
	defer func() { cancel(); <-done }()
	err = runHotspot(ctx, role, peer, control)
	if err != nil && ctx.Err() == nil {
		message := "Hotspot unavailable. Check Wi-Fi and try again."
		if role == "hotspot-scan" && errors.Is(err, context.DeadlineExceeded) {
			message = "No nearby rooms. Create a room on the other console first."
		}
		if strings.Contains(err.Error(), "turn on Wi-Fi") {
			message = "Turn on Wi-Fi, then try again."
		}
		if strings.Contains(err.Error(), "previous room is still closing") {
			message = "The previous room is closing. Try again shortly."
		}
		control.send('E', message)
	}
	return err
}
