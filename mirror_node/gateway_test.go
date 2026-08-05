package mirrornode

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func run(t *testing.T, dir string, args ...string) string {
	t.Helper()
	cmd := exec.Command(args[0], args[1:]...)
	cmd.Dir = dir
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("%v: %v: %s", args, err, out)
	}
	return strings.TrimSpace(string(out))
}

func makeBareRepo(t *testing.T) (string, string) {
	t.Helper()
	root := t.TempDir()
	source := filepath.Join(root, "source")
	bare := filepath.Join(root, "mirror.git")
	run(t, root, "git", "init", "-b", "main", source)
	run(t, source, "git", "config", "user.email", "test@example.test")
	run(t, source, "git", "config", "user.name", "Test")
	if err := os.WriteFile(filepath.Join(source, "README.md"), []byte("one\n"), 0600); err != nil {
		t.Fatal(err)
	}
	run(t, source, "git", "add", "README.md")
	run(t, source, "git", "commit", "-m", "one")
	run(t, root, "git", "clone", "--bare", source, bare)
	return source, bare
}

func TestRefsSHA256MatchesGatewayCanonicalAndIgnoresToolRefs(t *testing.T) {
	_, bare := makeBareRepo(t)
	run(t, ".", "git", "--git-dir="+bare, "branch", "z")
	run(t, ".", "git", "--git-dir="+bare, "tag", "v1")
	run(t, ".", "git", "--git-dir="+bare, "update-ref", "refs/codex/tmp", "HEAD")
	digest, err := refsSHA256(context.Background(), bare)
	if err != nil {
		t.Fatal(err)
	}
	lines := []string{run(t, ".", "git", "--git-dir="+bare, "rev-parse", "refs/heads/main") + " refs/heads/main", run(t, ".", "git", "--git-dir="+bare, "rev-parse", "refs/heads/z") + " refs/heads/z", run(t, ".", "git", "--git-dir="+bare, "rev-parse", "refs/tags/v1") + " refs/tags/v1"}
	want := sha256.Sum256([]byte(strings.Join(lines, "\n")))
	if digest != hex.EncodeToString(want[:]) {
		t.Fatalf("got %s want %x", digest, want)
	}
}

func TestSyncRepositoryFetchesStableRefsAndReportsFailure(t *testing.T) {
	source, bare := makeBareRepo(t)
	if err := os.WriteFile(filepath.Join(source, "README.md"), []byte("two\n"), 0600); err != nil {
		t.Fatal(err)
	}
	run(t, source, "git", "add", "README.md")
	run(t, source, "git", "commit", "-m", "two")
	repo := Repository{GitDir: bare}
	if err := syncRepository(context.Background(), repo, []string{"/missing", source}); err != nil {
		t.Fatal(err)
	}
	if got, want := run(t, ".", "git", "--git-dir="+bare, "rev-parse", "main"), run(t, source, "git", "rev-parse", "main"); got != want {
		t.Fatalf("got %s want %s", got, want)
	}
	if err := syncRepository(context.Background(), repo, []string{"/missing"}); err == nil {
		t.Fatal("expected failure")
	}
}

func TestWriteAndLoadGatewayConfigAreAtomicAndStrict(t *testing.T) {
	_, bare := makeBareRepo(t)
	identity, _ := testIdentity(t)
	path := filepath.Join(t.TempDir(), "gateway.json")
	var cfg GatewayConfig
	cfg.SchemaVersion = 1
	cfg.Node.Name = "mirror9"
	cfg.Node.PublicKey = identity.PublicKey()
	cfg.RouterPublicKey = identity.PublicKey()
	cfg.Repositories = []Repository{{Owner: "mirror9", Name: "forkmesh", Visibility: "public", Enabled: true, GitDir: bare}}
	changed, err := writeGatewayConfig(path, cfg)
	if err != nil || !changed {
		t.Fatalf("%v %v", changed, err)
	}
	changed, err = writeGatewayConfig(path, cfg)
	if err != nil || changed {
		t.Fatalf("identical write changed: %v %v", changed, err)
	}
	loaded, _, err := loadGatewayConfig(path)
	if err != nil || loaded.Node.Name != "mirror9" {
		t.Fatalf("%+v %v", loaded, err)
	}
	raw, _ := json.Marshal(map[string]any{"schemaVersion": 1, "node": map[string]string{"name": "mirror9", "publicKey": identity.PublicKey()}, "repositories": []any{}, "unknown": true})
	if err := os.WriteFile(path, raw, 0600); err != nil {
		t.Fatal(err)
	}
	if _, _, err := loadGatewayConfig(path); err == nil {
		t.Fatal("unknown field accepted")
	}
}

func TestCatalogPathCounts(t *testing.T) {
	i, m, p, d, a := catalogPathCounts([]string{".forkmesh/issues/open/9/issue-9.json", ".forkmesh/issues/closed/11/issue-11.json", ".forkmesh/pulls/open/2.json", ".forkmesh/discussions/1/discussion.json", ".forkmesh/artifacts/a"})
	if i != 1 || m != 11 || p != 1 || d != 1 || a != 1 {
		t.Fatalf("%d %d %d %d %d", i, m, p, d, a)
	}
}
