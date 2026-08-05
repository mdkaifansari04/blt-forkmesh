package mirrornode

import (
	"context"
	"crypto/ed25519"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

func TestCatalogPublisherSignsTheWorkerNormalizedRecord(t *testing.T) {
	source, bare := makeBareRepo(t)
	if err := os.MkdirAll(filepath.Join(source, ".forkmesh/issues/open/7"), 0700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(source, ".forkmesh/issues/open/7/issue-7.json"), []byte("{}"), 0600); err != nil {
		t.Fatal(err)
	}
	run(t, source, "git", "add", ".")
	run(t, source, "git", "commit", "-m", "issue")
	for _, number := range []string{"1", "2"} {
		dir := filepath.Join(source, "pulls", number)
		if err := os.MkdirAll(dir, 0700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(dir, "pull.md"), []byte("---\nstatus: open\n---\n"), 0600); err != nil {
			t.Fatal(err)
		}
	}
	run(t, source, "git", "add", ".")
	run(t, source, "git", "commit", "-m", "historical pulls")
	run(t, source, "git", "checkout", "-b", "forkmesh/pulls")
	run(t, source, "git", "rm", "-r", "pulls/2")
	run(t, source, "git", "commit", "-m", "retain open pulls only")
	run(t, source, "git", "checkout", "main")
	run(t, ".", "git", "--git-dir="+bare, "fetch", source, "refs/heads/main:refs/heads/main")
	run(t, ".", "git", "--git-dir="+bare, "fetch", source, "refs/heads/forkmesh/pulls:refs/heads/forkmesh/pulls")
	identity, _ := testIdentity(t)
	stateHash, err := refsSHA256(context.Background(), bare)
	if err != nil {
		t.Fatal(err)
	}
	var received map[string]any
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost || r.Header.Get("Content-Type") != "application/json" {
			t.Errorf("bad request: %s %s", r.Method, r.Header.Get("Content-Type"))
		}
		if err := json.NewDecoder(r.Body).Decode(&received); err != nil {
			t.Error(err)
		}
		w.WriteHeader(http.StatusCreated)
	}))
	defer server.Close()
	repo := Repository{Owner: "mirror9", Name: "forkmesh", Visibility: "public", Enabled: true, GitDir: bare}
	publisher := &CatalogPublisher{URL: server.URL, Owner: "mirror9", Node: "mirror9", Version: "test", Identity: identity, Client: server.Client()}
	if err := publisher.Publish(context.Background(), repo, stateHash); err != nil {
		t.Fatal(err)
	}
	if received["stateHash"] != stateHash || received["issueCount"] != "1" ||
		received["pullCount"] != "1" || received["runtimeMode"] != "headless" {
		t.Fatalf("unexpected record: %+v", received)
	}
	statePayload := "forkmesh-repostate-v1\nmirror9\nforkmesh\n" + stateHash + "\n" + received["updatedAt"].(string)
	stateSig, _ := base64.RawURLEncoding.DecodeString(received["stateSig"].(string))
	if !ed25519.Verify(identity.public, []byte(statePayload), stateSig) {
		t.Fatal("bad state signature")
	}
	catalogSig, _ := base64.RawURLEncoding.DecodeString(received["catalogSig"].(string))
	delete(received, "catalogSig")
	delete(received, "catalogSigVersion")
	delete(received, "signature")
	canonical, _ := json.Marshal(received)
	digest := sha256.Sum256(canonical)
	payload := []byte("forkmesh-catalog-v2\n" + hex.EncodeToString(digest[:]))
	if !ed25519.Verify(identity.public, payload, catalogSig) {
		t.Fatalf("bad catalog signature over %s", canonical)
	}
}

func TestCatalogPublisherSurfacesHTTPFailure(t *testing.T) {
	_, bare := makeBareRepo(t)
	identity, _ := testIdentity(t)
	stateHash, _ := refsSHA256(context.Background(), bare)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) { http.Error(w, "no", http.StatusUnauthorized) }))
	defer server.Close()
	p := &CatalogPublisher{URL: server.URL, Owner: "mirror9", Node: "mirror9", Version: "test", Identity: identity, Client: server.Client()}
	if err := p.Publish(context.Background(), Repository{Owner: "mirror9", Name: "forkmesh", GitDir: bare}, stateHash); err == nil || !strings.Contains(err.Error(), "401") {
		t.Fatal("HTTP failure ignored")
	}
}

