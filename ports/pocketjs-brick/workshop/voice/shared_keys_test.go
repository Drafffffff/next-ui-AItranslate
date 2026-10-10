package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestSharedKeyPriorityAndReload(t *testing.T) {
	t.Setenv("NEXTUI_AI_KEYS_FILE", "")
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "config.json"), []byte(`{"asrKey":"legacy","asrEndpoint":"wss://workspace.ap-southeast-1.maas.aliyuncs.com/api-ws/v1/inference"}`), 0600)
	v := &Voice{data: dir, shared: dir}
	if v.config().AsrKey != "legacy" {
		t.Fatal("legacy fallback")
	}
	path := filepath.Join(dir, "ai-keys.txt")
	os.WriteFile(path, []byte("\xef\xbb\xbf# config\r\nDASHSCOPE_API_KEY = shared-one\r\n"), 0600)
	got := v.config()
	if got.AsrKey != "shared-one" || got.AsrEndpoint != "wss://workspace.ap-southeast-1.maas.aliyuncs.com/api-ws/v1/inference" || got.AsrModel != "qwen-audio-3.1-asr-flash-message" {
		t.Fatal("shared priority must preserve region and model")
	}
	os.WriteFile(path, []byte("DASHSCOPE_API_KEY=shared-two\n"), 0600)
	if v.config().AsrKey != "shared-two" {
		t.Fatal("must reload")
	}
	os.WriteFile(path, []byte("DASHSCOPE_API_KEY=\n"), 0600)
	if v.config().AsrKey != "legacy" {
		t.Fatal("empty fallback")
	}
	os.WriteFile(path, []byte("DASHSCOPE_API_KEY=one\nDASHSCOPE_API_KEY=two\n"), 0600)
	if got := v.config(); got.AsrKey != "" || got.KeyError == "" {
		t.Fatal("invalid common key must not fallback")
	}
}

func TestSharedParserBoundsAndLiteralValues(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "ai-keys.txt")
	t.Setenv("NEXTUI_AI_KEYS_FILE", path)
	for _, raw := range []string{"DASHSCOPE_API_KEY=a b", "DASHSCOPE_API_KEY=a\x00b", "DASHSCOPE_API_KEY=" + strings.Repeat("x", 512), strings.Repeat("#", 16385)} {
		os.WriteFile(path, []byte(raw), 0600)
		if key, err := readSharedBailian(dir); err == nil || key != "" {
			t.Fatal("invalid credential must fail closed")
		}
	}
	os.WriteFile(path, []byte("DASHSCOPE_API_KEY=$(touch-never-executed)\n"), 0600)
	if key, err := readSharedBailian(dir); err != nil || key != "$(touch-never-executed)" {
		t.Fatal("literal data")
	}
	if trustedBailianEndpoint("wss://dashscope.aliyuncs.com.evil.example/api-ws/v1/inference") || trustedBailianEndpoint("wss://user@dashscope.aliyuncs.com/api-ws/v1/inference") {
		t.Fatal("credential origin boundary")
	}
}
