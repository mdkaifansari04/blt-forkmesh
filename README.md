# ForkMesh

A distributed source-code preservation and collaboration network.

ForkMesh is an open-source desktop node and relay prototype for hosting,
mirroring, discovering, and discussing software projects without depending on
one central code-hosting company.

## What Exists Now asdf

- Qt 6 desktop node in `qt_client/`
- Python Cloudflare Worker relay in `cloudflare_worker/`
- Local Ed25519 identity keys for signed profile and repository metadata
- Local bare Git mirrors via `git clone --mirror` and `git fetch --prune`
- Encrypted relay-room mode for project chat
- Cloudflare Durable Object room relay with no npm/npx project dependencies
- Cloudflare Durable Object repository catalog surfaced on the public website
- Per-repository chat channels derived from mirrored repository names
- Solana address fields on profiles and repositories

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
- Funding: profiles and repositories can publish Solana donation
  addresses.
- Federation: relay nodes can be operated independently, similar in spirit to
  Matrix or Mastodon.
- Mainnodes: hosted nodes can provide encrypted relay rooms, repository
  catalogs, mirror-health indexing, Solana metadata, and contributor, host, and
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

The roadmap now lives in the in-repo issue tracker under [`issues/`](issues/) — a
folder per issue with signed, append-only history, surfaced in the desktop
client's **Issues** tab and editable across nodes. See
[`issues/README.md`](issues/README.md) for the format.

> **Design note (important):** File browsing and clone are served on demand from
> a connected host and nothing is stored on the relay. The website caches what
> you've browsed in localStorage, and the desktop client can keep temporary
> preview mirrors in the Settings-configured preview cache so you can inspect
> code, issues, commits, and pull requests before choosing to mirror or fork.
> This keeps the relay lean and free-plan-hostable.

## Done

- Added a Qt desktop client skeleton for ForkMesh.
- Added local Ed25519 identity support for signed profile and repository
  metadata.
- Added local bare Git mirroring through `git clone --mirror` and
  `git fetch --prune`.
- Added profile and repository Solana address fields.
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
  uptime, synced bytes, SOL earnings, or accepted contributions?

## Mainnode Model

Mainnodes are hosted ForkMesh infrastructure nodes. They do not own user
identity or repository history, but they can provide useful network services:

- encrypted repo-room relay through Durable Objects
- repository catalogs and search indexes
- mirror-health report ingestion
- Solana donation metadata and optional community faucet support
- project, contributor, host, and mainnode leaderboards
- compatibility routes while the protocol evolves

## Security Notes

- Relay chat payloads are encrypted client-side with AES-256-GCM.
- Profile and repository metadata can be signed by the local Ed25519 identity.
- The relay only sees ciphertext envelopes; room contents require the client
  passphrase.

## Roadmap
- consider having the ability for nodes to mirror private repositories - they would have a public (handle) and just the size people can choose to mirror them they would be encrypted with quantum proof encryption
