package mirrornode

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

type CatalogPublisher struct {
	URL     string
	Owner   string
	Node    string
	Version string
	// CloneURL identifies the canonical source used to group its mirrors.
	CloneURL string
	Identity *Identity
	Client   *http.Client
}

// EndpointPublisher renews the account-bound HTTPS routing lease.
type EndpointPublisher struct {
	CatalogURL string
	Node       string
	BaseURL    string
	Version    string
	Identity   *Identity
	Client     *http.Client
}

func (p *EndpointPublisher) Publish(ctx context.Context) (bool, error) {
	endpointURL, err := mirrorEndpointURL(p.CatalogURL)
	if err != nil {
		return false, err
	}
	issuedAt := time.Now().UnixMilli()
	message := strings.Join([]string{
		"forkmesh-https-endpoint-v1",
		p.Node,
		p.BaseURL,
		p.Identity.PublicKey(),
		strconv.FormatInt(issuedAt, 10),
	}, "\n")
	payload := map[string]any{
		"node": p.Node, "baseUrl": p.BaseURL,
		"publicKey": p.Identity.PublicKey(), "issuedAt": issuedAt,
		"signature": p.Identity.Sign([]byte(message)),
	}
	body, err := json.Marshal(payload)
	if err != nil {
		return false, err
	}
	req, err := http.NewRequestWithContext(
		ctx, http.MethodPost, endpointURL, bytes.NewReader(body))
	if err != nil {
		return false, err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")
	req.Header.Set("User-Agent", "ForkMesh-Mirror-Node/"+p.Version)
	client := p.Client
	if client == nil {
		client = &http.Client{Timeout: 30 * time.Second}
	}
	response, err := client.Do(req)
	if err != nil {
		return false, err
	}
	defer response.Body.Close()
	raw, err := io.ReadAll(io.LimitReader(response.Body, 64<<10))
	if err != nil {
		return false, err
	}
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return false, fmt.Errorf("endpoint registration returned HTTP %d: %s",
			response.StatusCode, boundedText(raw, 240))
	}
	var result struct {
		OK      bool   `json:"ok"`
		Node    string `json:"node"`
		BaseURL string `json:"baseUrl"`
		Health  string `json:"health"`
	}
	if err := json.Unmarshal(raw, &result); err != nil {
		return false, errors.New("endpoint registration returned invalid JSON")
	}
	if !result.OK || result.Node != p.Node || result.BaseURL != p.BaseURL ||
		(result.Health != "active" && result.Health != "pending") {
		return false, errors.New("endpoint registration returned invalid lease state")
	}
	return result.Health == "active", nil
}

func mirrorEndpointURL(catalogURL string) (string, error) {
	parsed, err := url.Parse(catalogURL)
	if err != nil || parsed.Scheme == "" || parsed.Host == "" ||
		(parsed.Scheme != "https" && parsed.Scheme != "http") {
		return "", errors.New("catalogUrl is not an absolute HTTP URL")
	}
	parsed.Path = "/api/mirrors/https"
	parsed.RawPath = ""
	parsed.RawQuery = ""
	parsed.Fragment = ""
	return parsed.String(), nil
}

func (p *CatalogPublisher) Publish(ctx context.Context, repo Repository, stateHash string) error {
	record, err := p.record(ctx, repo, stateHash)
	if err != nil {
		return err
	}
	body, err := json.Marshal(record)
	if err != nil {
		return err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, p.URL, bytes.NewReader(body))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")
	req.Header.Set("User-Agent", "ForkMesh-Mirror-Node/"+p.Version)
	client := p.Client
	if client == nil {
		client = &http.Client{Timeout: 30 * time.Second}
	}
	response, err := client.Do(req)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		body, _ := io.ReadAll(io.LimitReader(response.Body, 1024))
		return fmt.Errorf("catalog returned HTTP %d: %s", response.StatusCode,
			boundedText(body, 240))
	}
	return nil
}

