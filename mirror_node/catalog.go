package mirrornode

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"time"
)

type CatalogPublisher struct {
	URL      string
	Owner    string
	Node     string
	Version  string
	Identity *Identity
	Client   *http.Client
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
	paths := gitLines(ctx, repo.GitDir, "ls-tree", "-r", "--name-only", "HEAD", "--", ".forkmesh")
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
		"cloneUrl":     "", "solana": "", "channel": "#" + p.Owner + "-" + repo.Name,
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
		if strings.HasPrefix(path, ".forkmesh/pulls/open/") && strings.HasSuffix(path, ".json") {
			pulls++
		}
		if strings.HasPrefix(path, ".forkmesh/discussions/") && strings.HasSuffix(path, ".json") {
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
	var info syscall.Sysinfo_t
	if syscall.Sysinfo(&info) != nil {
		return 0, 0
	}
	total := info.Totalram * uint64(info.Unit)
	free := (info.Freeram + info.Bufferram) * uint64(info.Unit)
	return total - free, total
}

func diskUsage(path string) (uint64, uint64) {
	var info syscall.Statfs_t
	if syscall.Statfs(path, &info) != nil {
		return 0, 0
	}
	total := info.Blocks * uint64(info.Bsize)
	free := info.Bavail * uint64(info.Bsize)
	return total - free, total
}

func cpuPercent() int { return 0 }
