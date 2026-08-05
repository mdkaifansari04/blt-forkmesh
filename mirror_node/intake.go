package mirrornode

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path"
	"sync"
	"syscall"
	"time"
)

// IntakeBridge keeps the legacy Qt materializer off the node unless the relay
// actually has collaboration work. The Go service owns polling and lifecycle;
// the compatibility binary is a short-lived worker, not a resident daemon.
type IntakeBridge struct {
	program      string
	pendingURL   string
	pollInterval time.Duration
	idleGrace    time.Duration
	client       *http.Client

	mu       sync.Mutex
	command  *exec.Cmd
	started  time.Time
	stopping bool
	starts   int64
}

func NewIntakeBridge(program, catalogURL, owner, repository string, pollInterval, idleGrace time.Duration) (*IntakeBridge, error) {
	base, err := url.Parse(catalogURL)
	if err != nil || base.Scheme != "https" || base.Host == "" {
		return nil, errors.New("catalogUrl must be an absolute HTTPS URL")
	}
	base.Path = path.Join("/api/repo", owner, repository, "pending")
	base.RawQuery, base.Fragment = "", ""
	return &IntakeBridge{program: program, pendingURL: base.String(), pollInterval: pollInterval, idleGrace: idleGrace, client: &http.Client{Timeout: 10 * time.Second}}, nil
}

func (b *IntakeBridge) Run(ctx context.Context) {
	b.cycle(ctx)
	ticker := time.NewTicker(b.pollInterval)
	defer ticker.Stop()
	defer b.stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			b.cycle(ctx)
		}
	}
}

func (b *IntakeBridge) cycle(ctx context.Context) {
	pending, err := b.pending(ctx)
	if err != nil {
		log.Printf("intake pending probe failed: %v", err)
		return
	}
	b.reconcile(ctx, pending)
}

func (b *IntakeBridge) pending(ctx context.Context) (int, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, b.pendingURL, nil)
	if err != nil {
		return 0, err
	}
	req.Header.Set("Accept", "application/json")
	response, err := b.client.Do(req)
	if err != nil {
		return 0, err
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return 0, fmt.Errorf("HTTP %d", response.StatusCode)
	}
	var payload struct {
		OK      bool `json:"ok"`
		Pending struct {
			Issues      int `json:"issues"`
			Pulls       int `json:"pulls"`
			Discussions int `json:"discussions"`
		} `json:"pending"`
	}
	decoder := json.NewDecoder(io.LimitReader(response.Body, 16<<10))
	if err := decoder.Decode(&payload); err != nil || !payload.OK ||
		payload.Pending.Issues < 0 || payload.Pending.Pulls < 0 || payload.Pending.Discussions < 0 {
		return 0, errors.New("invalid pending response")
	}
	return payload.Pending.Issues + payload.Pending.Pulls + payload.Pending.Discussions, nil
}

func (b *IntakeBridge) reconcile(ctx context.Context, pending int) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if pending > 0 && b.command == nil {
		command := exec.CommandContext(ctx, b.program, "--headless")
		command.Env = append(os.Environ(), "FORKMESH_EXTERNAL_MIRROR_NODE=1", "QT_QPA_PLATFORM=offscreen")
		command.Stdout, command.Stderr = io.Discard, os.Stderr
		if err := command.Start(); err != nil {
			log.Printf("intake worker could not start: %v", err)
			return
		}
		b.command, b.started, b.stopping = command, time.Now(), false
		b.starts++
		go b.wait(command)
		return
	}
	if pending == 0 && b.command != nil && !b.stopping && time.Since(b.started) >= b.idleGrace {
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
