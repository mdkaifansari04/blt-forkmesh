package mirrornode

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func freeAddress(t *testing.T) string {
	t.Helper()
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	address := listener.Addr().String()
	listener.Close()
	return address
}

func TestDaemonSupervisesSyncsRepinsAndReportsReadiness(t *testing.T) {
	source, bare := makeBareRepo(t)
	identity, keyPath := testIdentity(t)
	gatewayAddress, statusAddress := freeAddress(t), freeAddress(t)
	script := filepath.Join(t.TempDir(), "fake_gateway.py")
	python := fmt.Sprintf(`import signal,socket,sys
s=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(("127.0.0.1",%s));s.listen()
signal.signal(signal.SIGTERM,lambda *_:sys.exit(0))
while True:
 c,_=s.accept();c.close()
`, gatewayAddress[len("127.0.0.1:"):])
	if err := os.WriteFile(script, []byte(python), 0700); err != nil {
		t.Fatal(err)
	}
	gatewayPath := filepath.Join(t.TempDir(), "gateway.json")
	var gateway GatewayConfig
	gateway.SchemaVersion = 1
	gateway.Node.Name = "mirror9"
	gateway.Node.PublicKey = identity.PublicKey()
	gateway.RouterPublicKey = identity.PublicKey()
	for _, owner := range []string{"forkmesh", "mirror9"} {
		repo := Repository{Owner: owner, Name: "forkmesh", Visibility: "public", Enabled: true, GitDir: bare}
		repo.Integrity.ExpectedRefsSHA256 = "0"
		gateway.Repositories = append(gateway.Repositories, repo)
	}
	if _, err := writeGatewayConfig(gatewayPath, gateway); err != nil {
		t.Fatal(err)
	}
	servicePath := filepath.Join(t.TempDir(), "service.json")
	if err := os.WriteFile(servicePath, []byte("{}"), 0600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("FORKMESH_MIRROR_NODE_CONFIG", servicePath)
	cfg := Config{SchemaVersion: 1, Listen: statusAddress, GatewayProbe: gatewayAddress, GatewayConfig: gatewayPath, GatewayScript: script, Python: "/usr/bin/python3", IdentityKey: keyPath, SyncInterval: Duration{5 * time.Second}, SyncTimeout: Duration{10 * time.Second}, Upstreams: map[string][]string{"mirror9/forkmesh": {source}}, DisableCloudflared: true, DisableCatalog: true, Version: "test"}
	if err := cfg.validate(); err != nil {
		t.Fatal(err)
	}
	daemon, err := NewDaemon(cfg)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan error, 1)
	go func() { done <- daemon.Run(ctx, "/bin/true") }()
	client := &http.Client{Timeout: time.Second}
	var status Status
	deadline := time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) {
		response, err := client.Get("http://" + statusAddress + "/v1/status")
		if err == nil {
			_ = json.NewDecoder(response.Body).Decode(&status)
			response.Body.Close()
			if status.Ready {
				break
			}
		}
		time.Sleep(100 * time.Millisecond)
	}
	if !status.Ready || !status.LastSyncOK || status.Repositories["mirror9/forkmesh"] == "" || !status.Processes["gateway"] {
		t.Fatalf("not ready: %+v", status)
	}
	if status.Restarts["gateway"] != 0 {
		t.Fatalf("fresh gateway restarted during initial reconciliation: %+v", status.Restarts)
	}
	response, err := client.Get("http://" + statusAddress + "/healthz")
	if err != nil || response.StatusCode != http.StatusOK {
		t.Fatalf("health: %v %+v", err, response)
	}
	response.Body.Close()
	response, err = client.Get("http://" + statusAddress + "/v1/control/sync")
	if err != nil || response.StatusCode != http.StatusMethodNotAllowed {
		t.Fatalf("sync GET should be rejected: %v %+v", err, response)
	}
	response.Body.Close()
	beforeSyncCount := status.SyncCount
	response, err = client.Post(
		"http://"+statusAddress+"/v1/control/sync", "application/json", nil)
	if err != nil || response.StatusCode != http.StatusAccepted {
		t.Fatalf("sync POST: %v %+v", err, response)
	}
	response.Body.Close()
	deadline = time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) {
		response, err = client.Get("http://" + statusAddress + "/v1/status")
		if err == nil {
			_ = json.NewDecoder(response.Body).Decode(&status)
			response.Body.Close()
			if status.SyncCount > beforeSyncCount {
				break
			}
		}
		time.Sleep(50 * time.Millisecond)
	}
	if status.SyncCount <= beforeSyncCount || status.Runtime.Goroutines == 0 ||
		status.Runtime.HeapBytes == 0 {
		t.Fatalf("manual sync/runtime stats not reported: %+v", status)
	}
	loaded, _, err := loadGatewayConfig(gatewayPath)
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Repositories[0].Integrity.ExpectedRefsSHA256 == "0" || loaded.HealthSignerCommand[0] != "/bin/true" {
		t.Fatalf("gateway not migrated: %+v", loaded)
	}
	cancel()
	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("daemon did not stop")
	}
}

func TestNewDaemonRejectsIdentityMismatchAndUnsupportedRepo(t *testing.T) {
	identity, key := testIdentity(t)
	other, _ := testIdentity(t)
	path := filepath.Join(t.TempDir(), "gateway.json")
	var gateway GatewayConfig
	gateway.SchemaVersion = 1
	gateway.Node.Name = "mirror9"
	gateway.Node.PublicKey = other.PublicKey()
	gateway.Repositories = []Repository{{Owner: "mirror9", Name: "x", Visibility: "public", Enabled: true, GitDir: "/tmp/x"}}
	if _, err := writeGatewayConfig(path, gateway); err != nil {
		t.Fatal(err)
	}
	cfg := Config{GatewayConfig: path, IdentityKey: key}
	if _, err := NewDaemon(cfg); err == nil {
		t.Fatal("identity mismatch accepted")
	}
	gateway.Node.PublicKey = identity.PublicKey()
	gateway.Repositories[0].Visibility = "private"
	if _, err := writeGatewayConfig(path, gateway); err != nil {
		t.Fatal(err)
	}
	if _, err := NewDaemon(cfg); err == nil {
		t.Fatal("private repository accepted")
	}
}
