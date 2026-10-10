package main

import (
	"context"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

func TestHotspotScanFiltersRoomsAndSecurity(t *testing.T) {
	room := `BSS dc:44:60:86:8c:82(on wlan0)
 freq: 2442
 signal: -42.00 dBm
 SSID: NextUI-GBA-8C82
 RSN: * Version: 1
  * Pairwise ciphers: CCMP
  * Authentication suites: PSK
`
	raw := strings.Replace(room, "8c:82", "9c:83", 1) + strings.Replace(room, "NextUI-GBA-8C82", "Home Wi-Fi", 1) + strings.Replace(room, "RSN:", "WPA:", 1) + room + room
	got := parseHotspotScan(raw)
	if len(got) != 1 || got[0].Address != "DC:44:60:86:8C:82" || got[0].Frequency != 2442 || got[0].RSSI != -42 {
		t.Fatalf("bad room filtering: %+v", got)
	}
	for _, bad := range []string{strings.Replace(room, "PSK", "EAP", 1), strings.Replace(room, "CCMP", "TKIP", 1), strings.Replace(room, "2442", "2484", 1), strings.Replace(room, "2442", "2443", 1)} {
		if got := parseHotspotScan(bad); len(got) != 0 {
			t.Fatalf("unsafe/unsupported room accepted: %+v", got)
		}
	}
	if _, err := hotspotSSID("invalid\nssid"); err == nil {
		t.Fatal("malformed address accepted")
	}
}

func TestHotspotRestoreDoesNotOverwriteForeignNetwork(t *testing.T) {
	for _, foreign := range []bool{false, true} {
		t.Run(map[bool]string{false: "owned session", true: "reused network ID"}[foreign], func(t *testing.T) {
			root := t.TempDir()
			if err := os.WriteFile(filepath.Join(root, "lease.json"), []byte("{}"), 0600); err != nil {
				t.Fatal(err)
			}
			var mutations []string
			original := hotspotExecute
			defer func() { hotspotExecute = original }()
			hotspotExecute = func(ctx context.Context, limit time.Duration, name string, args ...string) (string, error) {
				if name == "ip" && len(args) > 1 && args[0] == "-4" {
					return "2: wlan0 inet 192.168.241.2/30 scope global wlan0", nil
				}
				if name == "wpa_cli" {
					command := args[4]
					if command == "status" {
						return "id=1\nwpa_state=COMPLETED", nil
					}
					if command == "get_network" {
						if args[6] == "id_str" {
							if foreign {
								return `"some-other-application"`, nil
							}
							return strconv.Quote(hotspotMarker), nil
						}
						return map[string]string{"0": `"Home"`, "2": `"Office"`, "3": `"Disabled"`}[args[5]], nil
					}
					mutations = append(mutations, strings.Join(args[4:], " "))
					return "OK", nil
				}
				mutations = append(mutations, name+" "+strings.Join(args, " "))
				return "", nil
			}
			lease := hotspotLease{Role: "hotspot-join", Network: 1, Original: 0, Switched: true, Reconnect: true, Power: "on", Address: true, Networks: []hotspotNetwork{{0, `"Home"`, true}, {2, `"Office"`, true}, {3, `"Disabled"`, false}}}
			if err := lease.restoreAt(root); err != nil {
				t.Fatal(err)
			}
			got := strings.Join(mutations, "\n")
			if foreign {
				for _, bad := range []string{"remove_network", "select_network", "enable_network"} {
					if strings.Contains(got, bad) {
						t.Fatalf("foreign network changed: %s", got)
					}
				}
			} else {
				for _, want := range []string{"remove_network 1", "select_network 0", "enable_network 2", "ip addr del 192.168.241.2/30 dev wlan0"} {
					if !strings.Contains(got, want) {
						t.Fatalf("missing restore %q: %s", want, got)
					}
				}
			}
			for _, bad := range []string{"enable_network 3", "save_config", "reconfigure", "flush", "killall"} {
				if strings.Contains(got, bad) {
					t.Fatalf("unrelated/persistent settings touched: %s", got)
				}
			}
		})
	}
}
