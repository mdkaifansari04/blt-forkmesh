# Scaling ForkMesh to the Masses — Durable Object Cost Roadmap

> Goal: keep ForkMesh free to **use** and free to **self-host**, while staying inside
> Cloudflare's free tier (or pushing load off the central account entirely).

This document maps the current Durable Object (DO) usage, explains *why* the free
tier is being exhausted, and lays out a prioritized roadmap. Findings are grounded
in the code as of this writing — file/line references included.

---

## 1. How the system is set up today

Everything runs inside a single Cloudflare Worker ([src/entry.py](src/entry.py)):

| Component | Backed by | Purpose |
|-----------|-----------|---------|
| Static site (`public/`) | Cloudflare Static Assets | Free, not a concern |
| Catalog / accounts / issues / pulls | **D1** (`DB` binding) | Persistent data — already moved off DOs (migration `v8`) |
| `ForkMeshRoom` | **Durable Object** | Encrypted chat rooms + a read-only "live client count" observer socket |
| `ForkMeshHost` | **Durable Object** | Live git/file tunnel to a desktop client mirroring a repo |

The persistent data was already migrated from DOs to D1 (good — see the `v8`
`deleted_classes` block in [wrangler.toml](wrangler.toml)). **The only remaining
DO usage is transient WebSocket relaying**, which is exactly where the cost is now.

### The two live DO classes

