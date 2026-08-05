package mirrornode

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestTokenEnvironmentRequiresOwnerOnlyFileAndRedactsAlternates(t *testing.T) {
	path := filepath.Join(t.TempDir(), "token")
	if err := os.WriteFile(path, []byte("abc\n"), 0600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("CF_TOKEN", "leak")
	t.Setenv("TUNNEL_TOKEN", "old")
	env, err := tokenEnvironment(path)
	if err != nil {
		t.Fatal(err)
	}
	joined := strings.Join(env, "\n")
	if strings.Contains(joined, "leak") || strings.Contains(joined, "TUNNEL_TOKEN=old") || !strings.Contains(joined, "TUNNEL_TOKEN=abc") {
		t.Fatalf("unsafe env: %s", joined)
	}
	if err := os.Chmod(path, 0644); err != nil {
		t.Fatal(err)
	}
	if _, err := tokenEnvironment(path); err == nil {
		t.Fatal("weak token permissions accepted")
	}
	if err := os.Chmod(path, 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte("a\nb"), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := tokenEnvironment(path); err == nil {
		t.Fatal("multiline token accepted")
	}
}
