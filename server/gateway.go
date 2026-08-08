package mirrornode

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

type GatewayConfig struct {
	SchemaVersion int `json:"schemaVersion"`
	Node          struct {
		Name      string `json:"name"`
		PublicKey string `json:"publicKey"`
	} `json:"node"`
	RouterPublicKey        string          `json:"routerPublicKey"`
	PublicOrigin           string          `json:"publicOrigin"`
	Listen                 json.RawMessage `json:"listen"`
	ManifestPath           string          `json:"manifestPath"`
	RequestVerifierCommand []string        `json:"requestVerifierCommand"`
	HealthSignerCommand    []string        `json:"healthSignerCommand"`
	Repositories           []Repository    `json:"repositories"`
	Limits                 json.RawMessage `json:"limits"`
	PrivateReplicaStore    string          `json:"privateReplicaStore,omitempty"`
}

type Repository struct {
	Owner      string   `json:"owner"`
	Name       string   `json:"name"`
	Visibility string   `json:"visibility"`
	Enabled    bool     `json:"enabled"`
	GitDir     string   `json:"gitDir"`
	Operations []string `json:"operations"`
	Integrity  struct {
		ExpectedRefsSHA256 string `json:"expectedRefsSha256"`
	} `json:"integrity"`
}

func loadGatewayConfig(path string) (GatewayConfig, []byte, error) {
	var cfg GatewayConfig
	info, err := os.Lstat(path)
	if err != nil || !info.Mode().IsRegular() || info.Mode()&os.ModeSymlink != 0 || info.Size() > 4<<20 {
		return cfg, nil, errors.New("gateway config must be a small regular file")
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return cfg, nil, err
	}
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&cfg); err != nil {
		return cfg, nil, err
	}
	if cfg.SchemaVersion != 1 || cfg.Node.Name == "" || cfg.Node.PublicKey == "" || len(cfg.Repositories) == 0 {
		return cfg, nil, errors.New("gateway config is incomplete")
	}
	return cfg, raw, nil
}

// LoadGatewayConfigForCommand exposes the public, secret-free gateway identity
// needed by the two bounded helper modes.
func LoadGatewayConfigForCommand(path string) (GatewayConfig, []byte, error) {
	return loadGatewayConfig(path)
}

func refsSHA256(ctx context.Context, gitDir string) (string, error) {
	cmd := exec.CommandContext(ctx, "git", "--git-dir="+gitDir, "for-each-ref", "--format=%(objectname) %(refname)", "refs/heads", "refs/tags")
	out, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("read stable refs: %w", err)
	}
	var lines []string
	for _, line := range strings.Split(string(out), "\n") {
		line = strings.TrimSpace(line)
		parts := strings.SplitN(line, " ", 2)
		if len(parts) != 2 || strings.HasSuffix(line, "^{}") ||
			(!strings.HasPrefix(parts[1], "refs/heads/") && !strings.HasPrefix(parts[1], "refs/tags/")) {
			continue
		}
		lines = append(lines, line)
	}
	sort.Slice(lines, func(i, j int) bool {
		return strings.SplitN(lines[i], " ", 2)[1] < strings.SplitN(lines[j], " ", 2)[1]
	})
	digest := sha256.Sum256([]byte(strings.Join(lines, "\n")))
	return hex.EncodeToString(digest[:]), nil
}

func syncRepository(ctx context.Context, repo Repository, upstreams []string) error {
	if len(upstreams) == 0 {
		return errors.New("no upstream configured")
	}
	var failures []string
	for _, upstream := range upstreams {
		if strings.TrimSpace(upstream) == "" {
			continue
		}
		if _, err := os.Stat(repo.GitDir); errors.Is(err, os.ErrNotExist) {
			if err := cloneMirrorRepository(ctx, upstream, repo.GitDir); err == nil {
				return nil
			} else {
				failures = append(failures, fmt.Sprintf("%s: %s", upstream, err))
				continue
			}
		}
		// A public upstream is round-robin: the selected peer can briefly be
		// behind this node. Fetch into an isolated namespace first so one stale
		// response cannot rewind main, delete a new tag, or make the whole cycle
		// fail with a non-fast-forward rejection.
		cmd := exec.CommandContext(ctx, "git", "--git-dir="+repo.GitDir, "fetch", "--prune", upstream,
			"+refs/heads/*:refs/forkmesh/upstream/heads/*",
			"+refs/tags/*:refs/forkmesh/upstream/tags/*")
		cmd.Env = append(os.Environ(), "GIT_TERMINAL_PROMPT=0")
		if output, err := cmd.CombinedOutput(); err == nil {
			if err := reconcileUpstreamRefs(ctx, repo.GitDir); err != nil {
				failures = append(failures, fmt.Sprintf("%s: %s", upstream, err))
				continue
			}
			return nil
		} else {
			failures = append(failures, fmt.Sprintf("%s: %s", upstream, boundedText(output, 240)))
		}
	}
	return fmt.Errorf("all upstreams failed: %s", strings.Join(failures, "; "))
}

