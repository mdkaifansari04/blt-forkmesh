package mirrornode

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"
)

type Daemon struct {
	config         Config
	gateway        GatewayConfig
	identity       *Identity
	supervisor     *Supervisor
	intake         *IntakeBridge
	events         *EventSocket
	startedAt      time.Time
	mu             sync.RWMutex
	lastSyncAt     time.Time
	lastSyncOK     bool
	lastSyncErr    string
	lastPublishAt  time.Time
	lastEndpointAt time.Time
	syncCount      int64
	repoStates     map[string]string
	syncRequests   chan struct{}
	server         *http.Server
}

type RuntimeStats struct {
	Goroutines int    `json:"goroutines"`
	HeapBytes  uint64 `json:"heapBytes"`
	TotalAlloc uint64 `json:"totalAllocBytes"`
}

type Status struct {
	OK             bool              `json:"ok"`
	Ready          bool              `json:"ready"`
	Node           string            `json:"node"`
	Version        string            `json:"version"`
	UptimeSeconds  int64             `json:"uptimeSeconds"`
	Processes      map[string]bool   `json:"processes"`
	Restarts       map[string]int64  `json:"restarts"`
	Repositories   map[string]string `json:"repositories"`
	LastSyncAt     string            `json:"lastSyncAt,omitempty"`
	LastSyncOK     bool              `json:"lastSyncOk"`
	LastSyncError  string            `json:"lastSyncError,omitempty"`
	LastPublishAt  string            `json:"lastPublishAt,omitempty"`
	LastEndpointAt string            `json:"lastEndpointAt,omitempty"`
	SyncCount      int64             `json:"syncCount"`
	Runtime        RuntimeStats      `json:"runtime"`
}

const endpointRenewInterval = 4 * time.Minute

func NewDaemon(cfg Config) (*Daemon, error) {
	gateway, _, err := loadGatewayConfig(cfg.GatewayConfig)
	if err != nil {
		return nil, err
	}
	identity, err := LoadIdentity(cfg.IdentityKey)
	if err != nil {
		return nil, err
	}
	if identity.PublicKey() != gateway.Node.PublicKey {
		return nil, errors.New("gateway identity does not match identityKey")
	}
	if !nodePattern.MatchString(gateway.Node.Name) {
		return nil, errors.New("gateway node name is invalid")
	}
	for _, repo := range gateway.Repositories {
		if repo.Visibility != "public" || !repo.Enabled || !filepath.IsAbs(repo.GitDir) {
			return nil, errors.New("gateway contains an unsupported repository")
		}
	}
	// The node event channel authenticates as this node's own account: the
	// relay's fan-outs notify the machine name registered in the catalog,
	// and the account's signing key IS this identity key. It carries every
	// push-driven trigger — intake wakes and "commits" sync nudges alike.
	var events *EventSocket
	if cfg.CatalogURL != "" {
		events, err = NewEventSocket(cfg.CatalogURL, gateway.Node.Name, identity)
		if err != nil {
			return nil, err
		}
	}
	var intake *IntakeBridge
	if cfg.IntakeProgram != "" {
		if events == nil {
			return nil, errors.New("intake requires catalogUrl")
		}
		intake = NewIntakeBridge(cfg.IntakeProgram, cfg.IntakeOwner,
			cfg.IntakeRepository, cfg.IntakeIdleGrace.Duration)
	}
	daemon := &Daemon{
		config:       cfg,
		gateway:      gateway,
		identity:     identity,
		supervisor:   NewSupervisor(),
		intake:       intake,
		events:       events,
		startedAt:    time.Now(),
		repoStates:   map[string]string{},
		syncRequests: make(chan struct{}, 1),
	}
	if events != nil {
		events.Handler = daemon.handleNodeEvent
	}
	if intake != nil {
		intake.onWorkerExit = daemon.requestSync
	}
	return daemon, nil
}

// handleNodeEvent fans one relay push out to everything push-driven: the
// intake bridge (which filters for its repository and inbox topics) and the
// repository sync. A catch-up event — fired once per successful (re)connect —
// wakes both, which is the entire missed-push story: there is no fallback
// poll behind the channel (docs/operations/polling-elimination.md).
func (d *Daemon) handleNodeEvent(event Event) {
	if d.intake != nil {
		d.intake.handleEvent(event)
	}
	if event.CatchUp || event.Topic == "commits" {
		d.requestSync()
	}
}

