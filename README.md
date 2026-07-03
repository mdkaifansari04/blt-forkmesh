# ForkMesh

**Code hosting that lives on the network — not in someone else's data center.**

ForkMesh is a peer-to-peer developer platform: host, mirror, browse, discuss,
and *ship* software without handing your code, your identity, or your community
to a single central company. Every developer runs a node. Every node can mirror
any repository. And when the machine that owns a project goes offline, the mesh
keeps serving it.

It is open source, free-plan-hostable at the edge, and already doing real work
today — issues, pull requests, CI-style actions, AI coding agents, on-chain
bounties, and a live public website — all running on the network right now.

> **This is early, and that's the point.** The foundation is built and working.
> The people who show up now help shape the protocol, earn recognition for the
> repositories they preserve, and get their nodes on the leaderboards before the
> mesh fills up. Build a node, mirror a project you care about, and you're
> already part of it.

---

## ✅ What's Working Today

ForkMesh is well past "prototype." Here's what you can do right now:

**Identity & accounts**
- Anonymous accounts secured by a local Ed25519 key + password — no email, no
  central password database.
- Signed profile and repository metadata, verifiable across nodes.

**Hosting & mirroring**
- Local bare Git mirrors via `git clone --mirror` / `git fetch --prune`.
- A public, Worker-served **website** and repository catalog — every repo a node
  publishes is browsable on the web.
- **Browse before you mirror:** explore any repository's files, commits, and
  diffs on demand, streamed from a live host with nothing stored on the relay.
- **Mirror failover:** when the source machine goes offline, another node that
  holds the mirror serves clones and browsing in its place — the URL never
  changes.
- Content-addressed **release** artifacts and tags, with a sha256-verified
  one-line installer.

**Collaboration**
- **Issues** with open/closed states, priorities, custom fields, and signed,
  append-only history that syncs and stays editable across nodes.
- **Pull requests** as signed patch submissions, browsable on the web and in the
  desktop client, with mergeability checks and AI-assisted review.
- **Actions:** run real workflows on push, with the latest live log always one
  click away, rendered in a native in-app terminal with full color and emoji.
- **Encrypted room chat**, per-repository, end-to-end encrypted (AES-256-GCM)
  before it ever reaches the relay. The relay only ever sees ciphertext.

**AI agents, built in**
- Assign any issue to **Claude Code** or **Codex** straight from the issue view.
  The agent works on a connected fork and opens a real pull request when it's
  done.
- Run **multiple agents in parallel**, resume past sessions, and watch live
  activity indicators as they work.
- Kick off agents from your editor with the **IDE extension**.

**Funding**
- **Solana bounties:** fund any issue with one click. ForkMesh watches the
  chain, confirms the deposit, and pays out to the contributor when their PR is
  merged.
- Profiles and repositories can publish Solana donation addresses.

**Reach**
- A **Flutter mobile app** — carry the mesh in your pocket and mirror on your
  phone.
- QR handoffs, node profile pages, and network presence throughout.

The mesh currently runs on a Cloudflare Python Worker relay (Durable Objects,
no npm/npx/TypeScript project dependencies) and a Qt 6 desktop node — see the
[CHANGELOG](CHANGELOG.md) for the full release-by-release story.

---

## 🔜 What's Coming

The roadmap is public and lives in the in-repo issue tracker under
[`issues/`](issues/) — a folder per issue with signed, append-only history,
surfaced in the desktop client's **Issues** tab and editable across nodes. See
[`issues/README.md`](issues/README.md) for the format.

Highlights on the horizon:

- **Fully federated mainnodes** — run your own relay/index/funding node, Matrix-
  and Mastodon-style, and settle on a canonical cross-node repository identity.
- **Private mirrors with post-quantum encryption** — nodes advertise a public
  handle and size only; contents stay encrypted, and others can choose to help
  preserve them without ever reading them.