func cloneMirrorRepository(ctx context.Context, upstream, destination string) error {
	parent := filepath.Dir(destination)
	if err := os.MkdirAll(parent, 0700); err != nil {
		return err
	}
	temporary, err := os.MkdirTemp(parent, ".clone-*")
	if err != nil {
		return err
	}
	os.Remove(temporary)
	defer os.RemoveAll(temporary)
	command := exec.CommandContext(ctx, "git", "clone", "--mirror", "--", upstream, temporary)
	command.Env = append(os.Environ(), "GIT_TERMINAL_PROMPT=0")
	if output, err := command.CombinedOutput(); err != nil {
		return fmt.Errorf("clone failed: %s", boundedText(output, 240))
	}
	if err := os.Rename(temporary, destination); err != nil {
		return err
	}
	return nil
}

func reconcileUpstreamRefs(ctx context.Context, gitDir string) error {
	defer clearRefNamespace(context.Background(), gitDir, "refs/forkmesh/upstream")
	cmd := exec.CommandContext(ctx, "git", "--git-dir="+gitDir, "for-each-ref",
		"--format=%(refname) %(objectname)", "refs/forkmesh/upstream/heads",
		"refs/forkmesh/upstream/tags")
	raw, err := cmd.Output()
	if err != nil {
		return fmt.Errorf("list staged refs: %w", err)
	}
	for _, line := range strings.Split(strings.TrimSpace(string(raw)), "\n") {
		fields := strings.Fields(line)
		if len(fields) != 2 {
			continue
		}
		staged, incoming := fields[0], fields[1]
		local := ""
		switch {
		case strings.HasPrefix(staged, "refs/forkmesh/upstream/heads/"):
			local = "refs/heads/" + strings.TrimPrefix(staged, "refs/forkmesh/upstream/heads/")
		case strings.HasPrefix(staged, "refs/forkmesh/upstream/tags/"):
			local = "refs/tags/" + strings.TrimPrefix(staged, "refs/forkmesh/upstream/tags/")
		default:
			continue
		}
		current := gitRefValue(ctx, gitDir, local)
		if current == incoming || (strings.HasPrefix(local, "refs/tags/") && current != "") {
			continue // tags are immutable once observed
		}
		chosen := incoming
		if current != "" && gitIsAncestor(ctx, gitDir, incoming, current) {
			chosen = current // the selected round-robin peer is behind us
		} else if current != "" && !gitIsAncestor(ctx, gitDir, current, incoming) {
			// Diverged force-pushes converge deterministically on the newer commit;
			// an object-id tie-break makes simultaneous rewrites choose identically.
			currentTime := gitCommitTime(ctx, gitDir, current)
			incomingTime := gitCommitTime(ctx, gitDir, incoming)
			if currentTime > incomingTime || (currentTime == incomingTime && current > incoming) {
				chosen = current
			}
		}
		if chosen == current {
			continue
		}
		args := []string{"--git-dir=" + gitDir, "update-ref", local, chosen}
		if current != "" {
			args = append(args, current)
		}
		if output, err := exec.CommandContext(ctx, "git", args...).CombinedOutput(); err != nil {
			return fmt.Errorf("update %s: %s", local, boundedText(output, 160))
		}
	}
	return nil
}

func gitRefValue(ctx context.Context, gitDir, ref string) string {
	out, err := exec.CommandContext(ctx, "git", "--git-dir="+gitDir,
		"rev-parse", "--verify", "-q", ref).Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

func gitIsAncestor(ctx context.Context, gitDir, older, newer string) bool {
	return exec.CommandContext(ctx, "git", "--git-dir="+gitDir,
		"merge-base", "--is-ancestor", older, newer).Run() == nil
}

func gitCommitTime(ctx context.Context, gitDir, oid string) int64 {
	out, err := exec.CommandContext(ctx, "git", "--git-dir="+gitDir,
		"show", "-s", "--format=%ct", oid).Output()
	if err != nil {
		return 0
	}
	value, _ := strconv.ParseInt(strings.TrimSpace(string(out)), 10, 64)
	return value
}

func clearRefNamespace(ctx context.Context, gitDir, namespace string) {
	out, _ := exec.CommandContext(ctx, "git", "--git-dir="+gitDir,
		"for-each-ref", "--format=%(refname)", namespace).Output()
	for _, ref := range strings.Fields(string(out)) {
		_ = exec.CommandContext(ctx, "git", "--git-dir="+gitDir,
			"update-ref", "-d", ref).Run()
	}
}

func writeGatewayConfig(path string, cfg GatewayConfig) (bool, error) {
	raw, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return false, err
	}
	raw = append(raw, '\n')
	previous, _ := os.ReadFile(path)
	if bytes.Equal(previous, raw) {
		return false, nil
	}
	dir := filepath.Dir(path)
	tmp, err := os.CreateTemp(dir, ".gateway-config-*")
	if err != nil {
		return false, err
	}
	tmpName := tmp.Name()
	defer os.Remove(tmpName)
	if err := tmp.Chmod(0600); err != nil {
		tmp.Close()
		return false, err
	}
	if _, err := tmp.Write(raw); err != nil {
		tmp.Close()
		return false, err
	}
	if err := tmp.Sync(); err != nil {
		tmp.Close()
		return false, err
	}
	if err := tmp.Close(); err != nil {
		return false, err
	}
	if err := os.Rename(tmpName, path); err != nil {
		return false, err
	}
	return true, nil
}

func repositoryKey(repo Repository) string { return strings.ToLower(repo.Owner + "/" + repo.Name) }

func boundedText(raw []byte, maximum int) string {
	value := strings.Join(strings.Fields(string(raw)), " ")
	if len(value) > maximum {
		value = value[:maximum]
	}
	return value
}
