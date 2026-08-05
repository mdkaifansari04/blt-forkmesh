package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"

	mirrornode "forkmesh.com/mirror-node"
)

func main() {
	configPath := flag.String("config", "", "absolute mirror-node configuration path")
	signHealth := flag.Bool("sign-mirror-health", false, "run the bounded gateway health signer")
	verifyCapability := flag.Bool("verify-mirror-capability", false, "run the bounded gateway capability verifier")
	check := flag.Bool("check", false, "validate configuration and runtime dependencies")
	flag.Parse()
	if !filepath.IsAbs(*configPath) {
		fail("--config must be absolute")
	}
	cfg, err := mirrornode.LoadConfig(*configPath)
	if err != nil {
		fail(err.Error())
	}
	gateway, _, err := mirrornode.LoadGatewayConfigForCommand(cfg.GatewayConfig)
	if err != nil {
		fail(err.Error())
	}
	if *signHealth || *verifyCapability {
		identity, err := mirrornode.LoadIdentity(cfg.IdentityKey)
		if err != nil {
			fail(err.Error())
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