func (d *Daemon) requestSync() {
	select {
	case d.syncRequests <- struct{}{}:
	default:
	}
}

func (d *Daemon) Run(ctx context.Context, executable string) error {
	if !filepath.IsAbs(executable) {
		return errors.New("executable path must be absolute")
	}
	d.gateway.HealthSignerCommand = []string{executable, "--config", d.configPath(), "--sign-mirror-health"}
	d.gateway.RequestVerifierCommand = []string{executable, "--config", d.configPath(), "--verify-mirror-capability"}
	if _, err := writeGatewayConfig(d.config.GatewayConfig, d.gateway); err != nil {
		return err
	}
	specs := []ProcessSpec{{Name: "gateway", Program: d.config.Python, Args: []string{d.config.GatewayScript, "--config", d.config.GatewayConfig}}}
	if !d.config.DisableCloudflared {
		environment, err := tokenEnvironment(d.config.ConnectorToken)
		if err != nil {
			return err
		}
		specs = append(specs, ProcessSpec{Name: "cloudflared", Program: d.config.Cloudflared, Args: []string{"tunnel", "--no-autoupdate", "--protocol", "http2", "run"}, Env: environment, Output: io.Discard})
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", d.handleHealth)
	mux.HandleFunc("/readyz", d.handleReady)
	mux.HandleFunc("/v1/status", d.handleStatus)
	mux.HandleFunc("/v1/control/sync", d.handleSync)
	d.server = &http.Server{Addr: d.config.Listen, Handler: mux, ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 30 * time.Second}
	listener, err := net.Listen("tcp", d.config.Listen)
	if err != nil {
		return err
	}
	serverErr := make(chan error, 1)
	go func() { serverErr <- d.server.Serve(listener) }()
	// Publish health immediately, but reconcile refs and the gateway pin before
	// starting either child. A fresh node used to start the gateway with a stale
	// pin, stop it moments later, then sit through the supervisor's retry delay.
	// Preflighting once makes the first gateway process the usable one.
	d.syncOnce(ctx)
	go d.supervisor.Run(ctx, specs)
	if d.events != nil {
		go d.events.Run(ctx)
	}
	if d.intake != nil {
		go d.intake.Run(ctx)
	}
	go d.syncLoop(ctx)
	select {
	case <-ctx.Done():
		d.supervisor.Stop()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = d.server.Shutdown(shutdownCtx)
		return nil
	case err := <-serverErr:
		if errors.Is(err, http.ErrServerClosed) {
			return nil
		}
		return err
	}
}

// configPath is populated by the command through FORKMESH_MIRROR_NODE_CONFIG,
// avoiding any private material in child argv while retaining a deterministic
// helper command in the gateway configuration.
func (d *Daemon) configPath() string { return os.Getenv("FORKMESH_MIRROR_NODE_CONFIG") }

// hasExternalUpstreams reports whether any configured upstream lives outside
// the relay. The relay pushes a "commits" event the moment a source publishes
// a new head for a relay-hosted upstream, so only third-party upstreams —
// which cannot push — still justify a periodic fetch.
func (d *Daemon) hasExternalUpstreams() bool {
	catalog, err := url.Parse(d.config.CatalogURL)
	if err != nil || catalog.Host == "" {
		return true
	}
	for _, upstreams := range d.config.Upstreams {
		for _, upstream := range upstreams {
			parsed, err := url.Parse(upstream)
			if err != nil || !strings.EqualFold(parsed.Host, catalog.Host) {
				return true
			}
		}
	}
	return false
}

func (d *Daemon) syncLoop(ctx context.Context) {
	// The periodic fetch tick survives only where pushes cannot reach: an
	// upstream on a third-party git host, or a node running without the
	// catalog (and therefore without the event channel). Relay-hosted
	// upstreams sync on "commits" pushes and reconnect catch-ups instead —
	// no fallback poll (docs/operations/polling-elimination.md).
	var fetchTick <-chan time.Time
	if d.events == nil || d.hasExternalUpstreams() {
		ticker := time.NewTicker(d.config.SyncInterval.Duration)
		defer ticker.Stop()
		fetchTick = ticker.C
	}
	// The heartbeat is a liveness proof, not a poll: it reads only local
	// refs, republishes the catalog record, and renews the HTTPS endpoint
	// lease — no upstream fetch. It also publishes refs the intake worker
	// changed if the worker-exit nudge was lost.
	heartbeat := time.NewTicker(endpointRenewInterval)
	defer heartbeat.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-fetchTick:
			d.syncOnce(ctx)
		case <-heartbeat.C:
			d.publishOnce(ctx)
		case <-d.syncRequests:
			d.syncOnce(ctx)
		}
	}
}

