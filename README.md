# ForkMesh

A distributed source-code preservation and collaboration network.

ForkMesh is an open-source desktop node and relay prototype for hosting,
mirroring, discovering, and discussing software projects without depending on
one central code-hosting company.

## What Exists Now

- Qt 6 desktop node in `qt_client/`
- Python Cloudflare Worker relay in `cloudflare_worker/`
- Local Ed25519 identity keys for signed profile and repository metadata
- Local bare Git mirrors via `git clone --mirror` and `git fetch --prune`
- Encrypted relay-room mode for project chat
- Cloudflare Durable Object room relay with no npm/npx project dependencies
- Cloudflare Durable Object repository catalog surfaced on the public website
- Per-repository chat channels derived from mirrored repository names
- Bitcoin Cash address fields on profiles and repositories

This is an MVP. It does not yet replace a full forge like GitHub or GitLab,
but it lays down the working identity, mirroring, encrypted chat, and relay
foundation described in the original project notes.

## Repository Layout

```text
forkmesh/
  qt_client/          Qt desktop node
  cloudflare_worker/  Encrypted WebSocket relay for federated rooms
  CHANGELOG.md        Human-readable change history
```

## Desktop Node

Requirements:

- Qt 6.4+ Widgets and Network
- CMake 3.16+
- OpenSSL development headers
- Git
- A C++17 compiler
- `openssl` CLI at runtime for the LAN TLS certificate

Build and run:

```sh
cd qt_client
./run.sh
```

Run the headless backend smoke test:

```sh
cd qt_client
./run.sh test
```

The client creates:

- an Ed25519 identity key under the app data directory
- a TLS certificate for LAN peer encryption
- bare mirrors under the app data directory in `mirrors/`

Use `+ Add` on the **Repos** page (left navigation) to choose a local Git
repository or enter a remote clone URL. Repositories selected for the network
are published to the mainnode catalog at `/api/repositories` and appear on the
ForkMesh website without exposing private local filesystem paths.

## Cloudflare Relay

The relay hosts room WebSockets at:

```text
/api/repo/{owner}/{repo}/rooms/{room}/ws
```

The desktop client encrypts payloads before sending them. The Durable Object
only relays ciphertext to currently connected clients and does not persist
message bodies. The older `/api/room/{room}/ws` route remains as a temporary
compatibility endpoint.

The Worker also stores signed repository catalog records at:

```text
/api/repositories
```

Run locally with Cloudflare's Python Worker tooling:

```sh
cd cloudflare_worker
uvx --from workers-py pywrangler dev
```

The desktop client starts without setup input and connects to the ForkMesh mainnode:

```text
wss://forkmesh.com/api/repo/mainnode/forkmesh/rooms/general/ws
```

Use this client server URL for local relay testing:

```text
ws://127.0.0.1:8787/api/repo/mainnode/forkmesh/rooms/general/ws
```

Deploy:

```sh
cd cloudflare_worker
uvx --from workers-py pywrangler deploy
```

## Core Model

ForkMesh is built around these concepts:

- Identity: users generate local Ed25519 keys instead of passwords.
- Repositories: projects are represented by signed metadata and Git remotes.
- Mirrors: any node can host a bare mirror of any repository.
- Chat: repository communities talk through Matrix-like encrypted mainnode
  relay rooms.
- Funding: profiles and repositories can publish Bitcoin Cash donation
  addresses.
- Federation: relay nodes can be operated independently, similar in spirit to
  Matrix or Mastodon.
- Mainnodes: hosted nodes can provide encrypted relay rooms, repository
  catalogs, mirror-health indexing, BCH metadata, and contributor, host, and
  node leaderboards.

## Direction

This is the living plan for turning the README concept into the actual
project. It keeps the important scratch notes in structured form so they stay
visible while the code evolves.

- ForkMesh is a Git app first. Chat exists to support repositories, mirrors,
  issues, releases, and maintainer coordination.
- Avoid npm, npx, and TypeScript project dependencies. Cloudflare edge code
  should be Python Workers plus `pywrangler`.
- Use `mainnode` rather than `masternode` for hosted relay, index, funding,
  and coordination nodes.
- Move away from LAN mesh as a primary product model. The preferred direction
  is Matrix-like encrypted chat through durable mainnode relay rooms.
- Make routes repo-centric. A repository may have rooms, but the API should
  lead with repository identity rather than generic room names.

## Todo

> **Design note (important):** File browsing and clone are **pure live** — data
> is served on demand from a connected host and nothing is stored on the relay.
> If no host is online the repo is unavailable (the website caches what you've
> browsed in localStorage). This keeps the relay lean and free-plan-hostable.

