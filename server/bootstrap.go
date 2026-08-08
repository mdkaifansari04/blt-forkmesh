package mirrornode

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
)

type BootstrapOptions struct {
	ConfigPath      string
	StateDirectory  string
	Node            string
	Owner           string
	Repository      string
	Upstream        string
	CatalogURL      string
	PublicOrigin    string
	RouterPublicKey string
	GatewayScript   string
	Python          string
	Cloudflared     string
	Version         string
}

// Bootstrap writes the minimum native state for a fresh server.
func Bootstrap(options BootstrapOptions) (string, error) {
	for label, value := range map[string]string{
		"config": options.ConfigPath, "state": options.StateDirectory,
		"gateway script": options.GatewayScript, "python": options.Python,
		"cloudflared": options.Cloudflared,
	} {
		if !filepath.IsAbs(value) {
			return "", errors.New(label + " path must be absolute")
		}
	}
	if !nodePattern.MatchString(options.Node) || !nodePattern.MatchString(options.Owner) ||
		!repositoryPattern.MatchString(options.Repository) || options.Upstream == "" ||
		options.CatalogURL == "" || options.PublicOrigin == "" || options.RouterPublicKey == "" {
		return "", errors.New("bootstrap identity, repository, or endpoint is invalid")
	}
	if err := os.MkdirAll(options.StateDirectory, 0700); err != nil {
		return "", err
	}
	identityPath := filepath.Join(options.StateDirectory, "identity", "ed25519.pem")
	identity, err := GenerateIdentity(identityPath)
	if err != nil {
		return "", err
	}
	repositoryPath := filepath.Join(options.StateDirectory, "repositories", options.Owner+"-"+options.Repository+".git")
	gatewayPath := filepath.Join(options.StateDirectory, "mirror-gateway", "config.json")
	manifestPath := filepath.Join(options.StateDirectory, "mirror-gateway", "forkmesh-mirror.json")
	tokenPath := filepath.Join(options.StateDirectory, "mirror-gateway", "connector.token")
	if err := os.MkdirAll(filepath.Dir(gatewayPath), 0700); err != nil {
		return "", err
	}
	var gateway GatewayConfig
	gateway.SchemaVersion = 1
	gateway.Node.Name = options.Node
	gateway.Node.PublicKey = identity.PublicKey()
	gateway.RouterPublicKey = options.RouterPublicKey
	gateway.PublicOrigin = options.PublicOrigin
	gateway.Listen = json.RawMessage(`{"host":"127.0.0.1","port":8790}`)
	gateway.ManifestPath = manifestPath
	gateway.Limits = json.RawMessage(`{"maxReleaseBytes":2147483648,"maxPrivateReplicaBytes":536870912}`)
	operations := []string{"blob", "blobs", "branches", "commit", "compare", "git-info-refs", "git-upload-pack", "history", "raw", "release-blob", "search", "sizes", "stats", "tree"}
	for _, owner := range []string{options.Owner, options.Node} {
		repository := Repository{Owner: owner, Name: options.Repository, Visibility: "public", Enabled: true, GitDir: repositoryPath, Operations: operations}
		gateway.Repositories = append(gateway.Repositories, repository)
	}
	if _, err := writeGatewayConfig(gatewayPath, gateway); err != nil {
		return "", err
	}
	config := map[string]any{
		"schemaVersion": 1, "listen": "127.0.0.1:8791", "gatewayProbe": "127.0.0.1:8790",
		"gatewayConfig": gatewayPath, "gatewayScript": options.GatewayScript,
		"python": options.Python, "cloudflared": options.Cloudflared,
		"connectorToken": tokenPath, "identityKey": identityPath,
		"catalogUrl": options.CatalogURL, "publishOwner": options.Node,
		"version": options.Version, "syncInterval": "30s", "syncTimeout": "5m",
		"upstreams": map[string][]string{
			options.Owner + "/" + options.Repository: {options.Upstream},
			options.Node + "/" + options.Repository:  {options.Upstream},
		},
	}
	if err := writePrivateJSON(options.ConfigPath, config); err != nil {
		return "", err
	}
	return identity.PublicKey(), nil
}

func writePrivateJSON(path string, value any) error {
	if !filepath.IsAbs(path) {
		return errors.New("output path must be absolute")
	}
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return err
	}
	raw, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	raw = append(raw, '\n')
	temporary, err := os.CreateTemp(filepath.Dir(path), ".config-*")
	if err != nil {
		return err
	}
	temporaryPath := temporary.Name()
	defer os.Remove(temporaryPath)
	if err := temporary.Chmod(0600); err != nil {
		temporary.Close()
		return err
	}
	if _, err := temporary.Write(raw); err != nil {
		temporary.Close()
		return err
	}
	if err := temporary.Sync(); err != nil {
		temporary.Close()
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}
	return os.Rename(temporaryPath, path)
}
