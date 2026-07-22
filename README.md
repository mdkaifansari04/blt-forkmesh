# ForkMesh

**ForkMesh lets developers collaborate on, mirror, and preserve source-code repositories across independent hosts—without relying on any single hosting provider.**

ForkMesh is a peer-to-peer developer platform for hosting, browsing, discussing, and shipping software without handing your code, identity, or community to one central company. A developer can run a node, any permitted node can mirror a repository, and verified mirrors keep the project available when its source host goes offline.

It is open source, free-plan-hostable at the edge, and already doing real work today — issues, pull requests, CI-style actions, AI coding agents, on-chain bounties, and a live public website — all running on the network right now.

> **This is early, and that's the point.** The foundation is built and working. The people who show up now help shape the protocol, earn recognition for the repositories they preserve, and get their nodes on the leaderboards before the mesh fills up. Build a node, mirror a project you care about, and you're already part of it.

**Docs:** [forkmesh.com/docs](https://forkmesh.com/docs) — the protocol spec, desktop/relay/mobile/IDE build notes, and the [changelog](https://forkmesh.com/changelog).

---

## ✅ What's Working Today

ForkMesh is well past "prototype." Here's what you can do right now:

**Identity & accounts**
- Accounts can start on the web with email/password or on desktop with a local Ed25519 key.
- Key-bound actions remain signed and verifiable across nodes.
- Signed profile and repository metadata, verifiable across nodes.

**Hosting & mirroring**
- Local bare Git mirrors via `git clone --mirror` / `git fetch --prune`.
- A public, Worker-served **website** and repository catalog — every repo a node publishes is browsable on the web.
- **Browse before you mirror:** explore any repository's files, commits, and diffs on demand, streamed from a live host with nothing stored on the relay.
- **Mirror failover:** when the source machine goes offline, another node that holds the mirror serves clones and browsing in its place — the URL never changes.
- Content-addressed **release** artifacts and tags, with a sha256-verified one-line installer.

**Collaboration**
- **Issues** with open/closed states, priorities, custom fields, and signed, append-only history that syncs and stays editable across nodes.
- **Pull requests** as signed patch submissions, browsable on the web and in the desktop client, with mergeability checks and AI-assisted review.
- **Actions:** run real workflows on push, with the latest live log always one click away, rendered in a native in-app terminal with full color and emoji.
- **Encrypted room chat**, per-repository — messages are AES-256-GCM encrypted client-side, so the relay only ever stores and forwards ciphertext and never keeps plaintext. A default room derives its key from a shared, app-wide constant: that keeps the relay from reading messages at rest, but it is *not* confidential between ForkMesh users — set a room passphrase (shared out of band) when you need a key only the participants know.

**AI agents, built in**
- Assign any issue to **Claude Code** or **Codex** straight from the issue view. The agent works on a connected fork and opens a real pull request when it's done.
- Run **multiple agents in parallel**, resume past sessions, and watch live activity indicators as they work.
- Kick off agents from your editor with the **IDE extension**.
- **No desktop client required:** repo owners can create agent sessions, send prompts, and assign issues to an agent straight from the website.

**Funding**
- **Solana bounties:** fund any issue with one click. ForkMesh watches the chain, confirms the deposit, and pays out to the contributor when their PR is merged.
- Profiles and repositories can publish Solana donation addresses.

**Reach**
- A **Flutter mobile app** — carry the mesh in your pocket and mirror on your phone.
- QR handoffs, node profile pages, and network presence throughout.

The mesh currently runs on a Cloudflare Python Worker relay (Durable Objects, no npm/TypeScript project dependencies in the repo) and a Qt 6 desktop node — see the [changelog](https://forkmesh.com/changelog) for the full release-by-release story.

---

## 🔜 What's Coming

The canonical roadmap is public in [Project #2](.forkmesh/projects/2/project-2.json), with one signed epic per outcome under [`.forkmesh/issues/`](.forkmesh/issues/). The desktop client's **Projects** and **Issues** tabs render the same append-only records, and the [public Projects view](https://forkmesh.com/forkmesh/forkmesh/projects) exposes them without an account.

Highlights on the horizon:

- **Preservation public alpha** — full-fidelity migration, public discovery, verified availability, stable clone paths, release preservation, and an accountless browser journey.
- **Collaboration public beta** — portable identity, authenticated push, complete review, signed collaboration objects, and continuous two-way forge synchronization.
- **Hosted teams and revenue GA** — paid managed network services, private organization workspaces, delivery pipelines, and verifiable availability contracts with a self-hosted exit path.
- **An open, resilient ecosystem** — independently self-hostable services, formal specifications, conformance tests, community governance, offline operation, privacy transports, and disaster recovery.

The 44 epics start from a 30% evidence-based product baseline. None is marked complete until its full acceptance criteria, compatibility tests, security gates, documentation, telemetry, and rollout evidence pass.

---

## Get Started

### Desktop Node

Requirements: Qt 6.4+ (Widgets, Network), CMake 3.16+, OpenSSL dev headers, Git, a C++17 compiler, and the `openssl` CLI at runtime for the LAN TLS certificate.

```sh
cd qt_client
./run.sh          # build and run
./run.sh test     # headless backend smoke test
```

The client creates an Ed25519 identity key, a LAN-encryption TLS certificate, and bare mirrors under its app data directory. Use **+ Add** on the **Repos** page to pick a local repo or paste a remote clone URL. Repositories you select are published to the mainnode catalog and appear on the ForkMesh website — without ever exposing local filesystem paths.

The desktop client starts with no setup input and connects to the ForkMesh mainnode automatically.

Prefer a prebuilt binary over building from source? Use the sha256-verified one-line installer instead:

```sh
curl -fsSL https://forkmesh.com/install.sh | bash
```

### Cloudflare Relay

[![Deploy to Cloudflare](https://deploy.workers.cloudflare.com/button)](https://deploy.workers.cloudflare.com/?url=https://forkmesh.com/forkmesh/forkmesh)


The relay hosts encrypted room WebSockets and the signed repository catalog:

```text
/api/repo/{owner}/{repo}/rooms/{room}/ws     # per-repo encrypted chat
/api/repositories                            # signed catalog records
```

The Durable Object only relays ciphertext to connected clients and never persists message bodies. (`/api/room/{room}/ws` remains as a temporary compatibility route.)

Run and deploy with Cloudflare's Python Worker tooling:

```sh
cd cloudflare_worker
uvx --from workers-py pywrangler dev      # local
uvx --from workers-py pywrangler deploy   # deploy
```

The ForkMesh deploy pipeline runs `cloudflare_worker/deploy.sh`, which uses `pywrangler` and bootstraps the local tool when needed.

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
  .forkmesh/          In-repo signed data: issues (the live roadmap),
                      discussions, commit comments, release metadata, workflows
  tools/              Standalone helpers (MCP server, PR review)
```

Developer docs (protocol spec, per-project build notes, changelog) live on the website at [forkmesh.com/docs](https://forkmesh.com/docs), not in this repo.

### MCP server

`tools/forkmesh_mcp_server.py` exposes the mesh to any MCP-capable agent (Claude Code, Codex, …) as tools over the stdio transport: `list_repos`, `read_file`, `search_issues`, `create_issue`, `comment_on_issue`, `open_pr_from_branch`, and `get_pr_diff`. The write tools sign with the node identity key and produce the exact same native `issues/` and `pulls/` entries the desktop node writes — no privileged side door. The `.mcp.json` at the repo root registers it so Claude Code discovers it automatically. Run `python3 tools/test_forkmesh_mcp_server.py` to exercise it end-to-end.

---

## Core Model

- **Identity:** local Ed25519 keys for signed protocol actions, with email/password login for web and cross-device account access.
- **Repositories:** signed metadata plus Git remotes.
- **Mirrors:** any node can host a bare mirror of any repository, and serve it when the source is offline.
- **Chat:** repository communities talk through encrypted mainnode relay rooms.
- **Funding:** profiles, repositories, and issues carry Solana addresses and bounties.
- **Federation:** relay nodes run independently, in the spirit of Matrix or Mastodon.
- **Mainnodes:** hosted nodes provide encrypted relay rooms, repository catalogs, mirror-health indexing, Solana metadata, and leaderboards — without ever owning user identity or repository history.

## Design Principles

- **Git first.** Chat, agents, and bounties exist to serve repositories, mirrors, issues, releases, and maintainer coordination.
- **Lean, free-hostable edge.** Nothing is stored on the relay that doesn't have to be; browsing and clone are served on demand from a connected host, and the relay stays inside a free Cloudflare plan.
- **Repo-centric routes.** The API leads with repository identity, not generic room names.
- **No heavy edge toolchains in-repo.** Cloudflare code is Python Workers + `pywrangler`; no npm or TypeScript project dependencies are committed.

## Security Notes

- Relay chat payloads are encrypted client-side with AES-256-GCM; the relay only ever stores and forwards ciphertext envelopes. Default rooms use a shared, app-wide key, so this protects message contents at rest on the relay but is not confidential between users — use a room passphrase for participant-only confidentiality.
- Profile and repository metadata are signed by the local Ed25519 identity.

## Roadmap

**Product promise:** ForkMesh lets developers collaborate on, mirror, and preserve source-code repositories across independent hosts—without relying on any single hosting provider.

The full prototype loop works today: publish a repository, browse and clone it on the web, open signed issues, review pull requests, run actions, assign coding agents, and fund work. The remaining gaps are substantial: delegated protected push, complete collaboration convergence, full-fidelity migration, multi-provider synchronization, independent service operation, private-team key lifecycle, and commercial service operations do not yet meet their final acceptance gates.

The canonical [2026–2027 project](.forkmesh/projects/2/project-2.json) links 44 outcome epics, each with scope, measurable acceptance criteria, dependencies, dates, a unique priority, an evidence-based progress baseline, and product or business measures.

### Milestones

1. **Product contract & measurement — August 14, 2026.** Freeze the promise, free/paid boundary, success measures, terminology, dependency map, comparison standard, and optional-incentive invariant.
2. **Preservation public alpha — October 30, 2026.** Make full-fidelity migration, accountless browsing, public discovery, Git compatibility, releases, availability proof, secret safety, installation, and daily UX dependable.
3. **Collaboration public beta — January 29, 2027.** Complete signed objects, recoverable identity, protected push, pull-request review, two-way forge sync, social discovery, and scoped community trust.
4. **Hosted teams & revenue GA — April 30, 2027.** Launch managed network services, encrypted private organizations, operator policy, delivery pipelines, dependable node operations, support, billing, and availability contracts.
5. **Open platform & ecosystem — July 30, 2027.** Publish stable interfaces and formal specifications, self-host every role, prove interoperability, establish governance, and fund ecosystem work transparently.
6. **Adversarial resilience & disaster readiness — September 30, 2027.** Prove offline, low-bandwidth, privacy-route, removable-media, primary-service-loss, and recovery workflows through observed drills.

Commercial work intentionally overlaps product work: pricing interviews and cost measurement start with the product contract, hosted-service trials begin during public alpha, and paid relay/index/backup/sync enters beta alongside collaboration. Organization and availability products reach GA only after their security, recovery, policy, service-level, and support gates pass.

Progress is based on shipped evidence, not effort: specification 10%, core journey 35%, interoperability and edge cases 15%, security/privacy 10%, automated validation 15%, UX/docs/operations 10%, and telemetry/rollout 5%. An epic cannot exceed 85% without end-to-end validation or 95% without security, documentation, telemetry, and production-rollout evidence. A milestone exits only when all of its P0 epics are closed, compatibility tests are green, and no critical security or data-loss defect remains.

---

**Ready to join the mesh?** Build a node, mirror a project you love, open an issue, and hand it to an agent. The network is small enough that you'll matter, and far enough along that you'll ship something real today.
