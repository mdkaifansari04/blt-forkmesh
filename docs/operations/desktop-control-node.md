# ForkMesh desktop control node

The Qt desktop client includes a **Control** page in its main navigation. It is
the operator surface for the current device; it does not turn ForkMesh into a
custodian.

## Local mirror operations

The status card shows the relay connection, tracked mirror paths, repositories
configured for direct-HTTPS serving, and bounded HTTPS sync jobs.

- **Start mirror serving** brings the local node online and starts eligible
  direct-HTTPS mirror gateways. Repository changes are discovered through the
  existing bounded HTTPS sync; no per-repository persistent socket is opened.
- **Stop mirror serving** stops repository serving and the node heartbeat. It
  does not delete local repositories and does not disconnect chat.
- **Sync all mirrors** runs the same asynchronous mirror fetch path used by the
  regular repository UI.
- **Run health check** verifies the local Ed25519 identity, Git executable,
  Cloudflare bootstrap source, mirror paths, and relay connection.
- **Open full local log** opens the existing durable device log. Cloudflare
  process output stays in the Control page's bounded, in-memory output pane.

The permissions table controls each local repository:

- **Private** controls public discovery and authorized browse/clone access.
- **Serve** controls whether this device publishes and serves its mirror.
- **Run actions** controls whether trusted `.forkmesh` workflows may run locally.
- **Secret scan** controls push protection for high-confidence credentials.

Only a device holding the repository owner's signing identity can change
visibility or network-serving permission. Permission changes are recorded in the
local audit log. Private repository names and contents are not sent by the
control page to unrelated services.

## Identity and wallet

ForkMesh's Ed25519 identity key is generated and used on the local device with
owner-only filesystem permissions. **Back up or import identity key** reuses the
existing passphrase-encrypted keyfile workflow.

The wallet field accepts only a base58 Solana public address that decodes to 32
bytes. It rejects seed phrases, recovery phrases, and private-key material.
ForkMesh stores the public payout address only:

- ForkMesh does not create or hold a wallet for the user.
- Wallet private keys remain in the user's external self-custodial wallet.
- Connecting an address does not authorize a transfer.
- Displayed balances and visual rewards are not custody or guaranteed returns.

## Cloudflare one-click deployment

The deployment card invokes ForkMesh's pinned `tools/cloudflare_bootstrap.py`.
A source build uses the checkout; CMake installs and native desktop packages
carry the same `tools/` and `cloudflare_worker/` resource tree, including Worker
source, generated/static assets, migrations, and the pinned Wrangler wrapper.
The release flow therefore does not depend on the build machine's source path.

1. For a token scoped to one account and one active zone, leave the topology
   fields empty and paste the least-privilege Cloudflare API token. ForkMesh
   discovers the zone and derives distinct relay and direct-mirror hostnames.
2. If the token can see more than one account or zone, select the account,
   zone, and hostnames in the advanced fields; automatic setup fails closed
   instead of guessing.
3. Use **Validate Worker + mirror** for a read-only dry run, or
   **Deploy Worker + mirror** to
   create/reuse D1, deploy the Worker, configure proxied DNS and the Worker
   route, provision the direct Tunnel, and run health checks.
4. When **Add and connect** is enabled, the successful hostname is added to the
   local relay list and selected.

Credential boundaries:

- The Cloudflare token is never written to `QSettings`.
- The token is never included in process arguments.
- The token is passed only as `CLOUDFLARE_API_TOKEN` in the local child process
  environment.
- The desktop never passes `--secret-env`, so the API token cannot become a
  Worker secret.
- Exact tokens and credential-shaped output are redacted before display.
- Only public deployment fields are saved locally for reuse.
- Generated temporary Wrangler configuration contains public resource IDs only;
  the bootstrapper removes it after the run.
- If the host has no `cloudflared`, the desktop invokes its bundled Python
  installer directly (never `curl | sh`). It downloads Cloudflare release
  `2026.7.2` over HTTPS into owner-only app data, accepts only the supported
  OS/architecture assets and trusted GitHub release hosts, verifies the exact
  repository-pinned SHA-256 archive and executable digests, and atomically
  installs the connector. A managed copy is reverified once per desktop
  process before it is executed.

Python 3 remains an explicit host prerequisite. The Worker deploy wrapper may
fetch its version-pinned `workers-py<1.14.0` and `wrangler@4.42.1` dependencies
on first use; those third-party runtimes are not embedded in ForkMesh.

The bootstrapper asks an external signer for the endpoint manifest. It launches
the same ForkMesh binary with `--sign-mirror-manifest`; that mode reads the
public canonical manifest on standard input, validates its type, target public
key, encoding, and SHA-256 digest, signs with the existing local Ed25519
identity, and emits only the public key and detached signature. The private key
never leaves the desktop process.

## Connected hosts

The Control page links to the existing **Hosts** page, where operators can add a
remote machine, install/update ForkMesh, and view host logs. It also offers a
confirmed fleet action to upload the current binary to saved hosts.

SSH passwords remain on the desktop and are supplied to local SSH tooling via
environment/stdin, not command-line arguments. They are never sent to the
Cloudflare Worker. Prefer dedicated hosts, scoped accounts, and key-based SSH
where available.

## First-instance reward-pool signer

The Control page also contains the explicit local signer for the public
community reward pool. It imports an existing Solana key into a
passphrase-encrypted, owner-only local vault; it never generates or uploads that
key. The desktop verifies every Worker-authored transfer, shows the exact
recipients and amounts for confirmation, signs locally, submits directly to a
configured public HTTPS Solana RPC, waits for finality, and reports only the
public transaction signature to the Worker.

See [Community reward-pool local signer](community-reward-pool-signer.md) for
the vault format, configuration, exact request canonicalization, fail-closed
validation, recovery behavior, and production checklist.

## ForkMesh World

**World** is available in the main navigation and on the Control page. It opens
`https://<active-relay>/world/` in the system browser. Repository HTTP traffic
continues to use ordinary HTTPS; opening the World does not route clone or file
traffic through multiplayer sockets.
