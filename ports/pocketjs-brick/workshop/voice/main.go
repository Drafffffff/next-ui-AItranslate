// Direct, on-device Qwen Message ASR. Protocol matches Brick Mic's ASR.swift.
package main

import (
	"bufio"
	"context"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"math"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/gorilla/websocket"
)

type Config struct {
	KeyError    string `json:"-"`
	AsrKey      string `json:"asrKey"`
	AsrEndpoint string `json:"asrEndpoint"`
	AsrModel    string `json:"asrModel"`
}
type State struct {
	State   string  `json:"state"`
	Session uint64  `json:"session"`
	Seconds float64 `json:"seconds"`
	RMS     int     `json:"rms"`
	Text    string  `json:"text"`
	Error   string  `json:"error"`
}
type Voice struct {
	mu                  sync.Mutex
	state               State
	cancel              context.CancelFunc
	stop                chan struct{}
	done                chan struct{}
	data, shared, probe string
	test                bool
}

func setting(file, key string) string {
	f, e := os.Open(file)
	if e != nil {
		return ""
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	for s.Scan() {
		p := strings.SplitN(s.Text(), "=", 2)
		if len(p) == 2 && p[0] == key {
			return strings.TrimSpace(p[1])
		}
	}
	return ""
}
func (v *Voice) config() Config {
	c := Config{}
	b, _ := os.ReadFile(filepath.Join(v.data, "config.json"))
	_ = json.Unmarshal(b, &c)
	if c.AsrKey == "" {
		c.AsrKey = setting(filepath.Join(v.shared, "voice-ai.txt"), "voiceAsrKey")
	}
	if c.AsrKey == "" {
		c.AsrKey = setting(filepath.Join(v.shared, "ai-translate.txt"), "aiBailianKey")
	}
	if c.AsrEndpoint == "" {
		endpoint := setting(filepath.Join(v.shared, "voice-ai.txt"), "voiceAsrEndpoint")
		if strings.HasPrefix(endpoint, "wss://") {
			c.AsrEndpoint = endpoint
		}
	}
	if c.AsrEndpoint == "" {
		c.AsrEndpoint = "wss://dashscope.aliyuncs.com/api-ws/v1/inference"
	}
	if c.AsrModel == "" {
		c.AsrModel = "qwen-audio-3.1-asr-flash-message"
	}
	if trustedBailianEndpoint(c.AsrEndpoint) {
		key, err := readSharedBailian(v.shared)
		if err != nil {
			c.AsrKey = ""
			c.KeyError = "公共 Key 配置格式无效，请检查 ai-keys.txt"
		} else if key != "" {
			c.AsrKey = key
		}
	}
	return c
}
func (v *Voice) snapshot() State { v.mu.Lock(); defer v.mu.Unlock(); return v.state }
func (v *Voice) update(id uint64, fn func(*State)) {
	v.mu.Lock()
	defer v.mu.Unlock()
	if v.state.Session == id {
		fn(&v.state)
	}
}
func (v *Voice) command(op string) {
	v.mu.Lock()
	defer v.mu.Unlock()
	switch op {
	case "start":
		if v.state.State == "recording" || v.state.State == "processing" {
			return
		}
		if v.done != nil {
			select {
			case <-v.done:
			default:
				return
			}
		}
		ctx, cancel := context.WithCancel(context.Background())
		v.cancel = cancel
		v.stop = make(chan struct{})
		v.done = make(chan struct{})
		v.state = State{State: "recording", Session: v.state.Session + 1}
		go v.run(ctx, v.state.Session, v.stop, v.done)
	case "stop":
		if v.stop != nil {
			select {
			case <-v.stop:
			default:
				close(v.stop)
			}
		}
		if v.state.State == "recording" {
			v.state.State = "processing"
		}
	case "cancel":
		if v.cancel != nil {
			v.cancel()
		}
		v.state.Session++
		v.state.State = "idle"
		v.state.Text = ""
		v.state.Error = ""
		v.state.RMS = 0
	}
}
func (v *Voice) capture(ctx context.Context, id uint64, stop <-chan struct{}, out chan<- []byte) error {
	defer close(out)
	recCtx, cancel := context.WithCancel(ctx)
	defer cancel()
	go func() {
		select {
		case <-stop:
			cancel()
		case <-recCtx.Done():
		}
	}()
	var source io.ReadCloser
	var cmd *exec.Cmd
	if v.probe != "" {
		f, e := os.Open(v.probe)
		if e != nil {
			return errors.New("测试音频无法读取")
		}
		source = f
	} else {
		cmd = exec.CommandContext(recCtx, "arecord", "-q", "-D", "plughw:0,0", "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1", "--buffer-time=100000")
		p, e := cmd.StdoutPipe()
		if e != nil {
			return errors.New("无法打开麦克风")
		}
		if e = cmd.Start(); e != nil {
			return errors.New("无法启动内置麦克风")
		}
		source = p
	}
	defer source.Close()
	if cmd != nil {
		defer func() { cancel(); _ = cmd.Wait() }()
	}
	samples := 0
	started := time.Now()
	for samples < 16000*60 {
		select {
		case <-recCtx.Done():
			return nil
		default:
		}
		b := make([]byte, 640)
		n, e := io.ReadFull(source, b)
		n -= n % 2
		if n > 0 {
			b = b[:n]
			samples += n / 2
			energy := 0.0
			for i := 0; i < n; i += 2 {
				x := float64(int16(binary.LittleEndian.Uint16(b[i:])))
				energy += x * x
			}
			v.update(id, func(s *State) { s.Seconds = float64(samples) / 16000; s.RMS = int(math.Sqrt(energy / float64(n/2))) })
			select {
			case out <- b:
			case <-recCtx.Done():
				return nil
			default:
				return errors.New("网络上传过慢，本次输入已取消")
			}
			if v.probe != "" {
				t := time.NewTimer(time.Until(started.Add(time.Duration(samples) * time.Second / 16000)))
				select {
				case <-t.C:
				case <-recCtx.Done():
					t.Stop()
					return nil
				}
			}
		}
		if e != nil {
			if recCtx.Err() != nil || e == io.EOF || e == io.ErrUnexpectedEOF {
				return nil
			}
			return errors.New("麦克风读取失败")
		}
	}
	return nil
}
func (v *Voice) run(ctx context.Context, id uint64, stop <-chan struct{}, done chan<- struct{}) {
	defer close(done)
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	fail := func(msg string) {
		if ctx.Err() == nil {
			v.update(id, func(s *State) { s.State = "error"; s.Error = msg; s.RMS = 0 })
		}
	}
	c := v.config()
	if c.KeyError != "" {
		fail(c.KeyError)
		return
	}
	if c.AsrKey == "" {
		fail("请配置百炼语音识别 Key")
		return
	}
	u, e := url.Parse(c.AsrEndpoint)
	if e != nil || u.Host == "" || (u.Scheme != "wss" && !(v.test && u.Scheme == "ws")) {
		fail("语音识别地址必须为 wss://")
		return
	}
	q := u.Query()
	if u.Path == "/api-ws/v1/realtime" {
		u.Path = "/api-ws/v1/inference"
	}
	if u.Path != "/api-ws/v1/inference" {
		fail("识别地址应为 /api-ws/v1/inference")
		return
	}
	if c.AsrModel != "qwen-audio-3.1-asr-flash-message" {
		fail("识别模型应为 qwen-audio-3.1-asr-flash-message")
		return
	}
	q.Del("model")
	u.RawQuery = q.Encode()
	audio := make(chan []byte, 150)
	captureDone := make(chan error, 1)
	go func() { captureDone <- v.capture(ctx, id, stop, audio) }()
	defer func() { cancel(); <-captureDone }()
	pool, _ := x509.SystemCertPool()
	if pool == nil {
		pool = x509.NewCertPool()
	}
	if path := os.Getenv("POCKETJS_CA"); path != "" {
		pem, e := os.ReadFile(path)
		if e != nil || !pool.AppendCertsFromPEM(pem) {
			fail("语音识别证书无法加载")
			return
		}
	}
	d := websocket.Dialer{Proxy: http.ProxyFromEnvironment, HandshakeTimeout: 10 * time.Second, TLSClientConfig: &tls.Config{RootCAs: pool, MinVersion: tls.VersionTLS12}}
	conn, resp, e := d.DialContext(ctx, u.String(), http.Header{"Authorization": []string{"Bearer " + c.AsrKey}})
	if e != nil {
		if resp != nil {
			fail(fmt.Sprintf("百炼连接失败（HTTP %d）", resp.StatusCode))
			resp.Body.Close()
		} else {
			fail("百炼连接失败，请检查 Wi-Fi 和识别配置")
		}
		return
	}
	defer conn.Close()
	go func() { <-ctx.Done(); conn.Close() }()
	uuid := make([]byte, 16)
	if _, e = rand.Read(uuid); e != nil {
		fail("识别任务无法初始化")
		return
	}
	uuid[6] = (uuid[6] & 0x0f) | 0x40
	uuid[8] = (uuid[8] & 0x3f) | 0x80
	taskID := fmt.Sprintf("%x-%x-%x-%x-%x", uuid[:4], uuid[4:6], uuid[6:8], uuid[8:10], uuid[10:])
	send := func(action string, payload map[string]any) error {
		_ = conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
		return conn.WriteJSON(map[string]any{"header": map[string]any{"action": action, "task_id": taskID, "streaming": "duplex"}, "payload": payload})
	}
	ready := make(chan struct{})
	finished := make(chan error, 1)
	var transcript string
	conn.SetReadLimit(131072)
	_ = conn.SetReadDeadline(time.Now().Add(10 * time.Second))
	go func() {
		results := map[int]string{}
		opened := false
		for {
			var event struct {
				Header struct {
					Event  string `json:"event"`
					TaskID string `json:"task_id"`
					Code   string `json:"error_code"`
				} `json:"header"`
				Payload struct {
					Output struct {
						Sentence struct {
							ID        int    `json:"sentence_id"`
							End       bool   `json:"sentence_end"`
							Heartbeat bool   `json:"heartbeat"`
							Text      string `json:"text"`
						} `json:"sentence"`
					} `json:"output"`
				} `json:"payload"`
			}
			if e := conn.ReadJSON(&event); e != nil {
				finished <- errors.New("语音识别连接中断或超时")
				return
			}
			if event.Header.TaskID != taskID {
				continue
			}
			switch event.Header.Event {
			case "task-started":
				if !opened {
					close(ready)
					opened = true
				}
				_ = conn.SetReadDeadline(time.Now().Add(75 * time.Second))
			case "result-generated":
				s := event.Payload.Output.Sentence
				if !opened || !s.End || s.Heartbeat || s.ID < 1 {
					continue
				}
				if s.ID > 4096 || len(s.Text) > 96000 {
					finished <- errors.New("识别结果过长，请缩短录音")
					return
				}
				results[s.ID] = s.Text
				total := 0
				for _, text := range results {
					total += len(text)
				}
				if total > 96000 {
					finished <- errors.New("识别结果过长，请缩短录音")
					return
				}
			case "task-finished":
				keys := make([]int, 0, len(results))
				for key := range results {
					keys = append(keys, key)
				}
				sort.Ints(keys)
				for _, key := range keys {
					transcript += results[key]
				}
				finished <- nil
				return
			case "task-failed":
				code := strings.Map(func(r rune) rune {
					if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '_' || r == '-' || r == '.' {
						return r
					}
					return -1
				}, event.Header.Code)
				if len(code) > 64 {
					code = code[:64]
				}
				if code == "" {
					code = "unknown"
				}
				finished <- fmt.Errorf("百炼语音识别失败（%s），请检查 Key、模型和地域", code)
				return
			}
		}
	}()
	if e = send("run-task", map[string]any{"task_group": "audio", "task": "asr", "function": "recognition", "model": c.AsrModel, "input": map[string]any{}, "parameters": map[string]any{"format": "pcm", "sample_rate": 16000, "intermediate_result_enabled": false, "vad_model": "near_meeting_16k"}}); e != nil {
		fail("语音识别初始化失败")
		return
	}
	select {
	case <-ready:
	case e = <-finished:
		if e != nil {
			fail(e.Error())
		} else {
			fail("识别服务提前结束，请重新录音")
		}
		return
	case <-ctx.Done():
		return
	}
	bytes := 0
	for {
		select {
		case <-ctx.Done():
			return
		case e = <-finished:
			if e != nil {
				fail(e.Error())
			} else {
				fail("识别服务提前结束，请重新录音")
			}
			return
		case b, ok := <-audio:
			if ok {
				bytes += len(b)
				_ = conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
				if e = conn.WriteMessage(websocket.BinaryMessage, b); e != nil {
					fail("语音上传失败")
					return
				}
				continue
			}
			v.update(id, func(s *State) { s.State = "processing"; s.RMS = 0 })
			if bytes < 3200 {
				fail("录音太短，请按住 A 说话")
				return
			}
			// capture error is checked before committing; incomplete audio is never submitted.
			e = <-captureDone
			captureDone <- e
			if e != nil {
				fail(e.Error())
				return
			}
			e = send("finish-task", map[string]any{"input": map[string]any{}})
			if e != nil {
				fail("提交录音失败")
				return
			}
			_ = conn.SetReadDeadline(time.Now().Add(15 * time.Second))
			select {
			case e = <-finished:
				if e != nil {
					fail(e.Error())
					return
				}
			case <-ctx.Done():
				return
			}
			text := strings.TrimSpace(transcript)
			if text == "" {
				fail("没有听清，请再说一次")
				return
			}
			v.update(id, func(s *State) { s.State = "result"; s.Text = text; s.Error = "" })
			return
		}
	}
}
func main() {
	socket := flag.String("socket", "", "private IPC socket")
	data := flag.String("data", "", "workshop data directory")
	shared := flag.String("settings", "", "shared settings directory")
	probe := flag.String("probe-file", "", "diagnostic 16kHz S16LE input; normal app never sets this")
	test := flag.Bool("test-mode", false, "allow local mock WebSocket")
	command := flag.String("command", "", "diagnostic IPC command: start/stop/cancel/status")
	checkConfig := flag.Bool("check-config", false, "check ASR credentials without recording or networking")
	flag.Parse()
	if *checkConfig {
		if *data == "" || *shared == "" {
			os.Exit(2)
		}
		v := &Voice{data: *data, shared: *shared}
		c := v.config()
		key, err := readSharedBailian(*shared)
		configured := c.AsrKey != "" && c.KeyError == ""
		fmt.Printf("ASR configured=%t shared_key=%t model_preserved=%t\n", configured, err == nil && key != "" && c.AsrKey == key, c.AsrModel == "qwen-audio-3.1-asr-flash-message")
		if !configured {
			os.Exit(1)
		}
		return
	}
	if *command != "" {
		if *socket == "" || (*command != "start" && *command != "stop" && *command != "cancel" && *command != "status") {
			os.Exit(2)
		}
		c, e := net.DialTimeout("unix", *socket, time.Second)
		if e != nil {
			os.Exit(1)
		}
		defer c.Close()
		_ = c.SetDeadline(time.Now().Add(time.Second))
		_, _ = fmt.Fprintln(c, *command)
		var state State
		if json.NewDecoder(io.LimitReader(c, 131072)).Decode(&state) != nil {
			os.Exit(1)
		}
		_ = json.NewEncoder(os.Stdout).Encode(state)
		return
	}
	if *socket == "" || *data == "" || *shared == "" {
		os.Exit(2)
	}
	v := &Voice{state: State{State: "idle"}, data: *data, shared: *shared, probe: *probe, test: *test}
	l, e := net.Listen("unix", *socket)
	if e != nil {
		os.Exit(1)
	}
	_ = os.Chmod(*socket, 0600)
	defer os.Remove(*socket)
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGTERM, syscall.SIGINT)
	go func() { <-sig; v.command("cancel"); l.Close() }()
	for {
		c, e := l.Accept()
		if e != nil {
			break
		}
		go func() {
			defer c.Close()
			_ = c.SetDeadline(time.Now().Add(time.Second))
			line, _ := bufio.NewReader(io.LimitReader(c, 64)).ReadString('\n')
			op := strings.TrimSpace(line)
			if op == "quit" {
				_ = syscall.Kill(os.Getpid(), syscall.SIGTERM)
			} else {
				v.command(op)
			}
			_ = json.NewEncoder(c).Encode(v.snapshot())
		}()
	}
	v.command("cancel")
	v.mu.Lock()
	done := v.done
	v.mu.Unlock()
	if done != nil {
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	}
}
