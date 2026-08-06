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

During the intake migration, the daemon polls the relay's content-free pending
counter every ten minutes, matching that endpoint's edge cache — a faster tick
reads the same cached counts. Anything shorter in `intakePollInterval` is raised
to ten minutes. It starts the Qt relay lease/materialization bridge
only while issue, pull, or discussion work exists and stops it after the queue
drains. `FORKMESH_EXTERNAL_MIRROR_NODE=1` keeps that short-lived worker from
fetching Git, publishing catalog state, or supervising gateway/tunnel children.
There is no resident Qt service and therefore no idle Qt memory cost.

## Build and test

```sh
cd mirror_node
go test -race -cover ./...
CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' \
  -o forkmesh-mirror-node ./cmd/forkmesh-mirror-node
```

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
