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
		cmd := exec.CommandContext(ctx, "git", "--git-dir="+repo.GitDir, "fetch", "--prune", upstream,
			"refs/heads/*:refs/heads/*", "refs/tags/*:refs/tags/*")
		cmd.Env = append(os.Environ(), "GIT_TERMINAL_PROMPT=0")
		if output, err := cmd.CombinedOutput(); err == nil {
			return nil
		} else {
			failures = append(failures, fmt.Sprintf("%s: %s", upstream, boundedText(output, 240)))
		}
	}
	return fmt.Errorf("all upstreams failed: %s", strings.Join(failures, "; "))
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