// syncOnce fetches upstreams, reconciles the served refs, and publishes.
// publishOnce does the same minus the upstream fetch — the local-read,
// publish-and-renew liveness cycle.
func (d *Daemon) syncOnce(parent context.Context)    { d.cycle(parent, true) }
func (d *Daemon) publishOnce(parent context.Context) { d.cycle(parent, false) }

func (d *Daemon) cycle(parent context.Context, fetch bool) {
	ctx, cancel := context.WithTimeout(parent, d.config.SyncTimeout.Duration)
	defer cancel()
	changed := false
	var failures []string
	states := map[string]string{}
	seenDirs := map[string]bool{}
	for i := range d.gateway.Repositories {
		repo := &d.gateway.Repositories[i]
		key := repositoryKey(*repo)
		upstreams := d.upstreamsFor(*repo)
		if fetch && !seenDirs[repo.GitDir] {
			seenDirs[repo.GitDir] = true
			if err := syncRepository(ctx, *repo, upstreams); err != nil {
				failures = append(failures, key+": "+err.Error())
			}
		}
		digest, err := refsSHA256(ctx, repo.GitDir)
		if err != nil {
			failures = append(failures, key+": "+err.Error())
			continue
		}
		states[key] = digest
		if repo.Integrity.ExpectedRefsSHA256 != digest {
			repo.Integrity.ExpectedRefsSHA256 = digest
			changed = true
		}
	}
	if changed {
		if _, err := writeGatewayConfig(d.config.GatewayConfig, d.gateway); err != nil {
			failures = append(failures, "write gateway config: "+err.Error())
		} else {
			d.supervisor.Restart("gateway")
		}
	}
	if !d.config.DisableCatalog {
		publisher := &CatalogPublisher{URL: d.config.CatalogURL, Owner: d.config.PublishOwner, Node: d.gateway.Node.Name, Version: d.config.Version, Identity: d.identity}
		published := false
		for _, repo := range d.gateway.Repositories {
			if repo.Owner != d.config.PublishOwner {
				continue
			}
			// Advertise the canonical source URL this node mirrors so the relay
			// groups every mirror of the repo together instead of under this
			// node's own name (build_repo_mirrors_payload -> clone_target). The
			// first configured upstream is that source (e.g.
			// https://forkmesh.com/forkmesh/forkmesh).
			if upstreams := d.upstreamsFor(repo); len(upstreams) > 0 {
				publisher.CloneURL = upstreams[0]
			} else {
				publisher.CloneURL = ""
			}
			if err := publisher.Publish(ctx, repo, states[repositoryKey(repo)]); err != nil {
				failures = append(failures, "catalog "+repositoryKey(repo)+": "+err.Error())
			} else {
				published = true
				d.mu.Lock()
				d.lastPublishAt = time.Now()
				d.mu.Unlock()
			}
		}
		d.mu.RLock()
		lastEndpointAt := d.lastEndpointAt
		d.mu.RUnlock()
		running, _ := d.supervisor.Snapshot()
		publicServing := running["gateway"] &&
			(d.config.DisableCloudflared || running["cloudflared"])
		if published && publicServing &&
			(lastEndpointAt.IsZero() || time.Since(lastEndpointAt) >= endpointRenewInterval) {
			endpoint := &EndpointPublisher{
				CatalogURL: d.config.CatalogURL,
				Node:       d.gateway.Node.Name,
				BaseURL:    d.gateway.PublicOrigin,
				Version:    d.config.Version,
				Identity:   d.identity,
			}
			active, err := endpoint.Publish(ctx)
			if err != nil {
				failures = append(failures, "endpoint lease: "+err.Error())
			} else if active {
				d.mu.Lock()
				d.lastEndpointAt = time.Now()
				d.mu.Unlock()
			}
		}
	}
	d.mu.Lock()
	d.lastSyncAt = time.Now()
	d.syncCount++
	d.lastSyncOK = len(failures) == 0
	d.lastSyncErr = boundedText([]byte(strings.Join(failures, "; ")), 1000)
	d.repoStates = states
	d.mu.Unlock()
	if len(failures) > 0 {
		log.Printf("mirror cycle completed with warnings: %s", strings.Join(failures, "; "))
	}
}

