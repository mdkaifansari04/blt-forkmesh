package mirrornode

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestLoadConfigDefaultsAndRejectsUnsafeInputs(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "node.json")
	good := `{"schemaVersion":1,"gatewayConfig":"/a","gatewayScript":"/b","python":"/c","cloudflared":"/d","connectorToken":"/e","identityKey":"/f","catalogUrl":"https://example.test/api/repositories","publishOwner":"mirror9","upstreams":{}}`
	if err := os.WriteFile(path, []byte(good), 0600); err != nil {
		t.Fatal(err)
	}
	cfg, err := LoadConfig(path)
	if err != nil {
		t.Fatal(err)
	}
	if cfg.Listen != "127.0.0.1:8791" || cfg.SyncInterval.Duration != 30*time.Second || cfg.SyncTimeout.Duration != 5*time.Minute {
		t.Fatalf("defaults not applied: %+v", cfg)
	}
	for name, raw := range map[string]string{
		"unknown":       strings.Replace(good, `"schemaVersion":1`, `"schemaVersion":1,"wat":true`, 1),
		"public listen": strings.Replace(good, `"schemaVersion":1`, `"schemaVersion":1,"listen":"0.0.0.0:1"`, 1),
		"relative":      strings.Replace(good, `"gatewayConfig":"/a"`, `"gatewayConfig":"a"`, 1),
		"bad duration":  strings.Replace(good, `"upstreams":{}`, `"syncInterval":"1s","upstreams":{}`, 1),
	} {
		t.Run(name, func(t *testing.T) {
			if err := os.WriteFile(path, []byte(raw), 0600); err != nil {
				t.Fatal(err)
			}
			if _, err := LoadConfig(path); err == nil {
				t.Fatal("expected rejection")
			}
		})
	}
	link := filepath.Join(dir, "link.json")
	if err := os.Symlink(path, link); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadConfig(link); err == nil {
		t.Fatal("symlink config accepted")
	}
}

func TestDurationRejectsNonStringAndInvalid(t *testing.T) {
	for _, raw := range []string{`1`, `""`, `"nope"`, `"-1s"`} {
		var d Duration
		if err := d.UnmarshalJSON([]byte(raw)); err == nil {
			t.Fatalf("accepted %s", raw)
		}
	}
}
