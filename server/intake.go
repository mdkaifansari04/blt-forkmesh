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

// IntakeBridge runs the compatibility materializer only while collaboration
// work is pending. Relay pushes and reconnect catch-ups drive every wake.
type IntakeBridge struct {
	program      string
	repoKey      string // "owner/repository", lowercased, for event filtering
	idleGrace    time.Duration
	onWorkerExit func()

	mu          sync.Mutex
	ctx         context.Context
	command     *exec.Cmd
	lastWake    time.Time
	wakePending bool
	stopping    bool
	starts      int64
}

func NewIntakeBridge(program, owner, repository string, idleGrace time.Duration) *IntakeBridge {
	return &IntakeBridge{
		program:   program,
		repoKey:   strings.ToLower(owner + "/" + repository),
		idleGrace: idleGrace,
	}
}

func (b *IntakeBridge) Run(ctx context.Context) {
	b.mu.Lock()
	b.ctx = ctx
	b.mu.Unlock()
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

func (b *IntakeBridge) handleEvent(event Event) {
	if !event.CatchUp {
		if event.Repo != "" && !strings.EqualFold(event.Repo, b.repoKey) {
			return
		}
		switch event.Topic {
		case "", "issues", "pulls", "discussions":
		default:
			return
		}
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
	exit := b.onWorkerExit
	b.mu.Unlock()
	if err != nil {
		log.Printf("intake worker exited: %v", err)
	}
	if exit != nil {
		exit()
	}
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