func TestEndpointPublisherRenewsSignedActiveLease(t *testing.T) {
	identity, _ := testIdentity(t)
	var received map[string]any
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/mirrors/https" || r.Method != http.MethodPost {
			t.Errorf("unexpected request: %s %s", r.Method, r.URL.Path)
		}
		if err := json.NewDecoder(r.Body).Decode(&received); err != nil {
			t.Error(err)
		}
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok": true, "node": "mirror9", "baseUrl": "https://mirror9.example.test",
			"health": "active",
		})
	}))
	defer server.Close()
	publisher := &EndpointPublisher{
		CatalogURL: server.URL + "/api/repositories?ignored=true",
		Node:       "mirror9", BaseURL: "https://mirror9.example.test",
		Version: "test", Identity: identity, Client: server.Client(),
	}
	active, err := publisher.Publish(context.Background())
	if err != nil || !active {
		t.Fatal(err)
	}
	issuedAt := int64(received["issuedAt"].(float64))
	message := "forkmesh-https-endpoint-v1\nmirror9\nhttps://mirror9.example.test\n" +
		identity.PublicKey() + "\n" + strconv.FormatInt(issuedAt, 10)
	signature, err := base64.RawURLEncoding.DecodeString(received["signature"].(string))
	if err != nil || !ed25519.Verify(identity.public, []byte(message), signature) {
		t.Fatal("endpoint registration signature is invalid")
	}
}

func TestEndpointPublisherRetriesPendingLeaseWithoutFailingSync(t *testing.T) {
	identity, _ := testIdentity(t)
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok": true, "node": "mirror9", "baseUrl": "https://mirror9.example.test",
			"health": "pending",
		})
	}))
	defer server.Close()
	publisher := &EndpointPublisher{
		CatalogURL: server.URL + "/api/repositories", Node: "mirror9",
		BaseURL: "https://mirror9.example.test", Version: "test",
		Identity: identity, Client: server.Client(),
	}
	active, err := publisher.Publish(context.Background())
	if err != nil || active {
		t.Fatalf("pending lease result: active=%v err=%v", active, err)
	}
}

func TestFirstLineMatchesWorkerRootCommitNormalization(t *testing.T) {
	if got := firstLine("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"); got != "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" {
		t.Fatalf("got %q", got)
	}
	if got := firstLine(strings.Repeat("x", 80)); len(got) != 64 {
		t.Fatalf("got %d chars", len(got))
	}
}

func TestCatalogPathCountsUseCurrentGitLayouts(t *testing.T) {
	issues, maxIssue, pulls, discussions, artifacts := catalogPathCounts([]string{
		".forkmesh/issues/open/7/issue-7.json",
		".forkmesh/issues/closed/91/issue-91.json",
		"pulls/11/pull.md",
		"pulls/not-a-number/pull.md",
		".forkmesh/discussions/2/discussion.md",
		".forkmesh/discussions/2/0002-comment.md",
		".forkmesh/artifacts/sha256/file",
	})
	if issues != 1 || maxIssue != 91 || pulls != 1 || discussions != 1 || artifacts != 1 {
		t.Fatalf("got issues=%d max=%d pulls=%d discussions=%d artifacts=%d",
			issues, maxIssue, pulls, discussions, artifacts)
	}
}

func TestCPUPercentUsesDeltaAndBoundsInput(t *testing.T) {
	oldRead := readCPUStat
	oldPrevious := cpuPrevious
	t.Cleanup(func() {
		readCPUStat = oldRead
		cpuPrevious = oldPrevious
	})
	samples := [][]byte{
		[]byte("cpu  100 0 50 800 50 0 0 0\n"),
		[]byte("cpu  150 0 70 860 60 0 0 0\n"),
	}
	readCPUStat = func() ([]byte, error) {
		value := samples[0]
		samples = samples[1:]
		return value, nil
	}
	cpuPrevious = cpuTimes{}
	if got := cpuPercent(); got != 0 {
		t.Fatalf("first sample = %d", got)
	}
	if got := cpuPercent(); got != 50 {
		t.Fatalf("second sample = %d", got)
	}
	if _, ok := parseCPUTimes([]byte("intr 1 2 3\n")); ok {
		t.Fatal("accepted non-CPU stat line")
	}
}

func TestMemoryUsageExcludesReclaimableLinuxCache(t *testing.T) {
	oldRead := readMemInfo
	t.Cleanup(func() { readMemInfo = oldRead })
	readMemInfo = func() ([]byte, error) {
		return []byte("MemTotal:       1000000 kB\nMemFree:         100000 kB\nMemAvailable:    650000 kB\nCached:          500000 kB\n"), nil
	}
	used, total := memoryUsage()
	if total != 1000000*1024 || used != 350000*1024 {
		t.Fatalf("used=%d total=%d", used, total)
	}
}