1. **Anti-spoofing / security** — require a node-key-signed token on `/host` + catalog/files publish (verified against account pubkey via `ed25519_verify`); rate-limit messages/host requests/catalog POSTs; lower the 96 MB room-frame cap; add catalog anti-spam.
2. **Pull requests + signed collaboration objects** — one node's repo can PR to another's, with changes visible in client + web. Build append-only signed objects for issues, comments, pull requests, releases, endorsements, and mirror-health reports.
3. **Fork from client** — fork a repo into your own directory; basis for opening a PR.
4. **Signup + account UI** — new auth model: register through the client, verify an email + a BCH address (replaces passwordless Ed25519-only). Build website signup page (node-signed, unique name + email), in-node Account UI (Settings) with "Register node name" via `signData()` and login/status, and enforce `^[a-z][a-z0-9]*$` on the client handle.
5. **Email sending from main node** — needed for email verification; evaluate free senders (Mailgun, etc.).
6. **BCH registration + faucet flow** — 24-hour BCH transaction check plus a community faucet path for users without BCH.
7. **@mention alerts** — native desktop notification when mentioned.
8. **Auto-update mirrors** — mirrors refresh automatically when the owner's repo updates (owner = first to create the repo on their node).
9. **Private repos** — shareable to other nodes but hidden from the website unless logged in.
10. **Extend mainnode repository catalogs** — add search, tags, host count, and latest signed mirror-health reports.
11. **Leaderboards** — longest hosted repos, most mirrored repos, mainnode uptime, contributor activity, and BCH received by projects/contributors/mainnodes.
12. **Mirror storage location setting** — let the user choose which directory mirrors are stored in.
13. **Launch-on-boot setting** — auto-restart the node when the computer restarts.
14. **Live activity chart** — per-second chart on Home (`#homeGraph`/`#homeScoreBoard` styles started).
15. **Federation & routing** — multi-relay federation and room discovery, optional Tor/relay routing modes, mirror bandwidth and storage accounting, and import/export of signed project metadata alongside Git history.
16. **Set a real `ACCOUNTS_KEY` secret in prod** (`pywrangler secret put ACCOUNTS_KEY`) before relying on email encryption.
17. **Keep this list current** — every meaningful implementation pass updates `Done` and this Todo, and records user-visible changes in [CHANGELOG.md](CHANGELOG.md).

## Done

- Added a Qt desktop client skeleton for ForkMesh.
- Added local Ed25519 identity support for signed profile and repository
  metadata.
- Added local bare Git mirroring through `git clone --mirror` and
  `git fetch --prune`.
- Added profile and repository Bitcoin Cash address fields.
- Added per-repository chat channels in the Qt client.
- Added a Python Cloudflare Worker relay with a Durable Object room class.
- Removed npm, npx, TypeScript, package-lock, and node_modules from the worker
  project.
- Updated docs to use Python Worker tooling.
- Replaced generic room-first relay URLs with repo-centric mainnode URLs:

  ```text
  /api/repo/{owner}/{repo}/rooms/{room}/ws
  ```

- Kept `/api/room/{room}/ws` as a temporary compatibility endpoint.
- Renamed the Durable Object binding to `FORKMESH_MAINNODE_ROOM`.
- Removed LAN mesh from the Qt client build and setup flow.
- Replaced the mesh backend test with a focused identity and room-crypto smoke
  test.
- Defined the first mainnode capability model in docs.
- Added a public Worker-served website and a Durable Object repository catalog
  at `/api/repositories`.
- Added local-repository publishing from the Qt client so selected mirrors can
  appear on the public website without sending local filesystem paths.

## Open Questions

- What exact route should identify a repository across federated mainnodes:
  `domain:owner/repo`, `owner@domain/repo`, or another shape?
- Which metrics should power the first leaderboards: hosted time, mirror count,
  uptime, synced bytes, BCH earnings, or accepted contributions?

## Mainnode Model

Mainnodes are hosted ForkMesh infrastructure nodes. They do not own user
identity or repository history, but they can provide useful network services:

- encrypted repo-room relay through Durable Objects
- repository catalogs and search indexes
- mirror-health report ingestion
- BCH donation metadata and optional community faucet support
- project, contributor, host, and mainnode leaderboards
- compatibility routes while the protocol evolves

## Security Notes

- Relay chat payloads are encrypted client-side with AES-256-GCM.
- Profile and repository metadata can be signed by the local Ed25519 identity.
- The relay only sees ciphertext envelopes; room contents require the client
  passphrase.