func (p *CatalogPublisher) record(ctx context.Context, repo Repository, stateHash string) (map[string]any, error) {
	branch := gitValue(ctx, repo.GitDir, "symbolic-ref", "--short", "HEAD")
	commit := gitValue(ctx, repo.GitDir, "rev-parse", "HEAD")
	if branch == "" || commit == "" || !digestPattern.MatchString(stateHash) {
		return nil, fmt.Errorf("repository head or state hash unavailable")
	}
	now := strconv.FormatInt(time.Now().UnixMilli(), 10)
	rootCommit := firstLine(gitValue(ctx, repo.GitDir, "rev-list", "--max-parents=0", "HEAD"))
	commitCount := gitValue(ctx, repo.GitDir, "rev-list", "--count", "HEAD")
	branchCount := countLines(gitValue(ctx, repo.GitDir, "for-each-ref", "--format=%(refname)", "refs/heads"))
	metadataPaths := gitLines(ctx, repo.GitDir, "ls-tree", "-r", "--name-only", "HEAD", "--", ".forkmesh")
	paths := make([]string, 0, len(metadataPaths))
	for _, path := range metadataPaths {
		if !strings.HasPrefix(path, ".forkmesh/pulls/") {
			paths = append(paths, path)
		}
	}
	pullRef := "HEAD"
	if gitValue(ctx, repo.GitDir, "rev-parse", "--verify", "-q", "refs/heads/forkmesh/pulls^{commit}") != "" {
		pullRef = "refs/heads/forkmesh/pulls"
	}
	paths = append(paths, gitLines(ctx, repo.GitDir, "ls-tree", "-r", "--name-only", pullRef, "--", ".forkmesh/pulls")...)
	issueCount, issueMax, pullCount, discussionCount, artifacts := catalogPathCounts(paths)
	size := directorySize(repo.GitDir)
	memUsed, memTotal := memoryUsage()
	diskUsed, diskTotal := diskUsage(repo.GitDir)
	record := map[string]any{
		"owner": p.Owner, "name": repo.Name, "visibility": "public",
		"mirrorEncryption": "", "opaqueRepoId": "", "keyEpoch": 0,
		"encryptedManifestHash": "", "encryptedManifestSig": "",
		"sizeBytes": size, "description": "",
		"logoMetadata": map[string]any{"description": "", "languages": map[string]int64{}, "topics": []string{}, "fileStructure": []string{}, "frameworks": []string{}, "projectCategory": ""},
		"cloneUrl":     p.CloneURL, "solana": "", "channel": "#" + p.Owner + "-" + repo.Name,
		"hostedSince": "", "lastSync": now, "updatedAt": now,
		"rootCommit": rootCommit, "source": "remote-clone", "commit": commit, "branch": branch,
		"issueCount": strconv.Itoa(issueCount), "issueMaxNumber": strconv.Itoa(issueMax),
		"commitCount": commitCount, "branchCount": strconv.Itoa(branchCount),
		"pullCount": strconv.Itoa(pullCount), "discussionCount": strconv.Itoa(discussionCount),
		"activityWeeks": make([]int, 52), "worktreeCount": "0", "artifactCount": strconv.Itoa(artifacts),
		"platform": runtime.GOOS, "version": p.Version, "nodeId": "",
		"clonesServed": "0", "websiteServed": "0", "maintainer": p.Identity.PublicKey(),
		"signature": "", "stateHash": stateHash, "stateSig": "",
		"cpuPercent": cpuPercent(), "memUsedBytes": memUsed, "memTotalBytes": memTotal,
		"diskUsedBytes": diskUsed, "diskTotalBytes": diskTotal,
		"machineName": p.Node, "runtimeMode": "headless",
	}
	statePayload := "forkmesh-repostate-v1\n" + p.Owner + "\n" + repo.Name + "\n" + stateHash + "\n" + now
	record["stateSig"] = p.Identity.Sign([]byte(statePayload))
	signed := make(map[string]any, len(record)-1)
	for key, value := range record {
		if key != "signature" {
			signed[key] = value
		}
	}
	canonical, err := json.Marshal(signed)
	if err != nil {
		return nil, err
	}
	digest := sha256.Sum256(canonical)
	record["catalogSigVersion"] = 2
	record["catalogSig"] = p.Identity.Sign([]byte("forkmesh-catalog-v2\n" + hex.EncodeToString(digest[:])))
	return record, nil
}

