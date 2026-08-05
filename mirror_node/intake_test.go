package mirrornode

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestIntakeBridgeRunsOnlyWhileWorkIsPending(t *testing.T) {
	var pending atomic.Int64
	pending.Store(2)
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/repo/forkmesh/forkmesh/pending" {
			t.Errorf("unexpected path %s", r.URL.Path)
		}
		_, _ = w.Write([]byte(`{"ok":true,"pending":{"issues":` + strconv.FormatInt(pending.Load(), 10) + `,"pulls":0,"discussions":0}}`))
	}))
	defer server.Close()
	program := filepath.Join(t.TempDir(), "intake")
	if err := os.WriteFile(program, []byte("#!/bin/sh\ntrap 'exit 0' TERM\nwhile :; do sleep 1; done\n"), 0700); err != nil {
		t.Fatal(err)
	}
	bridge, err := NewIntakeBridge(program, server.URL+"/api/repositories", "forkmesh", "forkmesh", time.Second, 5*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	bridge.client = server.Client()
	bridge.idleGrace = 0
	bridge.cycle(context.Background())
	if running, starts := bridge.Snapshot(); !running || starts != 1 {
		t.Fatalf("not started: running=%v starts=%d", running, starts)
	}
	pending.Store(0)
	bridge.cycle(context.Background())
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		if running, _ := bridge.Snapshot(); !running {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if running, _ := bridge.Snapshot(); running {
		t.Fatal("worker did not stop after the queue drained")
	}
}

func TestIntakeBridgeRejectsBadEndpointAndResponse(t *testing.T) {
	if _, err := NewIntakeBridge("/bin/true", "http://example.test", "o", "r", time.Second, time.Second); err == nil {
		t.Fatal("accepted non-HTTPS endpoint")
	}
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		_, _ = w.Write([]byte(`{"ok":true,"pending":{"issues":-1}}`))
	}))
	defer server.Close()
	bridge, err := NewIntakeBridge("/bin/true", server.URL, "o", "r", time.Second, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	bridge.client = server.Client()
	if _, err := bridge.pending(context.Background()); err == nil || !strings.Contains(err.Error(), "invalid") {
		t.Fatalf("bad response error = %v", err)
	}
}
