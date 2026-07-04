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
- **No desktop client required:** repo owners can create agent sessions, send
  prompts, and assign issues to an agent straight from the website.

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
  tools/              Standalone helpers (MCP server, PR review)
  CHANGELOG.md        Human-readable, release-by-release history
```

### MCP server

`tools/forkmesh_mcp_server.py` exposes the mesh to any MCP-capable agent
(Claude Code, Codex, …) as tools over the stdio transport: `list_repos`,
`read_file`, `search_issues`, `create_issue`, `comment_on_issue`,
`open_pr_from_branch`, and `get_pr_diff`. The write tools sign with the node
identity key and produce the exact same native `issues/` and `pulls/` entries
the desktop node writes — no privileged side door. The `.mcp.json` at the repo
root registers it so Claude Code discovers it automatically. Run
`python3 tools/test_forkmesh_mcp_server.py` to exercise it end-to-end.

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

---

**Ready to join the mesh?** Build a node, mirror a project you love, open an
issue, and hand it to an agent. The network is small enough that you'll matter,
and far enough along that you'll ship something real today.
