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

> **Implementation status (updated):** Phase 0's cost-dominant items (R2, R3, R4
> fan-out, R5) and the host side of Phase 1 #7 are **implemented and build-verified**.
> WebSocket Hibernation (R1) is **specified but not yet applied** — see §3.5 for why
> it needs a live test first. Phase 2 is a **design**, not yet built.

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

8. ⬜ **Coalesce git browsing.** Batch `/tree`+`/blob` round-trips, and cache immutable
   blobs (addressed by commit SHA) at the edge so repeat views don't re-hit the host
   DO.

9. ⬜ **Add per-DO idle timeouts.** *Caveat discovered:* a blanket idle-close is wrong
   for host sockets (a connected-but-idle host is the "repo is available" guarantee)
   and for chat (drops participants). Scope this to truly-abandoned connections only,
   or fold it into hibernation (§3.5).

### 3.5 — WebSocket Hibernation (R1) ⚠️ SPECIFIED, NOT APPLIED — needs a live test

This is the plan's biggest *duration* win, but it is **not a drop-in refactor** and I
could not safely apply it blind. Two blockers, both requiring a running worker to
resolve:

- **Unverifiable dispatch.** The `workers-py` SDK contains no hibernation glue
  (`grep` for `acceptWebSocket`/`webSocketMessage` in the package is empty) — dispatch
  happens in the workerd runtime. I could not confirm from the repo whether Python DOs
  receive hibernation events, **nor the method-name convention** (`webSocketMessage`
  camelCase vs `web_socket_message` snake_case). Guessing wrong = silently broken chat
  and tunnel in production.
- **State must be reconstructed, not held in memory.** Hibernation evicts the DO
  between events, so `self.sockets` / `self.observers` / `self.hosts` (with `rtt`)
  cannot live in instance attributes — they must be rebuilt from
  `self.ctx.getWebSockets(...)` and per-socket `serializeAttachment(...)`.

**Implementation guide (do this in a `./deploy.sh dev` session, testing after each
step):**

1. Confirm dispatch + naming: add a no-op DO that `acceptWebSocket`s a socket and logs
   from a `webSocketMessage`/`web_socket_message` method; see which fires in dev.
2. `ForkMeshRoom`: accept with `self.ctx.acceptWebSocket(server, ["chat"])` or
   `["observer"]`. Move `on_message`/`forget` logic into the handler methods. Compute
   the client count as `len(self.ctx.getWebSockets("chat"))`. Push counts to observers
   on connect/close. Consider `setWebSocketAutoResponse` for a ping/pong keepalive.
3. `ForkMeshHost`: tag host sockets `["host"]`; store `{id, rtt}` via
   `serializeAttachment`. Rebuild the host registry from `getWebSockets("host")` in
   `_best_host`. The `pending`/`git_buffers` in-memory maps are fine — they only live
   during an in-flight request, which keeps the DO active (no hibernation mid-request).
4. Smoke test before deploy: chat send/receive across two tabs, repo tree/blob browse,
   and a full `git clone`. Only deploy once all three pass.

**Revert plan:** the change is confined to the two DO classes; if dev shows broken
dispatch, revert those classes and keep the Phase 0 wins (which already remove the
dominant always-on cost).

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

**13. Surface DO metrics on the admin dashboard.** You already have an admin error-log
dashboard ([entry.py](src/entry.py) `render_admin_html`); add request/duration
counters (and the new `host_presence` count) so you can *see* what drives cost.

**14. Budgets and graceful degradation.** Define per-day soft caps; when exceeded,
degrade live features (counts go stale, chat read-only) rather than returning 5xx.

---

## 4. Where this leaves you

**Shipped now (verified):** the dominant cost drivers are gone — no per-visitor
WebSocket pinning a Durable Object 24/7 (R2), and no per-visit fan-out of dozens of DO
requests (R3, R4-browse). Live presence is read from D1, and read-heavy endpoints are
edge-cached. These required no architectural change and should, on their own, bring DO
usage back toward the free tier.

**Next, in priority order:**
1. **Hibernation (§3.5)** — the biggest remaining *duration* win, for long-lived chat
   and host sockets. Needs a `./deploy.sh dev` test session first (method-name +
   dispatch unknowns); revert-safe if Python doesn't support it.
2. **Phase 1 #8/#9** — blob caching and scoped idle handling.
3. **Phase 2** — federation + WebRTC P2P: the durable answer to "free for the masses,"
   gated on the trust-model and TURN decisions called out above.
4. **Phase 3** — metrics + budgets so cost stays visible and degrades gracefully.

> ⚠️ Before deploying any of this: the worker's deploy must target the correct
> Cloudflare account. `CLOUDFLARE_ACCOUNT_ID` is now pinned in the gitignored
> `.env.production` and exported by `deploy.sh` — necessary because the wrong account
> id is cached in local wrangler state.
