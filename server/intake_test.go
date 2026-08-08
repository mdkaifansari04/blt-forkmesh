package mirrornode

import (
	"bufio"
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// The bridge must start the worker on a relay push, keep it running while
// events keep arriving, and SIGTERM it once idleGrace passes with no further
// pushes — all without a single HTTP GET (the relay handler below only ever
// sees the WebSocket upgrade). The socket is wired to the bridge exactly the
// way the Daemon does it.
func TestIntakeBridgeRunsWorkerOnPushedEventsOnly(t *testing.T) {
	identity := testEventIdentity(t)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/nodes/events" ||
			r.Header.Get("Upgrade") != "websocket" {
			t.Errorf("mirror sent a non-websocket request: %s %s", r.Method, r.URL)
			http.Error(w, "poll detected", http.StatusForbidden)
			return
		}
		conn := acceptTestWebSocket(t, w, r)
		defer conn.Close()
		writeTestServerText(t, conn,
			`{"type":"event","topic":"issues","repo":"forkmesh/forkmesh"}`)
		_, _ = bufio.NewReader(conn).ReadByte()
	}))
	defer server.Close()

	program := filepath.Join(t.TempDir(), "intake")
	if err := os.WriteFile(program, []byte("#!/bin/sh\ntrap 'exit 0' TERM\nwhile :; do sleep 1; done\n"), 0700); err != nil {
		t.Fatal(err)
	}
	events, err := NewEventSocket(server.URL, "mirror9", identity)
	if err != nil {
		t.Fatal(err)
	}
	bridge := NewIntakeBridge(program, "forkmesh", "forkmesh", 300*time.Millisecond)
	events.Handler = bridge.handleEvent
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go events.Run(ctx)
	go bridge.Run(ctx)

	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		if running, _ := bridge.Snapshot(); running {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if running, starts := bridge.Snapshot(); !running || starts != 1 {
		t.Fatalf("worker not started by push: running=%v starts=%d", running, starts)
	}

	// No further events: the reap tick must stop the worker after idleGrace.
	deadline = time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		if running, _ := bridge.Snapshot(); !running {
			return
		}
		time.Sleep(20 * time.Millisecond)
	}
	t.Fatal("worker did not stop after the push window drained")
}

func TestIntakeBridgeFiltersEventsByRepositoryAndTopic(t *testing.T) {
	bridge := NewIntakeBridge("/bin/true", "forkmesh", "forkmesh", time.Second)
	// No Run(): with a nil context reconcile refuses to start anything, so a
	// filtered-out event must simply leave the bridge idle.
	for _, event := range []Event{
		{Topic: "issues", Repo: "someone/else"},
		// The Daemon's own sync owns "commits"; the worker drains inboxes.
		{Topic: "commits", Repo: "forkmesh/forkmesh"},
		{Topic: "agents", Repo: "forkmesh/forkmesh"},
	} {
		bridge.handleEvent(event)
		bridge.mu.Lock()
		pending := bridge.wakePending
		bridge.mu.Unlock()
		if pending {
			t.Fatalf("event %+v marked work pending", event)
		}
	}
	for _, event := range []Event{
		{CatchUp: true},
		{Topic: "issues", Repo: "forkmesh/forkmesh"},
		{Topic: "pulls", Repo: ""},
		{Topic: "discussions", Repo: "Forkmesh/Forkmesh"},
	} {
		bridge.handleEvent(event)
		bridge.mu.Lock()
		pending := bridge.wakePending
		bridge.wakePending = false
		bridge.mu.Unlock()
		if !pending {
			t.Fatalf("event %+v did not mark work pending", event)
		}
	}
}
