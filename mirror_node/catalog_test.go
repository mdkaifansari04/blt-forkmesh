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
	run(t, ".", "git", "--git-dir="+bare, "fetch", source, "refs/heads/main:refs/heads/main")
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
	if received["stateHash"] != stateHash || received["issueCount"] != "1" || received["runtimeMode"] != "headless" {
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

func TestFirstLineMatchesWorkerRootCommitNormalization(t *testing.T) {
	if got := firstLine("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"); got != "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" {
		t.Fatalf("got %q", got)
	}
	if got := firstLine(strings.Repeat("x", 80)); len(got) != 64 {
		t.Fatalf("got %d chars", len(got))
	}
}
