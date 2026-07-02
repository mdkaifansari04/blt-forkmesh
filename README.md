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

This is the honest, holistic plan for getting ForkMesh from a working
prototype to a fully operational product people choose on purpose. Day-to-day
tracking stays in [`issues/`](issues/); this section is the strategic map.

### Where we actually are

The full loop works today: publish a repo from the desktop node, browse and
clone it through the website, open signed issues, hand one to a coding agent,
review the pull request it opens, fund it with a Solana bounty. That is real
and rare. But honesty requires naming the gaps:

- **One mainnode.** The public network runs through a single Cloudflare
  Worker on constrained Durable Objects (small isolates, strict rate limits).
  Past outages came directly from those constraints. "Federation" is still a
  design word, not a deployed reality.
- **No `git push`.** The relay serves `git-upload-pack` only. Publishing
  means syncing a mirror from a local copy. Fine for the preservation model,
  a hard blocker for anyone expecting a forge.
- **The desktop client carries debt.** The UI was one 54k-line file (now
  split), synchronous git work has stalled the GUI thread, and heavy tabs
  needed lazy-loading retrofits. Perf regressions are found by feel, not by
  benchmark.
- **Money paths are young.** Deposit custody and sweeps run on mainnet, but
  the bounty payout on PR merge has not been exercised live, and holding user
  funds has legal weight we have not formally addressed.
- **Operations are manual.** Release publishing has stalled in ways that
  needed hand-holding; deploys depend on one person's credentials.
- **Tests are thin.** Good smoke and window tests exist, but there is no
  continuous end-to-end exercise of the publish → browse → clone → issue →
  PR → payout loop.

### Phase 1 — Zero-surprise reliability

Nothing else matters if the network drops repos or the app freezes. Goal:
bugs don't reach users, and when the network hiccups it heals itself.

- Crash and stall telemetry from the desktop node (opt-in), feeding a
  triage queue — the `stalls.log` diagnostics are the seed.
- Eliminate every synchronous git call on the GUI thread; the watchdog
  should log zero blocking events in normal use.
- Chaos-test mirror failover continuously: kill the source host, verify
  browse/clone keep working from mirrors with integrity pins intact.
- Fully automated release pipeline: version bump → build → sign → publish
  with no step that can silently sit waiting for approval.
- A nightly integration run that drives the whole product loop headlessly
  and fails loudly.
- A public status page and an error budget we hold ourselves to.

### Phase 2 — Performance that feels native

- Cold start to interactive under one second; every list tab paints
  instantly from cheap data and backfills detail asynchronously.
- Relay: stream everything (no full-pack buffering in the isolate), batch
  small-object fetches, cache aggressively at the edge.
- Performance budgets enforced in CI — startup time, tab-open time, relay
  p95 latency — so regressions are caught by machines, not users.

### Phase 3 — A complete forge

Table stakes to be a daily driver, not just a mirror network:

- **`git push`** over the relay (receive-pack gated by signed identity) —
  the single biggest missing primitive.
- **Code review that holds up**: inline comments, review states, and
  re-review flows on pull requests.
- **Search** across code, issues, and PRs on the mesh.
- **Notifications**: subscriptions, mentions, and an email/webhook bridge.
- **Private repositories** as encrypted mirrors: hosts see only an opaque
  handle and size, content is end-to-end encrypted with a hybrid
  post-quantum scheme (e.g. ML-KEM + X25519) so today's ciphertext is not
  tomorrow's plaintext.
- **CI runs** that are declarative, cached, and safe on untrusted pull
  requests.
- **A browser that can participate**: issue and PR interaction from the
  website without installing the desktop node.

### Phase 4 — A real network, not one mainnode

- A written protocol spec, so a mainnode is something anyone can implement
  and run — including a one-command self-hosted deployment.
- Multiple independent mainnodes with catalog convergence and node-to-node
  repo announcement.
- Replication guarantees: every published repo has N healthy mirrors, with
  automatic repair when one disappears.
- Interop with the wider federation trend: Forgejo is shipping
  ActivityPub-based forge federation and ForgeFed is maturing — bridge
  issues and PRs rather than building an island.

### Phase 5 — The agent-native forge

This is the bet. Development in 2026 is visibly shifting from writing code
to orchestrating agents that write code, and no incumbent forge is
simultaneously agent-native, self-sovereign, and able to pay for merged
work. ForkMesh already treats agents as first-class contributors; lean in:

- **Agent identity and provenance**: agents sign their commits and PRs with
  attributable keys, and review UIs show exactly what was machine-authored.
- **Multi-agent workflows** on issues (plan → implement → review) with
  explicit human gates before merge.
- **A bounty-driven agent economy**: a funded issue can be picked up by any
  node's agent; a merged, human-approved PR pays out automatically. That is
  a marketplace for verified fixes, not just a chat bot.
- **An MCP surface** exposing repos, issues, and PRs so any agent tooling —
  not just the built-in integrations — can work the mesh.
- **Review tooling built for agent-scale volume**: queues, risk scoring,
  and machine-attached test evidence, because agents will open more PRs
  than humans can eyeball unaided.

### Phase 6 — Money, trust, and sustainability

- Exercise the full bounty payout live on mainnet, then publish the custody
  design and commission a third-party security audit of keys, custody, and
  the relay.
- Key recovery and rotation: losing a laptop must not mean losing an
  identity or funds.
- Legal review of custody; prefer moving escrow on-chain so ForkMesh is not
  a money transmitter holding user funds.
- Sustainable revenue that aligns with users: the bounty split, hosted
  mainnode capacity, and paid private-mirror storage — never selling data.
- Supply-chain hardening end to end: content-addressed signed releases
  (already shipped), reproducible builds, SBOMs.

### Phase 7 — Something people choose

- Signed installers and auto-update for Windows, macOS, and Linux; the app
  must be a download, not a build.
- Onboarding that delivers value in five minutes without the user needing
  to understand keys, nodes, or relays.
- Real documentation: protocol spec, self-hosting guide, contributor guide.
- Dogfood completely — ForkMesh development already runs on ForkMesh; keep
  every new feature on that path first.

### What "done" looks like

Concrete bars for "fully functioning, no bugs, high performance":

- 99.9% availability of browse and clone for any published repo, even with
  its source node offline.
- Crash-free desktop sessions above 99.5%, and no GUI stall over 200 ms in
  telemetry.
- The entire loop — publish, issue, agent PR, human review, merge, bounty
  payout — exercised automatically every night against production-like
  infrastructure.
- A stranger installs ForkMesh, publishes a repo, and merges an agent's PR
  in under ten minutes without reading the docs.

### Why this shape

The trends this roadmap is built against: self-hosted and federated forges
are growing fast ([Forgejo federation](https://forgejo.org/),
[ForgeFed](https://forgefed.org/)); pure peer-to-peer forges like
[Radicle](https://radicle.dev/) validate the sovereignty demand while
showing that tooling maturity decides adoption; and agent-orchestrated
development is the dominant 2026 shift ([Anthropic's agentic coding trends
report](https://resources.anthropic.com/2026-agentic-coding-trends-report)).
The intersection — a sovereign, federated forge where agents are paid,
attributable contributors — is empty, and it is exactly where ForkMesh
already stands. The roadmap above is the work required to deserve it.
