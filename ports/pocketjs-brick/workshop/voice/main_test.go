package main

import (
	"context"
	"encoding/json"
	"github.com/gorilla/websocket"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

func awaitState(t *testing.T, v *Voice, want string) State {
	t.Helper()
	until := time.Now().Add(4 * time.Second)
	for time.Now().Before(until) {
		s := v.snapshot()
		if s.State == want {
			return s
		}
		if s.State == "error" {
			t.Fatalf("voice error: %s", s.Error)
		}
		time.Sleep(time.Millisecond * 10)
	}
	t.Fatalf("state=%+v, expected %s", v.snapshot(), want)
	return State{}
}
func TestMessageWireAndSentenceAggregation(t *testing.T) {
	var mu sync.Mutex
	var wire []string
	var bytes int
	upgrader := websocket.Upgrader{}
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api-ws/v1/inference" || r.URL.Query().Get("model") != "" {
			t.Error("endpoint migration changed protocol incorrectly")
		}
		if r.Header.Get("Authorization") != "Bearer test-only-key" {
			t.Error("missing auth")
		}
		c, e := upgrader.Upgrade(w, r, nil)
		if e != nil {
			return
		}
		defer c.Close()
		var run map[string]any
		if e = c.ReadJSON(&run); e != nil {
			t.Error(e)
			return
		}
		header := run["header"].(map[string]any)
		id := header["task_id"].(string)
		if header["action"] != "run-task" || header["streaming"] != "duplex" {
			t.Error("wrong run header")
		}
		payload := run["payload"].(map[string]any)
		if payload["model"] != "qwen-audio-3.1-asr-flash-message" {
			t.Error("wrong model")
		}
		event := func(kind string, payload any) {
			_ = c.WriteJSON(map[string]any{"header": map[string]any{"event": kind, "task_id": id}, "payload": payload})
		}
		event("task-started", map[string]any{})
		for {
			kind, b, e := c.ReadMessage()
			if e != nil {
				return
			}
			mu.Lock()
			if kind == websocket.BinaryMessage {
				bytes += len(b)
				wire = append(wire, "pcm")
				mu.Unlock()
				continue
			}
			wire = append(wire, "finish")
			mu.Unlock()
			var finish map[string]any
			_ = json.Unmarshal(b, &finish)
			if finish["header"].(map[string]any)["action"] != "finish-task" {
				t.Error("unexpected JSON instead of binary PCM")
			}
			for _, s := range []struct {
				id   int
				text string
			}{{2, "向右移动"}, {1, "请让角色"}, {2, "移动更快"}} {
				event("result-generated", map[string]any{"output": map[string]any{"sentence": map[string]any{"sentence_id": s.id, "sentence_end": true, "text": s.text}}})
			}
			event("task-finished", map[string]any{})
			return
		}
	}))
	defer server.Close()
	root := t.TempDir()
	probe := filepath.Join(root, "probe.pcm")
	_ = os.WriteFile(probe, make([]byte, 16000), 0600)
	c := Config{AsrKey: "test-only-key", AsrEndpoint: "ws" + strings.TrimPrefix(server.URL, "http") + "/api-ws/v1/realtime?model=old", AsrModel: "qwen-audio-3.1-asr-flash-message"}
	b, _ := json.Marshal(c)
	_ = os.WriteFile(filepath.Join(root, "config.json"), b, 0600)
	v := &Voice{state: State{State: "idle"}, data: root, shared: root, probe: probe, test: true}
	v.command("start")
	v.command("start")
	s := awaitState(t, v, "result")
	if s.Session != 1 || s.Text != "请让角色移动更快" {
		t.Fatalf("unexpected result %+v", s)
	}
	mu.Lock()
	defer mu.Unlock()
	if bytes != 16000 || len(wire) < 2 || wire[len(wire)-1] != "finish" {
		t.Fatalf("audio not flushed before finish: %d %v", bytes, wire)
	}
	state, _ := json.Marshal(s)
	if strings.Contains(string(state), "test-only-key") {
		t.Fatal("key persisted in state")
	}
}
func TestCancelNeverPublishesLateResult(t *testing.T) {
	v := &Voice{state: State{State: "recording", Session: 1}, stop: make(chan struct{})}
	ctx, cancel := context.WithCancel(context.Background())
	v.cancel = cancel
	v.command("cancel")
	v.update(1, func(s *State) { s.State = "result"; s.Text = "stale" })
	if ctx.Err() == nil || v.snapshot().State != "idle" || v.snapshot().Text != "" {
		t.Fatal("cancel leaked a late transcript")
	}
}
func TestConfiguredRegionAndModelStayExact(t *testing.T) {
	dir := t.TempDir()
	_ = os.WriteFile(filepath.Join(dir, "voice-ai.txt"), []byte("voiceAsrKey=test-only-key\n"), 0600)
	cfg := Config{AsrEndpoint: "wss://workspace.ap-southeast-1.maas.aliyuncs.com/api-ws/v1/inference", AsrModel: "qwen-audio-3.1-asr-flash-message"}
	b, _ := json.Marshal(cfg)
	_ = os.WriteFile(filepath.Join(dir, "config.json"), b, 0600)
	v := &Voice{data: dir, shared: dir}
	got := v.config()
	if got.AsrKey != "test-only-key" || got.AsrEndpoint != cfg.AsrEndpoint || got.AsrModel != cfg.AsrModel {
		t.Fatalf("config changed %+v", got)
	}
}
