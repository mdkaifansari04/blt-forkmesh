package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	mirrornode "forkmesh.com/mirror-node"
)

func main() {
	configPath := flag.String("config", "", "absolute mirror-node configuration path")
	initConfig := flag.Bool("init", false, "initialize a fresh mirror-node configuration")
	stateDirectory := flag.String("state-dir", "", "absolute persistent state directory")
	node := flag.String("node", "", "mirror node name")
	owner := flag.String("owner", "forkmesh", "canonical repository owner")
	repository := flag.String("repository", "forkmesh", "repository name")
	upstream := flag.String("upstream", "https://forkmesh.com/forkmesh/forkmesh", "round-robin upstream URL")
	catalogURL := flag.String("catalog-url", "https://forkmesh.com/api/repositories", "catalog publication URL")
	publicOrigin := flag.String("public-origin", "", "public HTTPS mirror origin")
	routerPublicKey := flag.String("router-public-key", "", "mirror router Ed25519 public key")
	gatewayScript := flag.String("gateway-script", "/usr/local/share/forkmesh/tools/mirror_gateway.py", "mirror gateway script")
	python := flag.String("python", "/usr/bin/python3", "Python executable")
	cloudflared := flag.String("cloudflared", "/usr/local/bin/cloudflared", "cloudflared executable")
	version := flag.String("version", "dev", "reported ForkMesh version")
	printPublicKey := flag.Bool("public-key", false, "print the configured node public key")
	registerLinkCode := flag.String("register-link-code", "", "register this node with a six-digit installer link code")
	accountsURL := flag.String("accounts-url", "https://forkmesh.com/api/accounts", "node account API base URL")
	signHealth := flag.Bool("sign-mirror-health", false, "run the bounded gateway health signer")
	verifyCapability := flag.Bool("verify-mirror-capability", false, "run the bounded gateway capability verifier")
	signManifest := flag.Bool("sign-mirror-manifest", false, "run the bounded mirror manifest signer")
	check := flag.Bool("check", false, "validate configuration and runtime dependencies")
	flag.Parse()
	if !filepath.IsAbs(*configPath) {
		fail("--config must be absolute")
	}
	if *initConfig {
		publicKey, err := mirrornode.Bootstrap(mirrornode.BootstrapOptions{
			ConfigPath: *configPath, StateDirectory: *stateDirectory, Node: *node,
			Owner: *owner, Repository: *repository, Upstream: *upstream,
			CatalogURL: *catalogURL, PublicOrigin: *publicOrigin,
			RouterPublicKey: *routerPublicKey, GatewayScript: *gatewayScript,
			Python: *python, Cloudflared: *cloudflared, Version: *version,
		})
		if err != nil {
			fail(err.Error())
		}
		fmt.Println(publicKey)
		return
	}
	cfg, err := mirrornode.LoadConfig(*configPath)
	if err != nil {
		fail(err.Error())
	}
	gateway, _, err := mirrornode.LoadGatewayConfigForCommand(cfg.GatewayConfig)
	if err != nil {
		fail(err.Error())
	}
	if *registerLinkCode != "" {
		identity, err := mirrornode.LoadIdentity(cfg.IdentityKey)
		if err != nil {
			fail(err.Error())
		}
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()
		if err := mirrornode.RegisterNodeAccount(ctx, *accountsURL, gateway.Node.Name, *registerLinkCode, identity); err != nil {
			fail("register node: " + err.Error())
		}
		fmt.Println("node account registered")
		return
	}
	if *signHealth || *verifyCapability || *signManifest || *printPublicKey {
		identity, err := mirrornode.LoadIdentity(cfg.IdentityKey)
		if err != nil {
			fail(err.Error())
		}
		if *printPublicKey {
			fmt.Println(identity.PublicKey())
			return
		}
		if *signManifest {
			if err := mirrornode.RunManifestSigner(os.Stdin, os.Stdout, identity); err != nil {
				fail("manifest signer: " + err.Error())
			}
			return
		}
		if *signHealth {
			if err := mirrornode.RunHealthSigner(os.Stdin, os.Stdout, identity, gateway.Node.Name); err != nil {
				fail("health signer: " + err.Error())
			}
			return
		}
		if err := mirrornode.RunCapabilityVerifier(os.Stdin, os.Stdout, gateway.Node.Name, gateway.RouterPublicKey); err != nil {
			fail("capability verifier: " + err.Error())
		}
		return
	}
	if err := mirrornode.ValidateRuntime(cfg); err != nil {
		fail(err.Error())
	}
	if *check {
		fmt.Println("mirror-node configuration OK")
		return
	}
	if err := os.Setenv("FORKMESH_MIRROR_NODE_CONFIG", *configPath); err != nil {
		fail(err.Error())
	}
	daemon, err := mirrornode.NewDaemon(cfg)
	if err != nil {
		fail(err.Error())
	}
	executable, err := os.Executable()
	if err != nil {
		fail(err.Error())
	}
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	if err := daemon.Run(ctx, executable); err != nil {
		fail(err.Error())
	}
}

func fail(message string) { fmt.Fprintln(os.Stderr, "forkmesh-mirror-node:", message); os.Exit(2) }
