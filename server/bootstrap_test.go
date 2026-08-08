package mirrornode

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

func TestBootstrapCreatesPrivateNativeConfiguration(t *testing.T) {
	root := t.TempDir()
	configPath := filepath.Join(root, "etc", "mirror-node.json")
	publicKey, err := Bootstrap(BootstrapOptions{
		ConfigPath: configPath, StateDirectory: filepath.Join(root, "state"),
		Node: "mirror17", Owner: "forkmesh", Repository: "forkmesh",
		Upstream:        "https://forkmesh.test/forkmesh/forkmesh",
		CatalogURL:      "https://forkmesh.test/api/repositories",
		PublicOrigin:    "https://mirror17.forkmesh.test",
		RouterPublicKey: "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		GatewayScript:   "/usr/local/share/forkmesh/tools/mirror_gateway.py",
		Python:          "/usr/bin/python3", Cloudflared: "/usr/local/bin/cloudflared",
		Version: "test",
	})
	if err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(configPath)
	if err != nil || info.Mode().Perm()&0077 != 0 {
		t.Fatalf("configuration is not owner-only: %v %+v", err, info)
	}
	config, err := LoadConfig(configPath)
	if err != nil {
		t.Fatal(err)
	}
	identity, err := LoadIdentity(config.IdentityKey)
	if err != nil || identity.PublicKey() != publicKey {
		t.Fatalf("identity mismatch: %v", err)
	}
	raw, err := os.ReadFile(config.GatewayConfig)
	if err != nil {
		t.Fatal(err)
	}
	var gateway GatewayConfig
	if err := json.Unmarshal(raw, &gateway); err != nil || len(gateway.Repositories) != 2 {
		t.Fatalf("gateway configuration: %v %+v", err, gateway)
	}
	if values := config.Upstreams["mirror17/forkmesh"]; len(values) != 1 || values[0] == "" {
		t.Fatalf("round-robin upstream missing: %+v", config.Upstreams)
	}
}
