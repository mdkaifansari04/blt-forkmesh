package mirrornode

import (
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"strings"
	"sync"
	"syscall"
	"time"
)

type ProcessSpec struct {
	Name    string
	Program string
	Args    []string
	Env     []string
	Output  io.Writer
}

type Supervisor struct {
	mu       sync.RWMutex
	running  map[string]bool
	restarts map[string]int64
	cancel   context.CancelFunc
	restart  map[string]chan struct{}
}

func NewSupervisor() *Supervisor {
	return &Supervisor{running: map[string]bool{}, restarts: map[string]int64{}, restart: map[string]chan struct{}{}}
}

func (s *Supervisor) Run(ctx context.Context, specs []ProcessSpec) {
	ctx, s.cancel = context.WithCancel(ctx)
	for _, spec := range specs {
		spec := spec
		go s.runOne(ctx, spec)
	}
	<-ctx.Done()
}

func (s *Supervisor) runOne(ctx context.Context, spec ProcessSpec) {
	for ctx.Err() == nil {
		cmd := exec.CommandContext(ctx, spec.Program, spec.Args...)
		cmd.Env = spec.Env
		if len(cmd.Env) == 0 {
			cmd.Env = os.Environ()
		}
		writer := spec.Output
		if writer == nil {
			writer = os.Stderr
		}
		cmd.Stdout, cmd.Stderr = writer, writer
		restart := make(chan struct{}, 1)
		s.mu.Lock()
		s.restart[spec.Name] = restart
		s.mu.Unlock()
		s.setRunning(spec.Name, true)
		if err := cmd.Start(); err != nil {
			s.setRunning(spec.Name, false)
			log.Printf("%s could not start: %v", spec.Name, err)
			select {
			case <-ctx.Done():
				return
			case <-time.After(5 * time.Second):
				continue
			}
		}
		done := make(chan error, 1)
		go func() { done <- cmd.Wait() }()
		var err error
		select {
		case err = <-done:
		case <-restart:
			log.Printf("restarting %s after configuration change", spec.Name)
			_ = cmd.Process.Signal(syscall.SIGTERM)
			select {
			case err = <-done:
			case <-time.After(5 * time.Second):
				_ = cmd.Process.Kill()
				err = <-done
			}
		case <-ctx.Done():
			_ = cmd.Process.Signal(syscall.SIGTERM)
			select {
			case <-done:
			case <-time.After(5 * time.Second):
				_ = cmd.Process.Kill()
				<-done
			}
			s.setRunning(spec.Name, false)
			return
		}
		s.setRunning(spec.Name, false)
		if ctx.Err() != nil {
			return
		}
		s.mu.Lock()
		s.restarts[spec.Name]++
		s.mu.Unlock()
		log.Printf("%s exited: %v; retrying in 5s", spec.Name, err)
		select {
		case <-ctx.Done():
			return
		case <-time.After(5 * time.Second):
		}
	}
}

func (s *Supervisor) Restart(name string) bool {
	s.mu.RLock()
	ch := s.restart[name]
	s.mu.RUnlock()
	if ch == nil {
		return false
	}
	select {
	case ch <- struct{}{}:
		return true
	default:
		return false
	}
}

func (s *Supervisor) setRunning(name string, value bool) {
	s.mu.Lock()
	s.running[name] = value
	s.mu.Unlock()
}

func (s *Supervisor) Snapshot() (map[string]bool, map[string]int64) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	running := make(map[string]bool, len(s.running))
	restarts := make(map[string]int64, len(s.restarts))
	for key, value := range s.running {
		running[key] = value
	}
	for key, value := range s.restarts {
		restarts[key] = value
	}
	return running, restarts
}

func (s *Supervisor) Stop() {
	if s.cancel != nil {
		s.cancel()
	}
}

func tokenEnvironment(path string) ([]string, error) {
	info, err := os.Lstat(path)
	if err != nil || !info.Mode().IsRegular() || info.Mode()&os.ModeSymlink != 0 || info.Size() <= 0 || info.Size() > 16<<10 || info.Mode().Perm()&0077 != 0 {
		return nil, fmt.Errorf("connector token must be a small owner-only regular file")
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	token := string(raw)
	for len(token) > 0 && (token[len(token)-1] == '\n' || token[len(token)-1] == '\r') {
		token = token[:len(token)-1]
	}
	if token == "" || strings.ContainsAny(token, "\r\n") {
		return nil, fmt.Errorf("connector token is invalid")
	}
	environment := os.Environ()
	filtered := environment[:0]
	for _, value := range environment {
		upper := strings.ToUpper(value)
		if strings.HasPrefix(upper, "CLOUDFLARE_API_TOKEN=") || strings.HasPrefix(upper, "CF_API_TOKEN=") || strings.HasPrefix(upper, "CLOUDFLARE_TOKEN=") || strings.HasPrefix(upper, "CF_TOKEN=") || strings.HasPrefix(upper, "TUNNEL_TOKEN=") {
			continue
		}
		filtered = append(filtered, value)
	}
	return append(filtered, "TUNNEL_TOKEN="+token), nil
}