func gitValue(ctx context.Context, gitDir string, args ...string) string {
	base := append([]string{"--git-dir=" + gitDir}, args...)
	out, err := exec.CommandContext(ctx, "git", base...).Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

func gitLines(ctx context.Context, gitDir string, args ...string) []string {
	value := gitValue(ctx, gitDir, args...)
	if value == "" {
		return nil
	}
	return strings.Split(value, "\n")
}

func countLines(value string) int {
	if strings.TrimSpace(value) == "" {
		return 0
	}
	return len(strings.Split(strings.TrimSpace(value), "\n"))
}

func firstLine(value string) string {
	if index := strings.IndexByte(value, '\n'); index >= 0 {
		value = value[:index]
	}
	if len(value) > 64 {
		value = value[:64]
	}
	return strings.TrimSpace(value)
}

func catalogPathCounts(paths []string) (issues, maxIssue, pulls, discussions, artifacts int) {
	for _, path := range paths {
		path = strings.TrimSpace(path)
		if strings.HasPrefix(path, ".forkmesh/issues/open/") && strings.HasSuffix(path, ".json") {
			issues++
		}
		if strings.HasPrefix(path, ".forkmesh/issues/") {
			for _, part := range strings.Split(path, "/") {
				if value, err := strconv.Atoi(strings.TrimPrefix(strings.TrimSuffix(part, ".json"), "issue-")); err == nil && value > maxIssue {
					maxIssue = value
				}
			}
		}
		parts := strings.Split(path, "/")
		if len(parts) == 4 && parts[0] == ".forkmesh" &&
			parts[1] == "pulls" && parts[3] == "pull.md" {
			if _, err := strconv.Atoi(parts[2]); err == nil {
				pulls++
			}
		}
		if strings.HasPrefix(path, ".forkmesh/pulls/open/") && strings.HasSuffix(path, ".json") {
			pulls++
		}
		if strings.HasPrefix(path, ".forkmesh/discussions/") &&
			(strings.HasSuffix(path, "/discussion.md") ||
				strings.HasSuffix(path, "/discussion.json")) {
			discussions++
		}
		if strings.HasPrefix(path, ".forkmesh/artifacts/") {
			artifacts++
		}
	}
	return
}

func directorySize(root string) int64 {
	var total int64
	_ = filepathWalk(root, func(info os.FileInfo) {
		if info.Mode().IsRegular() {
			total += info.Size()
		}
	})
	return total
}

var filepathWalk = func(root string, visit func(os.FileInfo)) error {
	return filepath.Walk(root, func(_ string, info os.FileInfo, err error) error {
		if err == nil {
			visit(info)
		}
		return nil
	})
}

func memoryUsage() (uint64, uint64) {
	if raw, err := readMemInfo(); err == nil {
		var total, available uint64
		for _, line := range strings.Split(string(raw), "\n") {
			fields := strings.Fields(line)
			if len(fields) < 2 {
				continue
			}
			value, err := strconv.ParseUint(fields[1], 10, 64)
			if err != nil {
				continue
			}
			switch fields[0] {
			case "MemTotal:":
				total = value * 1024
			case "MemAvailable:":
				available = value * 1024
			}
		}
		if total > 0 && available <= total {
			return total - available, total
		}
	}
	var info syscall.Sysinfo_t
	if syscall.Sysinfo(&info) != nil {
		return 0, 0
	}
	total := info.Totalram * uint64(info.Unit)
	free := (info.Freeram + info.Bufferram) * uint64(info.Unit)
	return total - free, total
}

var readMemInfo = func() ([]byte, error) { return os.ReadFile("/proc/meminfo") }

func diskUsage(path string) (uint64, uint64) {
	var info syscall.Statfs_t
	if syscall.Statfs(path, &info) != nil {
		return 0, 0
	}
	total := info.Blocks * uint64(info.Bsize)
	free := info.Bavail * uint64(info.Bsize)
	return total - free, total
}

var (
	cpuMu       sync.Mutex
	cpuPrevious cpuTimes
	readCPUStat = func() ([]byte, error) { return os.ReadFile("/proc/stat") }
)

type cpuTimes struct {
	idle  uint64
	total uint64
}

func parseCPUTimes(raw []byte) (cpuTimes, bool) {
	line := strings.SplitN(string(raw), "\n", 2)[0]
	fields := strings.Fields(line)
	if len(fields) < 5 || fields[0] != "cpu" {
		return cpuTimes{}, false
	}
	var sample cpuTimes
	for index, field := range fields[1:] {
		value, err := strconv.ParseUint(field, 10, 64)
		if err != nil {
			return cpuTimes{}, false
		}
		sample.total += value
		if index == 3 || index == 4 { // idle + iowait
			sample.idle += value
		}
	}
	return sample, sample.total > 0
}

func cpuPercent() int {
	raw, err := readCPUStat()
	if err != nil {
		return 0
	}
	current, ok := parseCPUTimes(raw)
	if !ok {
		return 0
	}
	cpuMu.Lock()
	previous := cpuPrevious
	cpuPrevious = current
	cpuMu.Unlock()
	if previous.total == 0 || current.total <= previous.total ||
		current.idle < previous.idle {
		return 0
	}
	totalDelta := current.total - previous.total
	idleDelta := current.idle - previous.idle
	if idleDelta >= totalDelta {
		return 0
	}
	percent := int((100*(totalDelta-idleDelta) + totalDelta/2) / totalDelta)
	if percent > 100 {
		return 100
	}
	return percent
}
