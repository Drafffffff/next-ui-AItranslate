package main

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"golang.org/x/sys/unix"
)

// Explicit opt-in: this test requires the pinned real Mac gpSP and ARM SIO
// ROM. An absent runner is reported as SKIP, never a physical BLE/game pass.
func TestBridgeRealGpSPCore(t *testing.T) {
	runner := os.Getenv("NP_INTEGRATION_RUNNER")
	if runner == "" {
		t.Skip("real gpSP integration runner not requested")
	}
	core, rom := os.Getenv("NP_INTEGRATION_CORE"), os.Getenv("NP_INTEGRATION_ROM")
	for _, p := range []string{runner, core, rom} {
		if _, err := os.Stat(p); err != nil {
			t.Fatal(err)
		}
	}
	for _, caseName := range []string{"actual-core-sio", "rom-mismatch", "core-build-mismatch", "serial-mismatch"} {
		t.Run(caseName, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 35*time.Second)
			defer cancel()
			x, y := memoryPacketPair()
			radios := []duplexEndpoint{x, y}
			ready := make(chan int, 2)
			bridgeResults := make(chan error, 2)
			var appFiles []*os.File
			for i := 0; i < 2; i++ {
				fds, err := unix.Socketpair(unix.AF_UNIX, unix.SOCK_STREAM, 0)
				if err != nil {
					t.Fatal(err)
				}
				for _, fd := range fds {
					if err := unix.SetNonblock(fd, true); err != nil {
						t.Fatal(err)
					}
				}
				worker := os.NewFile(uintptr(fds[0]), "core bridge worker")
				app := os.NewFile(uintptr(fds[1]), "inherited core stream")
				appFiles = append(appFiles, app)
				go func(index int) {
					bridgeResults <- bridgeSession(ctx, radios[index], 512, 404, index == 0, worker, func() error { ready <- index; return nil })
				}(i)
			}
			for i := 0; i < 2; i++ {
				select {
				case <-ready:
				case <-ctx.Done():
					t.Fatal("core bridge readiness")
				}
			}
			var commands []*exec.Cmd
			var logs []*os.File
			for i, role := range []string{"host", "join"} {
				game, mode := rom, "mul_poke"
				if i == 1 && caseName == "rom-mismatch" {
					b, err := os.ReadFile(rom)
					if err != nil {
						t.Fatal(err)
					}
					b[len(b)-1] ^= 1
					game = filepath.Join(t.TempDir(), "changed.gba")
					if err := os.WriteFile(game, b, 0600); err != nil {
						t.Fatal(err)
					}
				}
				if i == 1 && caseName == "serial-mismatch" {
					mode = "rfu"
				}
				cmd := exec.CommandContext(ctx, runner, core, game, role, "127.0.0.1", "1", mode, "240")
				cmd.Env = append(os.Environ(), "NP_STREAM_FD=3", "NP_DUEL=1")
				script := filepath.Join(t.TempDir(), "keys.txt")
				keys := "100 106 256\n170 176 256\n"
				if i == 1 {
					keys = "130 136 256\n"
				}
				if err := os.WriteFile(script, []byte(keys), 0600); err != nil {
					t.Fatal(err)
				}
				cmd.Env = append(cmd.Env, "NP_SCRIPT="+script)
				if i == 1 && caseName == "core-build-mismatch" {
					cmd.Env = append(cmd.Env, "NP_CORE_MISMATCH=1")
				}
				cmd.ExtraFiles = []*os.File{appFiles[i]}
				log, err := os.Create(filepath.Join(t.TempDir(), role+".log"))
				if err != nil {
					t.Fatal(err)
				}
				logs = append(logs, log)
				cmd.Stdout = log
				cmd.Stderr = log
				if err := cmd.Start(); err != nil {
					t.Fatal(err)
				}
				commands = append(commands, cmd)
				appFiles[i].Close()
			}
			var codes []int
			var contents []string
			for i, cmd := range commands {
				cmd.Wait()
				codes = append(codes, cmd.ProcessState.ExitCode())
				logs[i].Close()
				b, err := os.ReadFile(logs[i].Name())
				if err != nil {
					t.Fatal(err)
				}
				contents = append(contents, string(b))
			}
			cancel()
			for i := 0; i < 2; i++ {
				select {
				case <-bridgeResults:
				case <-time.After(time.Second):
					t.Fatal("core bridge cleanup")
				}
			}
			if caseName == "actual-core-sio" {
				if codes[0] != 0 || codes[1] != 0 {
					t.Fatalf("real core failed %v\n%s", codes, strings.Join(contents, "\n"))
				}
			} else {
				token := map[string]string{"rom-mismatch": "ROM mismatch", "core-build-mismatch": "Core build mismatch", "serial-mismatch": "Link mode mismatch"}[caseName]
				for i, log := range contents {
					if codes[i] != 1 || !strings.Contains(log, token) || !strings.Contains(log, "frames=0") {
						t.Fatalf("identity guard %v\n%s", codes, strings.Join(contents, "\n"))
					}
				}
			}
			fmt.Printf("REAL_CORE_BRIDGE_PASS case=%s codes=%v scope=Mac-core-over-BLE-framing-not-physical-radio\n", caseName, codes)
		})
	}
}
