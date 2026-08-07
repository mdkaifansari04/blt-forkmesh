# Mirror node service

`forkmesh-mirror-node` is the small, headless runtime for public mirror hosts.
It replaces Git synchronization, gateway configuration, gateway/tunnel process
supervision, signed health/capability helpers, catalog publication, telemetry,
and local readiness reporting that previously ran inside the Qt event loop.

The service binds its operator API to loopback only:

- `GET /healthz` reports that the daemon event loop is alive;
- `GET /readyz` requires a running gateway and a recent successful sync; and
- `GET /v1/status` returns bounded, secret-free process, restart, sync, and
  repository-ref state.

The existing gateway stays on `127.0.0.1:8790` behind Cloudflare Tunnel. The
daemon reuses the existing owner-only Ed25519 identity and connector token; it
never accepts either value in argv, JSON, an HTTP request, or a catalog record.
Its two gateway helper modes accept only the bounded ForkMesh health and masked
proxy protocols on stdin.

During the intake migration, the daemon holds one WebSocket to the relay's
per-owner node event channel (`wss://…/api/nodes/events`, the ForkMeshNodes
Durable Object) authenticated with the node's own Ed25519 identity. The relay
pushes a payload-free `{"type":"event","topic","repo"}` frame the instant an
issue, pull, or discussion submission lands; the daemon starts the Qt relay
lease/materialization bridge on that push and stops it `intakeIdleGrace`
(default 2m) after the last one. There is **no** polling and no fallback poll:
`GET /api/repo/*/pending` is never called (`intakePollInterval` is parsed but
ignored). Missed pushes are covered by the socket itself — every reconnect
fires one catch-up wake that drains whatever queued while the channel was
down. `FORKMESH_EXTERNAL_MIRROR_NODE=1` keeps the short-lived worker from
fetching Git, publishing catalog state, or supervising gateway/tunnel children.
There is no resident Qt service and therefore no idle Qt memory cost.

Repository sync rides the same channel: the relay pushes a `commits` event
when a source node publishes a moved public head, and the daemon fetches on
that push (plus one catch-up per reconnect). The `syncInterval` fetch ticker
only runs when an upstream lives outside the relay (a third-party git host
that cannot push) or when the catalog — and therefore the channel — is
disabled. A four-minute heartbeat cycle still republishes the catalog record
and renews the HTTPS endpoint lease from local refs reads; that is a liveness
proof, not a poll.

## Build and test

```sh
cd mirror_node
go test -race -cover ./...
CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' \
  -o forkmesh-mirror-node ./cmd/forkmesh-mirror-node
```

## Desktop companion

Vultr/host deploys upload the deploying desktop's own local
`forkmesh-mirror-node`, looked up in `PATH`, beside the running executable,
beside the installed client (`~/.local/bin/forkmesh`), then in the source
checkout. The desktop installer (`qt_client/install.sh`), the public release
installer, and both in-app update flows (the source rebuild and the prebuilt
auto-update) install or refresh the companion beside the client. A desktop
whose Vultr install fails with "The Go mirror-node package is incomplete
(forkmesh-mirror-node is missing)" therefore heals by running Update & restart
(with a Go toolchain available for source installs) or by reinstalling a
release that publishes the `forkmesh-mirror-node-<os>-<arch>` asset.

Use `packaging/systemd/mirror-node.json.example` as the configuration template.
Every upstream list may contain multiple URLs; the daemon tries them in order,
and the public ForkMesh URL itself routes across the currently healthy mirror
set rather than pinning mirror2.

## Rolling migration

1. Install the binary, configuration, and unit without stopping the old node.
2. Run `forkmesh-mirror-node --config ... --check` as `forkmesh-node`.
3. Start `forkmesh-mirror-node` and confirm `/readyz`.
4. Disable the resident `forkmesh-node`/`forkmesh-qt-intake` compatibility unit;
   the Go daemon now starts its binary on demand.
5. Confirm signed external health, pending-count polling, and one clone before
   moving to the next host.

Keep the prior unit and binaries as rollback artifacts until the whole fleet has
passed reachability checks.