- **`ForkMeshRoom`** ([entry.py:1153](src/entry.py#L1153)) — chat relay. Two socket
  roles: chat participants (`_accept`, [:1198](src/entry.py#L1198)) and read-only
  observers that just receive the live client count (`_accept_observer`,
  [:1247](src/entry.py#L1247)).
- **`ForkMeshHost`** ([entry.py:1289](src/entry.py#L1289)) — per-repo tunnel. Desktop
  hosts connect via WebSocket; the site's `/tree`, `/blob`, `/commits`, and git
  clone (`/info/refs`, `/git-upload-pack`) requests are forwarded over that socket
  ([_tunnel](src/entry.py#L1407), [_git](src/entry.py#L1315)).

---

## 2. Why the free tier is burning (root causes, ranked)

### How DO billing actually works (the key mental model)

Durable Objects bill on **two axes**:

1. **Requests** — each `fetch()` into a DO (including the initial WebSocket upgrade).
2. **Duration (GB-seconds)** — wall-clock time the DO is *active in memory*, times
   its memory footprint (~128 MB). **A DO with an open WebSocket is active the whole
   time that socket is open** — unless you use the **WebSocket Hibernation API**.

That second axis is the trap. One DO held alive 24/7 by a single idle WebSocket
burns roughly `86,400 s × 0.128 GB ≈ 11,000 GB-s/day` — which is on the order of an
entire day's free duration budget *for one socket doing nothing*.

> ⚠️ Exact free-tier numbers change over time. Verify current limits at
> Cloudflare's pricing page before sizing — but the *ratios* below hold regardless.

### R1 — No WebSocket Hibernation API (biggest issue) 🔴

Every socket is accepted with the plain `socket.accept()` API
([entry.py:1200](src/entry.py#L1200), [:1249](src/entry.py#L1249),
[:1342](src/entry.py#L1342)) and wired up with `addEventListener` handlers
([:1224](src/entry.py#L1224)). This keeps the DO **resident in memory for the entire
lifetime of every connection**, billing duration continuously even when no messages
flow.

The fix is the **Hibernation API**: accept via `state.acceptWebSocket()` and move the
handlers to `webSocketMessage` / `webSocketClose` / `webSocketError` methods on the
DO. The runtime can then evict the DO from memory while sockets are idle and stop
billing duration — sockets stay open, you pay ~nothing while quiet. For a relay that
is mostly-idle (chat, a connected-but-quiet host), **this alone can cut duration
billing by ~95–99%.**

### R2 — The homepage pins a room DO alive forever 🔴

[static-page.js:72-98](public/static-page.js#L72-L98) (`watchClients`) opens a
persistent observer WebSocket to the `general` room on every homepage load, and
auto-reconnects forever ([:63](public/static-page.js#L63), backoff capped at 30 s).
Combined with R1, **any single open homepage tab anywhere in the world keeps the
`general` `ForkMeshRoom` DO resident 24/7** — the worst-case duration sink described
above. This is almost certainly the dominant line item.

### R3 — Homepage fan-out spins up dozens of DOs per page view 🟠

[static-page.js:119-124](public/static-page.js#L119-L124) (`loadNetworkStats`) fans
out a `fetch` to `/host` for **up to 80 repositories** on every homepage load. Each
hits `ForkMeshHost.idFromName(...)` ([entry.py:1131](src/entry.py#L1131)) → **up to 80
DO requests per visit**, just to render a "hosts online" number. The catalog page does
similar per-repo host probes ([catalog.js:251](public/catalog.js#L251)).

### R4 — All repo browsing and cloning is funneled through a DO 🟠

Every `/tree`, `/blob`, `/commits`, `/commit`, and full git clone is proxied through
`ForkMeshHost` ([entry.py:1336](src/entry.py#L1336), [:1143](src/entry.py#L1143)).
This is inherent to the live-tunnel design, but it means browsing a repo's file tree
in the web UI = N DO requests, and cloning streams an entire packfile through DO
memory in base64 chunks ([:1363-1381](src/entry.py#L1363-L1381)).

### R5 — Reconnect storms 🟡

The observer reconnect ([static-page.js:63](public/static-page.js#L63)) and chat
clients have no "pause when tab hidden" logic. Backgrounded tabs and flaky networks
generate steady upgrade-request churn against the same DOs.

---

## 3. Roadmap

> **Implementation status (updated):** Phase 0 (R2, R3, R4-browse, R5), Phase 1 #7
> (host presence), **WebSocket Hibernation (R1)**, and Phase 3 #13 (admin load panel)
> are **implemented and live**. Hibernation dispatch was verified in production (chat
> broadcast across two clients; see §3.5). Phase 1 #8 (blob caching) is deferred for
> correctness; Phase 2 (federation + WebRTC) is a **design** pending product decisions.

### Phase 0 — Quick wins ✅ IMPLEMENTED

What shipped in this pass (verified with `pywrangler deploy --dry-run`: bundles,
bindings resolve; no production write):

1. ✅ **Killed the per-visit Durable Object fan-out (R3, R4-browse).** New cached
   `/api/network/stats` endpoint ([entry.py](src/entry.py) `network_stats`) returns
   `{repos, hosts, clients}` from D1 + one internal count request, cached at the edge
   via the Cache API for `NETWORK_STATS_TTL` (20 s). The homepage previously fanned
   out up to 80 `/host` requests **per visit**; now a burst of visitors collapses to
   one computation per colo per TTL.

2. ✅ **Removed the always-on homepage observer socket (R2 — the dominant cost).**
   [static-page.js](public/static-page.js) and [catalog.js](public/catalog.js) no
   longer open a permanent observer WebSocket; they poll `/api/network/stats` every
   30 s instead. No open homepage tab can pin a room Durable Object anymore.

3. ✅ **Pause polling when the tab is hidden (R5).** Both pages stop the interval on
   `visibilitychange` (hidden) and resume + refresh on focus.

4. ✅ **Catalog cards no longer probe per-repo (R4-browse).** `/api/repositories`
   now annotates each repo with a `liveHost` flag, computed once server-side from the
   presence table (keyed by the repo's existing blind index), and the response is
   edge-cached for `CATALOG_TTL` (10 s) with cache-busting on publish/delete. Catalog
   cards render from `liveHost`; only the repo a user actually *opens* does a live
   `/host` probe.

5. ✅ **Edge-cached `/api/repositories`** behind the Cache API (see #4).

### Phase 1 — Shrink the DO footprint

7. 🟡 **Separate "presence" from "relay" — host side DONE.** A `host_presence`
   table (blind-indexed `repo_bi`, `ts`) now records live hosts so stats/catalog read
   presence from D1 instead of probing tunnel DOs. Rows are refreshed (throttled) while
   a host is active and self-heal via a `HOST_PRESENCE_STALE_MS` (10 min) window.
   *Remaining:* an "active in the last 10 min" host that goes fully idle drops off the
   count until its next request — exact long-idle presence would want an alarm-based
   heartbeat (deferred; low value for a vanity stat).

8. ⏸️ **Coalesce git browsing / cache blobs — DEFERRED (correctness).** The `/blob`
   API is keyed by **path only** (no commit SHA — see `pullPath("blob", path)` in
   [catalog.js](public/catalog.js)), so edge-caching a blob would serve **stale file
   content after any push**. Safe caching needs an immutable key: have the client pass
   the resolved commit SHA and key the cache by `sha + path` (content is then immutable,
   cache forever). Worth doing for browse-heavy repos, but it's a client+server feature,
   not a quick win — and shipping it wrong is worse than the cost it saves. Deferred
   until after the host tunnel is verified.

9. ✅/⏸️ **Per-DO idle timeouts — largely SUPERSEDED by hibernation.** Hibernation (§3.5)
   already stops idle sockets from billing duration, which was the goal. A blanket
   idle-*close* is actually wrong here (a connected-but-idle host is the "repo is
   available" guarantee; closing chat drops participants), so no further action unless
   abandoned-connection cleanup proves necessary.

### 3.5 — WebSocket Hibernation (R1) ✅ IMPLEMENTED & LIVE

The plan's biggest *duration* win, now shipped. Both DO classes use the Hibernation
API so an idle chat room or a connected-but-idle repo host no longer bills duration
while quiet.

**What was done** (in [entry.py](src/entry.py)):
- `ForkMeshRoom` and `ForkMeshHost` accept sockets via `self.ctx.acceptWebSocket(ws,
  [tag])` and are serviced by `webSocketMessage` / `webSocketClose` / `webSocketError`
  handlers (no more `addEventListener`/`create_proxy`).
- Connection state is read from `self.ctx.getWebSockets(tag)` rather than instance
  lists (which don't survive eviction). Per-host RTT and the chat sender-id ride in the
  socket's `serializeAttachment`.
- The host DO's `pending`/`git_buffers` stay in memory — they only ever hold an
  *in-flight* request, which keeps the DO active, so no eviction occurs mid-request.
- The retired observer/count socket path was removed; `/clients` over WebSocket now
  returns `410` (the count is served over HTTP via `/api/network/stats`).

**Verification (production):** `/clients` WS → `410` confirms the new code is live; a
two-client test against `/rooms/general/ws` showed peer B receiving peer A's message
and A correctly getting no echo — i.e. `webSocketMessage` dispatch, `getWebSockets`
broadcast, and `serializeAttachment` sender-skip all work. The Python Workers runtime
*does* dispatch hibernation handlers (camelCase); a snake_case alias is kept as a
belt-and-suspenders.

**Remaining check:** the host **tunnel** path (tree/blob/clone) reuses the exact same
hibernation mechanism but was not exercised end-to-end here because it needs a live
desktop host. Connect a host and confirm a `git clone` + file browse before relying on
it under load.

### Phase 2 — Decentralize: the real path to "free for the masses" 🟢 (DESIGN)

The central Cloudflare account is a single shared budget; scaling *its* free tier
only delays the wall. ForkMesh is already a **mesh** (`mainnode`, self-hostable
hosting clients) — lean into that so load doesn't concentrate on one account. This is
the structural answer; it needs product decisions, so it is a design, not yet built.

**10. Central Worker → thin directory + signaling layer.** It already holds only
transient relay state. The end state: the Worker serves the catalog, brokers
connections, and relays *small* control messages — but bulk/long-lived traffic moves
off it (to other nodes or peer-to-peer).

**11. Federation of relay nodes.** Anyone can already self-host the Worker
([deploy.sh](deploy.sh), [.env.production.example](.env.production.example)). Turn that
into a network:
- *Node directory.* A repo's catalog entry records which node(s) host it (the
  `source`/node-name fields and `/api/mainnode` are the seam). Clients resolve a repo
  to a node and connect there.
- *Node selection.* Clients prefer the repo's home node, falling back by
  latency/health. Each operator's free tier carries only their repos → horizontal
  scale across many accounts instead of one.
- *Trust model (decision needed).* Catalog records are already signed (ed25519) and
  data is encrypted at rest with blind indexes — federation must preserve that: a
  node should relay ciphertext it can't read, and clients verify signatures
  end-to-end rather than trusting the node. **Open question for the maintainer:** open
  federation (anyone joins the directory) vs. an allowlist of vetted nodes.

**12. Peer-to-peer tunnels (WebRTC) — removes the largest per-byte cost.** Today every
`/tree`, `/blob`, and full `git clone` streams through `ForkMeshHost` DO memory in
base64 chunks (R4). Move the bytes off the central account:
- *Data channel.* Desktop host ↔ browser establish a WebRTC `RTCDataChannel`; tree/
  blob/packfile bytes flow **directly** peer-to-peer.
- *Worker as signaling broker only.* The DO relays a handful of small SDP/ICE messages
  to set up the channel, then steps out — no packfile ever transits the Worker.
- *Infra decisions needed.* STUN is cheap/free; **TURN** (needed when both peers are
  behind strict NATs) is not — pick a provider or run `coturn`, and treat the existing
  DO tunnel as the fallback when a P2P channel can't be established.
- *Scope.* Start with `git clone` (largest payload, biggest win), then file browsing.

### Phase 3 — Cost controls & observability

**13. ✅ DONE — live load panel on the admin dashboard.** The admin page
([entry.py](src/entry.py) `admin_stats` / `render_admin_html`) now shows the things
that actually hold a Durable Object open — **live hosts** and **chat clients** — plus
catalog size and 24h error count. That's the at-a-glance signal for whether DO load is
under control.

**14. ⬜ Budgets and graceful degradation.** Define per-day soft caps; when exceeded,
degrade live features (counts go stale, chat read-only) rather than returning 5xx.
Deferred — only worth building once real traffic shows where the ceiling is.

---

## 4. Where this leaves you

**Shipped & live (verified):** the dominant cost drivers are gone — no per-visitor
WebSocket pinning a Durable Object 24/7 (R2), no per-visit fan-out of dozens of DO
requests (R3, R4-browse), and **WebSocket hibernation (R1)** so idle chat/host sockets
bill ~no duration. Live presence is read from D1, read-heavy endpoints are edge-cached.
Together these target every cost axis identified in §2.

Observability is in place too: the admin dashboard now shows live hosts/clients so you
can watch DO load directly (Phase 3 #13).

**What's left (all needs your input or real traffic — not blind code):**
1. **Exercise the host tunnel** (§3.5 remaining check) with a real desktop host —
   `git clone` + file browse — to confirm hibernation under tunnel load. *(Only you can
   do this; it needs a connected host.)*
2. **Phase 1 #8 (blob caching)** — deferred for correctness; needs the client to pass a
   commit SHA so blobs can be keyed immutably. Do it after the tunnel is verified, if
   browse traffic warrants.
3. **Phase 2 — federation + WebRTC P2P** — the durable answer to "free for the masses,"
   gated on your decisions: open vs. allowlisted federation, the end-to-end trust model,
   and STUN/TURN. This is a project, not a patch.
4. **Phase 3 #14 (budgets)** — wire once real traffic shows the ceiling.

> ⚠️ Before deploying any of this: the worker's deploy must target the correct
> Cloudflare account. `CLOUDFLARE_ACCOUNT_ID` is now pinned in the gitignored
> `.env.production` and exported by `deploy.sh` — necessary because the wrong account
> id is cached in local wrangler state.
