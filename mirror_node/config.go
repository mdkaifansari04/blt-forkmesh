package mirrornode

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const configSchemaVersion = 1

type Config struct {
	SchemaVersion      int                 `json:"schemaVersion"`
	Listen             string              `json:"listen"`
	GatewayConfig      string              `json:"gatewayConfig"`
	GatewayScript      string              `json:"gatewayScript"`
	Python             string              `json:"python"`
	Cloudflared        string              `json:"cloudflared"`
	ConnectorToken     string              `json:"connectorToken"`
	IdentityKey        string              `json:"identityKey"`
	CatalogURL         string              `json:"catalogUrl"`
	SyncInterval       Duration            `json:"syncInterval"`
	SyncTimeout        Duration            `json:"syncTimeout"`
	GatewayProbe       string              `json:"gatewayProbe"`
	Upstreams          map[string][]string `json:"upstreams"`
	PublishOwner       string              `json:"publishOwner"`
	Version            string              `json:"version"`
	IntakeProgram      string              `json:"intakeProgram"`
	IntakeOwner        string              `json:"intakeOwner"`
	IntakeRepository   string              `json:"intakeRepository"`
	IntakePollInterval Duration            `json:"intakePollInterval"` // deprecated: ignored, intake is push-driven
	IntakeIdleGrace    Duration            `json:"intakeIdleGrace"`
	DisableCatalog     bool                `json:"disableCatalog"`
	DisableCloudflared bool                `json:"disableCloudflared"`
}

type Duration struct{ time.Duration }

func (d *Duration) UnmarshalJSON(raw []byte) error {
	var value string
	if err := json.Unmarshal(raw, &value); err != nil {
		return errors.New("duration must be a string")
	}
	parsed, err := time.ParseDuration(value)
	if err != nil || parsed <= 0 {
		return errors.New("duration must be positive")
	}
	d.Duration = parsed
	return nil
}

func LoadConfig(path string) (Config, error) {
	var cfg Config
	if !filepath.IsAbs(path) {
		return cfg, errors.New("config path must be absolute")
	}
	info, err := os.Lstat(path)
	if err != nil {
		return cfg, fmt.Errorf("stat config: %w", err)
	}
	if !info.Mode().IsRegular() || info.Mode()&os.ModeSymlink != 0 || info.Size() > 256<<10 {
		return cfg, errors.New("config must be a small regular file")
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		return cfg, fmt.Errorf("read config: %w", err)
	}
	decoder := json.NewDecoder(strings.NewReader(string(raw)))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&cfg); err != nil {
		return cfg, fmt.Errorf("decode config: %w", err)
	}
	if err := decoder.Decode(&struct{}{}); err != io.EOF {
		return cfg, errors.New("config contains trailing JSON")
	}
	if err := cfg.validate(); err != nil {
		return cfg, err
	}
	return cfg, nil
}

func (c *Config) validate() error {
	if c.SchemaVersion != configSchemaVersion {
		return errors.New("unsupported config schemaVersion")
	}
	if c.Listen == "" {
		c.Listen = "127.0.0.1:8791"
	}
	host, _, err := net.SplitHostPort(c.Listen)
	if err != nil || (host != "127.0.0.1" && host != "::1" && host != "localhost") {
		return errors.New("listen must be a loopback host and port")
	}
	if c.GatewayProbe == "" {
		c.GatewayProbe = "127.0.0.1:8790"
	}
	probeHost, _, err := net.SplitHostPort(c.GatewayProbe)
	if err != nil || (probeHost != "127.0.0.1" && probeHost != "::1" && probeHost != "localhost") {
		return errors.New("gatewayProbe must be a loopback host and port")
	}
	for label, value := range map[string]string{
		"gatewayConfig": c.GatewayConfig, "gatewayScript": c.GatewayScript,
		"python": c.Python, "identityKey": c.IdentityKey,
	} {
		if !filepath.IsAbs(value) {
			return fmt.Errorf("%s must be absolute", label)
		}
	}
	if !c.DisableCloudflared {
		if !filepath.IsAbs(c.Cloudflared) || !filepath.IsAbs(c.ConnectorToken) {
			return errors.New("cloudflared and connectorToken must be absolute")
		}
	}
	if c.SyncInterval.Duration == 0 {
		c.SyncInterval.Duration = 30 * time.Second
	}
	if c.SyncInterval.Duration < 5*time.Second || c.SyncInterval.Duration > 24*time.Hour {
		return errors.New("syncInterval must be between 5s and 24h")
	}
	if c.SyncTimeout.Duration == 0 {
		c.SyncTimeout.Duration = 5 * time.Minute
	}
	if c.SyncTimeout.Duration < 10*time.Second || c.SyncTimeout.Duration > 30*time.Minute {
		return errors.New("syncTimeout must be between 10s and 30m")
	}
	if !c.DisableCatalog && (c.CatalogURL == "" || c.PublishOwner == "") {
		return errors.New("catalogUrl and publishOwner are required")
	}
	intakeConfigured := c.IntakeProgram != "" || c.IntakeOwner != "" || c.IntakeRepository != ""
	if intakeConfigured {
		if !filepath.IsAbs(c.IntakeProgram) || !nodePattern.MatchString(c.IntakeOwner) ||
			!repositoryPattern.MatchString(c.IntakeRepository) || c.CatalogURL == "" {
			return errors.New("intakeProgram, intakeOwner, intakeRepository, and catalogUrl must be valid")
		}
		// intakePollInterval is accepted but ignored: intake is push-driven
		// over the relay's node event WebSocket and never polls /pending.
		// The field survives only so deployed configs that still set it
		// keep loading (decoding rejects unknown fields).
		// idleGrace now bounds how long the worker keeps running after the
		// LAST push event (each new event extends the window). The old 20s
		// default assumed a live queue-drained signal; without one the
		// window must comfortably cover a full inbox drain.
		if c.IntakeIdleGrace.Duration == 0 {
			c.IntakeIdleGrace.Duration = 2 * time.Minute
		}
		if c.IntakeIdleGrace.Duration < 5*time.Second || c.IntakeIdleGrace.Duration > 10*time.Minute {
			return errors.New("intakeIdleGrace must be between 5s and 10m")
		}
	}
	if c.Version == "" {
		c.Version = "dev"
	}
	return nil
}
