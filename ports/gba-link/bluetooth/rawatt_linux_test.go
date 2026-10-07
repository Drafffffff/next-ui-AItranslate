package main

import (
	"bytes"
	"os"
	"testing"
	"time"

	"golang.org/x/sys/unix"
)

func attPair(t *testing.T) (*rawATT, *os.File) {
	t.Helper()
	fds, err := unix.Socketpair(unix.AF_UNIX, unix.SOCK_SEQPACKET|unix.SOCK_NONBLOCK, 0)
	if err != nil {
		t.Fatal(err)
	}
	a, b := os.NewFile(uintptr(fds[0]), "ATT test"), os.NewFile(uintptr(fds[1]), "peer")
	t.Cleanup(func() { a.Close(); b.Close() })
	a.SetDeadline(time.Now().Add(time.Second))
	b.SetDeadline(time.Now().Add(time.Second))
	return &rawATT{file: a, mtu: 517, tx: 59, rx: 62}, b
}

func TestRawATTHandlesPeerDiscoveryBeforeOurResponse(t *testing.T) {
	a, b := attPair(t)
	for _, p := range [][]byte{{2, 23, 0}, {0x10, 1, 0, 255, 255, 0, 0x28}, {3, 5, 2}} {
		if _, err := b.Write(p); err != nil {
			t.Fatal(err)
		}
	}
	p, err := a.request([]byte{2, 5, 2}, 3)
	if err != nil || !bytes.Equal(p, []byte{3, 5, 2}) {
		t.Fatal(p, err)
	}
	for _, want := range [][]byte{{2, 5, 2}, {3, 5, 2}, {1, 0x10, 1, 0, 0x0a}} {
		buf := make([]byte, 517)
		n, err := b.Read(buf)
		if err != nil || !bytes.Equal(buf[:n], want) {
			t.Fatal(buf[:n], want, err)
		}
	}
}

func TestRawATTRejectsWrongNotificationAndTruncation(t *testing.T) {
	for _, p := range [][]byte{{0x1b, 60, 0, 1}, {0x23, 59, 0, 1, 0, 1}, {0x1b, 59}, {0x1b, 59, 0, 1, 2, 3}} {
		a, b := attPair(t)
		b.Write(p)
		if _, err := a.Read(make([]byte, 2)); err == nil {
			t.Fatalf("accepted %x", p)
		}
	}
}

func TestRawATTRejectsMalformedServiceLists(t *testing.T) {
	for _, p := range [][]byte{{0x11}, {0x11, 0}, {0x11, 20, 1, 0}, {0x11, 6, 0, 0, 5, 0, 0, 0x28}, {0x11, 6, 7, 0, 6, 0, 0, 0x28}} {
		a, b := attPair(t)
		b.Write(p)
		if _, _, err := a.discover(); err == nil {
			t.Fatalf("accepted malformed service %x", p)
		}
	}
}

func TestRawATTWriteUsesNegotiatedMTU(t *testing.T) {
	a, b := attPair(t)
	a.mtu = 23
	if _, err := a.Write(make([]byte, 21)); err == nil {
		t.Fatal("oversize write accepted")
	}
	if n, err := a.Write([]byte{1, 2}); n != 2 || err != nil {
		t.Fatal(n, err)
	}
	buf := make([]byte, 23)
	n, err := b.Read(buf)
	if err != nil || !bytes.Equal(buf[:n], []byte{0x52, 62, 0, 1, 2}) {
		t.Fatal(buf[:n], err)
	}
}
