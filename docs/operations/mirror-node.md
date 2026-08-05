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

During the intake migration, `forkmesh-qt-intake.service` retains only the
relay lease/materialization bridge. Set `FORKMESH_EXTERNAL_MIRROR_NODE=1` so it
does not fetch Git, publish catalog state, or supervise gateway/tunnel children.
Once the relay intake protocol is available in the daemon, that compatibility
unit can be removed without changing the public endpoint.

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

1. Install the binary, configuration, and both units without stopping Qt.
2. Run `forkmesh-mirror-node --config ... --check` as `forkmesh-node`.
3. Stop the old unit, start `forkmesh-mirror-node`, and confirm `/readyz`.
4. Start the intake bridge with `FORKMESH_EXTERNAL_MIRROR_NODE=1`.
5. Confirm signed external health and one clone before moving to the next host.

Keep the prior unit and binaries as rollback artifacts until the whole fleet has
passed reachability checks.
