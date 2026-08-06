package mirrornode

import (
	"context"
	"io"
	"log"
	"os"
	"os/exec"
	"strings"
	"sync"
	"syscall"
	"time"
)

// IntakeBridge keeps the legacy Qt materializer off the node unless the relay
// actually has collaboration work. The Go service owns the event channel and
// lifecycle; the compatibility binary is a short-lived worker, not a resident
// daemon.
//
// The bridge is purely push-driven: it holds one EventSocket to the relay's
// per-owner ForkMeshNodes Durable Object and starts the worker when an
// "issues"/"pulls"/"discussions" event lands for the intake repository. It
// never calls GET /api/repo/*/pending — that poll (five seconds fleet-wide)
// was the busiest endpoint on the relay while reading a ten-minute edge
// cache, and it is gone with no fallback poll behind it. Missed-push safety
// comes from the socket itself: every reconnect emits one catch-up wake, so
// work queued while the channel was down is drained as soon as it is back.
type IntakeBridge struct {
	program   string
	repoKey   string // "owner/repository", lowercased, for event filtering
	idleGrace time.Duration
	events    *EventSocket

	mu          sync.Mutex
	ctx         context.Context
	command     *exec.Cmd
	lastWake    time.Time
	wakePending bool
	stopping    bool
	starts      int64
}

func NewIntakeBridge(program, catalogURL, owner, repository, node string, identity *Identity, idleGrace time.Duration) (*IntakeBridge, error) {
	events, err := NewEventSocket(catalogURL, node, identity)
	if err != nil {
		return nil, err
	}
	bridge := &IntakeBridge{
		program:   program,
		repoKey:   strings.ToLower(owner + "/" + repository),
		idleGrace: idleGrace,
		events:    events,
	}
	events.Handler = bridge.handleEvent
	return bridge, nil
}

func (b *IntakeBridge) Run(ctx context.Context) {
	b.mu.Lock()
	b.ctx = ctx
	b.mu.Unlock()
	go b.events.Run(ctx)
	// The ticker is a local reap clock (stop the worker once the grace window
	// after the last push has passed), not a network poll: reconcile sends no
	// requests. Starts happen inline in handleEvent, so a push still reaches
	// the worker in milliseconds.
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	defer b.stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			b.reconcile()
		}
	}
}

// handleEvent is the EventSocket push callback. A catch-up wake (fired once
// per successful connect) and a frame for the intake repository both mark
// work pending; frames for other repositories this account owns are ignored.
func (b *IntakeBridge) handleEvent(event Event) {
	if !event.CatchUp && event.Repo != "" &&
		!strings.EqualFold(event.Repo, b.repoKey) {
		return
	}
	b.mu.Lock()
	b.lastWake = time.Now()
	b.wakePending = true
	b.mu.Unlock()
	b.reconcile()
}

func (b *IntakeBridge) reconcile() {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.wakePending && b.command == nil && b.ctx != nil && b.ctx.Err() == nil {
		command := exec.CommandContext(b.ctx, b.program, "--headless")
		command.Env = append(os.Environ(), "FORKMESH_EXTERNAL_MIRROR_NODE=1", "QT_QPA_PLATFORM=offscreen")
		command.Stdout, command.Stderr = io.Discard, os.Stderr
		if err := command.Start(); err != nil {
			log.Printf("intake worker could not start: %v", err)
			return
		}
		b.command, b.stopping, b.wakePending = command, false, false
		b.starts++
		go b.wait(command)
		return
	}
	// A running (non-stopping) worker serves any wake by having its grace
	// window extended above; only a wake that lands while the worker is
	// already shutting down stays pending so wait() can restart it.
	if b.command != nil && !b.stopping {
		b.wakePending = false
	}
	// Without the pending counter there is no queue-drained signal, so the
	// worker runs for idleGrace after the LAST push (each new event extends
	// the window) and is then asked to stop. A drain still in flight ends
	// gracefully — the worker treats SIGTERM as "finish the current item and
	// exit" — and anything it did not reach triggers new events or the next
	// reconnect catch-up.
	if b.command != nil && !b.stopping && time.Since(b.lastWake) >= b.idleGrace {
		b.stopping = true
		_ = b.command.Process.Signal(syscall.SIGTERM)
	}
}

func (b *IntakeBridge) wait(command *exec.Cmd) {
	err := command.Wait()
	b.mu.Lock()
	if b.command == command {
		b.command, b.stopping = nil, false
	}
	b.mu.Unlock()
	if err != nil {
		log.Printf("intake worker exited: %v", err)
	}
	// A wake that arrived while the worker was shutting down starts a fresh
	// one now rather than waiting out the reap tick.
	b.reconcile()
}

func (b *IntakeBridge) stop() {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.command != nil && b.command.Process != nil {
		b.stopping = true
		_ = b.command.Process.Signal(syscall.SIGTERM)
	}
}

func (b *IntakeBridge) Snapshot() (bool, int64) {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.command != nil, b.starts
}
