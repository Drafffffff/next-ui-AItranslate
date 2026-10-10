package main

import (
	"bytes"
	"errors"
	"io"
	"net/url"
	"os"
	"path/filepath"
	"strings"
)

// Only provider-owned endpoints receive a shared Bailian credential.
func trustedBailianEndpoint(endpoint string) bool {
	u, err := url.Parse(endpoint)
	if err != nil || u.Scheme != "wss" || u.User != nil || (u.Port() != "" && u.Port() != "443") {
		return false
	}
	host := u.Hostname()
	return host == "dashscope.aliyuncs.com" || host == "dashscope-intl.aliyuncs.com" || host == "dashscope-us.aliyuncs.com" || (strings.HasSuffix(host, ".maas.aliyuncs.com") && !strings.ContainsAny(host, " /\\"))
}

func readSharedBailian(shared string) (string, error) {
	path := os.Getenv("NEXTUI_AI_KEYS_FILE")
	if path == "" {
		path = filepath.Join(shared, "ai-keys.txt")
	}
	file, err := os.Open(path)
	if errors.Is(err, os.ErrNotExist) {
		return "", nil
	}
	if err != nil {
		return "", err
	}
	defer file.Close()
	stat, err := file.Stat()
	if err != nil {
		return "", err
	}
	if !stat.Mode().IsRegular() || stat.Size() > 16384 {
		return "", errors.New("invalid shared credentials file")
	}
	raw, err := io.ReadAll(io.LimitReader(file, 16385))
	if err != nil {
		return "", err
	}
	if len(raw) > 16384 || bytes.IndexByte(raw, 0) >= 0 {
		return "", errors.New("invalid shared credentials file")
	}
	raw = bytes.TrimPrefix(raw, []byte{0xef, 0xbb, 0xbf})
	key, found := "", false
	for _, line := range strings.Split(string(raw), "\n") {
		if len(line) > 1022 {
			return "", errors.New("shared credentials line too long")
		}
		line = strings.TrimLeft(line, " \t")
		if strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
			continue
		}
		name, value, ok := strings.Cut(line, "=")
		if !ok || strings.TrimRight(name, " \t") != "DASHSCOPE_API_KEY" {
			continue
		}
		if found {
			return "", errors.New("duplicate shared credential")
		}
		found = true
		value = strings.Trim(value, " \t\r")
		if len(value) > 511 {
			return "", errors.New("shared credential too long")
		}
		for _, c := range []byte(value) {
			if c < 33 || c > 126 {
				return "", errors.New("invalid shared credential")
			}
		}
		key = value
	}
	return key, nil
}