- **Leaderboards** for contributors, hosts, and nodes — ranked by hosted uptime,
  mirror count, synced bytes, SOL earned, and accepted contributions.
- **Richer search and indexing** across the catalog.

> **Open questions we're deciding in the open:** the exact cross-node repository
> route shape (`domain:owner/repo`, `owner@domain/repo`, or another), and which
> metrics should power the first leaderboards. Show up and weigh in.

---

## Get Started

### Desktop Node

Requirements: Qt 6.4+ (Widgets, Network), CMake 3.16+, OpenSSL dev headers, Git,
a C++17 compiler, and the `openssl` CLI at runtime for the LAN TLS certificate.

```sh
cd qt_client
./run.sh          # build and run
./run.sh test     # headless backend smoke test
```

The client creates an Ed25519 identity key, a LAN-encryption TLS certificate,
and bare mirrors under its app data directory. Use **+ Add** on the **Repos**
page to pick a local repo or paste a remote clone URL. Repositories you select
are published to the mainnode catalog and appear on the ForkMesh website —
without ever exposing local filesystem paths.

The desktop client starts with no setup input and connects to the ForkMesh
mainnode automatically.

### Cloudflare Relay

The relay hosts encrypted room WebSockets and the signed repository catalog:

```text
/api/repo/{owner}/{repo}/rooms/{room}/ws     # per-repo encrypted chat
/api/repositories                            # signed catalog records
```

The Durable Object only relays ciphertext to connected clients and never
persists message bodies. (`/api/room/{room}/ws` remains as a temporary
compatibility route.)

Run and deploy with Cloudflare's Python Worker tooling:

```sh
cd cloudflare_worker
uvx --from workers-py pywrangler dev      # local
uvx --from workers-py pywrangler deploy   # deploy
```

Local relay testing URL:

```text
ws://127.0.0.1:8787/api/repo/mainnode/forkmesh/rooms/general/ws
```

---

## Repository Layout

```text
forkmesh/
  qt_client/          Qt 6 desktop node
  cloudflare_worker/  Python Worker relay + public website
  flutter_app/        Mobile app
  ide_extension/      Editor integration for ForkMesh agents
  issues/             In-repo, signed issue tracker (the live roadmap)
  releases/           Release metadata and artifacts
  CHANGELOG.md        Human-readable, release-by-release history
```

---

## Core Model

- **Identity:** local Ed25519 keys instead of passwords.
- **Repositories:** signed metadata plus Git remotes.
- **Mirrors:** any node can host a bare mirror of any repository, and serve it
  when the source is offline.
- **Chat:** repository communities talk through encrypted mainnode relay rooms.
- **Funding:** profiles, repositories, and issues carry Solana addresses and
  bounties.
- **Federation:** relay nodes run independently, in the spirit of Matrix or
  Mastodon.
- **Mainnodes:** hosted nodes provide encrypted relay rooms, repository
  catalogs, mirror-health indexing, Solana metadata, and leaderboards — without
  ever owning user identity or repository history.

## Design Principles

- **Git first.** Chat, agents, and bounties exist to serve repositories,
  mirrors, issues, releases, and maintainer coordination.
- **Lean, free-hostable edge.** Nothing is stored on the relay that doesn't have
  to be; browsing and clone are served on demand from a connected host, and the
  relay stays inside a free Cloudflare plan.
- **Repo-centric routes.** The API leads with repository identity, not generic
  room names.
- **No heavy edge toolchains.** Cloudflare code is Python Workers + `pywrangler`
  — no npm, npx, or TypeScript project dependencies.

## Security Notes

- Relay chat payloads are encrypted client-side with AES-256-GCM; the relay only
  sees ciphertext envelopes and cannot read room contents.
- Profile and repository metadata are signed by the local Ed25519 identity.

---

**Ready to join the mesh?** Build a node, mirror a project you love, open an
issue, and hand it to an agent. The network is small enough that you'll matter,
and far enough along that you'll ship something real today.