func (d *Daemon) upstreamsFor(repo Repository) []string {
	if values := d.config.Upstreams[repositoryKey(repo)]; len(values) > 0 {
		return values
	}
	for _, candidate := range d.gateway.Repositories {
		if candidate.GitDir == repo.GitDir {
			if values := d.config.Upstreams[repositoryKey(candidate)]; len(values) > 0 {
				return values
			}
		}
	}
	return nil
}

func (d *Daemon) ready() bool {
	connection, err := net.DialTimeout("tcp", d.config.GatewayProbe, 750*time.Millisecond)
	if err == nil {
		connection.Close()
	}
	d.mu.RLock()
	syncOK := d.lastSyncOK
	last := d.lastSyncAt
	d.mu.RUnlock()
	running, _ := d.supervisor.Snapshot()
	return err == nil && running["gateway"] && syncOK && !last.IsZero() && time.Since(last) < 2*d.config.SyncInterval.Duration+d.config.SyncTimeout.Duration
}

func (d *Daemon) status() Status {
	running, restarts := d.supervisor.Snapshot()
	if d.intake != nil {
		intakeRunning, intakeStarts := d.intake.Snapshot()
		running["intake"] = intakeRunning
		restarts["intake"] = intakeStarts
	}
	ready := d.ready()
	d.mu.RLock()
	defer d.mu.RUnlock()
	var memory runtime.MemStats
	runtime.ReadMemStats(&memory)
	status := Status{
		OK:            true,
		Ready:         ready,
		Node:          d.gateway.Node.Name,
		Version:       d.config.Version,
		UptimeSeconds: int64(time.Since(d.startedAt).Seconds()),
		Processes:     running,
		Restarts:      restarts,
		Repositories:  copyMap(d.repoStates),
		LastSyncOK:    d.lastSyncOK,
		LastSyncError: d.lastSyncErr,
		SyncCount:     d.syncCount,
		Runtime: RuntimeStats{
			Goroutines: runtime.NumGoroutine(),
			HeapBytes:  memory.HeapAlloc,
			TotalAlloc: memory.TotalAlloc,
		},
	}
	if !d.lastSyncAt.IsZero() {
		status.LastSyncAt = d.lastSyncAt.UTC().Format(time.RFC3339)
	}
	if !d.lastPublishAt.IsZero() {
		status.LastPublishAt = d.lastPublishAt.UTC().Format(time.RFC3339)
	}
	if !d.lastEndpointAt.IsZero() {
		status.LastEndpointAt = d.lastEndpointAt.UTC().Format(time.RFC3339)
	}
	return status
}

func (d *Daemon) handleHealth(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "node": d.gateway.Node.Name})
}
func (d *Daemon) handleReady(w http.ResponseWriter, _ *http.Request) {
	status := http.StatusOK
	if !d.ready() {
		status = http.StatusServiceUnavailable
	}
	writeJSON(w, status, d.status())
}
func (d *Daemon) handleStatus(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, d.status())
}

func (d *Daemon) handleSync(w http.ResponseWriter, request *http.Request) {
	if request.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeJSON(w, http.StatusMethodNotAllowed,
			map[string]any{"ok": false, "error": "POST required"})
		return
	}
	select {
	case d.syncRequests <- struct{}{}:
		writeJSON(w, http.StatusAccepted, map[string]any{"ok": true, "queued": true})
	default:
		writeJSON(w, http.StatusAccepted, map[string]any{
			"ok": true, "queued": false, "message": "sync already requested",
		})
	}
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
func copyMap(source map[string]string) map[string]string {
	out := make(map[string]string, len(source))
	for k, v := range source {
		out[k] = v
	}
	return out
}

func ValidateRuntime(cfg Config) error {
	paths := map[string]string{"gateway script": cfg.GatewayScript, "python": cfg.Python, "identity": cfg.IdentityKey}
	if cfg.IntakeProgram != "" {
		paths["intake program"] = cfg.IntakeProgram
	}
	for label, path := range paths {
		if info, err := os.Stat(path); err != nil || !info.Mode().IsRegular() {
			return fmt.Errorf("%s is unavailable", label)
		}
	}
	if _, err := exec.LookPath("git"); err != nil {
		return errors.New("git is unavailable")
	}
	return nil
}
