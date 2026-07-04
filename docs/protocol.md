# The ForkMesh mainnode protocol

ForkMesh is a peer-to-peer forge: desktop nodes hold the working copies, the git
history, and the issue/PR/discussion records, and they talk to each other through
a thin **mainnode** relay. The relay never holds a repository and never sees
plaintext room traffic — it coordinates presence, forwards the live clone/browse
tunnel, verifies signed submissions from people without write access, and keeps a
public catalog so the network is discoverable.

Today one mainnode runs at `forkmesh.com`. This document specifies the protocol
so that is a *deployment*, not the definition: anyone can stand up a mainnode from
this spec and point a stock desktop client at it (see
[Self-hosting a mainnode](#self-hosting-a-mainnode)). The reference relay is the
Cloudflare Worker in [`cloudflare_worker/`](../cloudflare_worker); the reference
client is the Qt app in [`qt_client/`](../qt_client).

Companion documents:

- [`issues/README.md`](../issues/README.md) — the on-disk issue/PR/discussion
  record format and the issue-event canonical string (summarized below).
- [`cloudflare_worker/README.md`](../cloudflare_worker/README.md) — relay
  operational notes.

Everything here is versioned by canonical-string prefix (`forkmesh-<thing>-v1`).
A `-v1` string is frozen: changing the bytes that go into a signature is a new
version, never an edit, so old signatures keep verifying across a client rollout.

---

## 1. Identity and signatures

### 1.1 Node identity

Every node has one long-lived **Ed25519** identity key, generated on first run and
stored as `ed25519.pem` in the node's config directory
([`ForkMeshIdentity`](../qt_client/src/ForkMeshIdentity.h)). This key is separate
from the LAN TLS certificate: TLS encrypts peer links, the Ed25519 key *signs*
metadata (profile, catalog records, issues, PRs, releases, auth tokens).

The **public key is the identity** — a raw 32-byte Ed25519 key, encoded
**base64url without padding**. That base64url string is what appears as `author` /
`maintainer` throughout the protocol and as the pubkey in every canonical string.
Signatures are likewise raw Ed25519, base64url without padding.

A human-facing **account name** (e.g. `newnewnode`) can be bound to a key by
reserving and finalizing it (§4.5). Account binding is what gates hosting, private
repos, and catalog publishing — a bare key can browse and submit, but only the key
registered to an account may claim to *host* `owner/repo`.

### 1.2 The canonical-string convention

ForkMesh never signs canonicalized JSON (that would invite C++/Python
serialization drift). It signs an **explicit newline-joined canonical string**:

```
forkmesh-<thing>-v1 \n
<field 1> \n
<field 2> \n
...
```

Where a signature commits to variable-length content, the content is reduced to a
single field by joining its parts with a NUL (`\x00`) and hashing:
`sha256hex(part1 \0 part2 \0 …)`. The signature then binds that hex digest, not
the raw content, so the canonical string stays fixed-shape.

Both sides implement this identically: the client in the various `*Store`
`canonicalString` / `contentForSigning` helpers, the relay in
[`cloudflare_worker/src/entry.py`](../cloudflare_worker/src/entry.py)
(`ed25519_verify` + the per-type `verify_*` functions).

### 1.3 Signature schemes

Every signed action in the protocol. The relay verifies each with the pubkey named
in the canonical string (`ed25519_verify`); a bad or missing signature is rejected
before any state changes.

| Purpose | Canonical string (fields after the prefix line, `\n`-joined) |
|---|---|
| **Issue event** | `issue-event` type · number · author · ts · `sha256hex(content)` |
| **Discussion event** | `discussion-event` type · number · author · ts · `sha256hex(content)` |
| **Pull request** | `pull-event` author · ts · `sha256hex(title \0 base \0 head \0 patch [\0 commits])` |
| **Pull comment** | `pull-comment` type · number · author · ts · `sha256hex(content)` |
| **Commit comment** | `commit-comment` sha · author · ts · `sha256hex(body)` |
| **Catalog record** | `catalog` owner · repo · updatedAt |
| **Catalog delete** | `catalog-delete` owner · name · ts |
| **Repo-state attestation** | `repostate` owner · repo · stateHash · updatedAt |
| **Release manifest** | `release` repo · tag · author · ts · `sha256hex(content)` |
| **Account reserve** | `reserve` name · ts |
| **Account finalize** | `finalize` name · email · ts |
| **Host token** | `host` owner · repo · ts |
| **View token** (private clone/browse) | `view` owner · repo · ts |
| **Push token** (`git push`) | `push` owner · repo · ts |
| **Share view token** (collaborator) | `share-view` viewer · owner · repo · ts |
| **Node link (self)** | `link-self` nodeName · identifier · ts |
| **Node link (grant)** | `link-grant` nodeName · ts |

All prefixes are the literal `forkmesh-<name>-v1`. `ts` is milliseconds since the
epoch; time-bound tokens (`host`/`view`/`push`) are only accepted inside a short
freshness window (`_ts_ok`), which is the sole replay defense — there is no
server-side nonce state.

The distinct prefixes are load-bearing: a `view` token can never be replayed as a
`host` or `push` token, and vice versa, even though the three share the same
`owner · repo · ts` shape.

### 1.4 Issue / PR / discussion record signing

The on-disk record format lives in [`issues/README.md`](../issues/README.md). The
essentials the relay verifies for cross-user submissions:

```
canonical =
  forkmesh-issue-event-v1 \n <type> \n <number> \n <author-pubkey> \n <ts> \n
  sha256hex(content)
```

`content` is type-specific and joined by NUL. For an `open` event it is
`title \0 body \0 attachments.join(",")`; the body has leading/trailing newlines
stripped so the blank line after frontmatter doesn't change the signature. See the
per-type content table in `issues/README.md`. Issue signatures bind the issue
number; PR signatures deliberately do **not** (the owner assigns the durable PR
number on merge).

---

## 2. Rooms (encrypted coordination channels)

Nodes converge on shared **rooms** hosted by the mainnode's Durable Objects. Rooms
carry presence, chat, and the small control frames that make the mesh feel live.
The default room every node joins is `mainnode/forkmesh` → room `general`, i.e.
the WebSocket path:

```
/api/repo/<owner>/<repo>/rooms/<room>/ws
```

The relay is a **blind** relay: room traffic is end-to-end encrypted and it only
ever sees ciphertext.

### 2.1 Room key

The room key is derived client-side, identically in the desktop client
([`RoomCrypto.cpp`](../qt_client/src/RoomCrypto.cpp)), the website
([`chat.js`](../cloudflare_worker/public/chat.js)), and the Flutter app:

- **Salt:** `SHA-256("ForkMesh room:" + <roomName>)`.
- **KDF:** PBKDF2 over a passphrase → a 256-bit **AES-GCM** key.
- The passphrase-free shared rooms use a baked-in app key
  (`"forkmesh-shared-room-key-v1"`), which is what binds every client to the same
  AES key. It must stay byte-for-byte identical across all three implementations —
  a mismatch silently drops every message.

### 2.2 Message envelope

Each frame on the wire is JSON:

```json
{ "kind": "cipher", "v": 1, "nonce": "<b64 12 bytes>", "tag": "<b64 16 bytes>", "body": "<b64 ciphertext>" }
```

The plaintext (before encryption) is the application JSON object — a chat message,
an edit, a delete, a presence beacon, etc. AES-GCM uses the 12-byte nonce; the
16-byte tag is stored separately from the ciphertext body (the desktop client
splits them the same way).

### 2.3 History retention

The relay keeps the last few days of **encrypted** frames so a late-joining node
sees some backlog when no peer is online to replay it. Retention is opt-in
per frame: only an envelope with `persist: true` is retained (the desktop client
sets it for its durable message types). Limits:
`CHAT_HISTORY_RETAIN_MS` = 7 days, `CHAT_HISTORY_MAX_PER_ROOM` = 500 frames, and
frames over `CHAT_HISTORY_MAX_BODY` (48 KiB) are relayed but not retained. The
retained bytes are the opaque ciphertext — the relay still can't read them.

---

## 3. Catalog and state attestation

The **catalog** is the relay's public, at-rest-encrypted index of repositories, so
the network page and clients can discover what is hosted and where. A node
publishes a signed record for each repo it hosts.

### 3.1 Catalog record

`POST /api/repo/<owner>/<repo>/mirrors` (publish) carries a record plus two
signatures. Key fields (`safe_catalog_record`):

| Field | Meaning |
|---|---|
| `owner`, `name` | logical repo identity within this mainnode |
| `visibility` | `public` \| `private` (anything but `private` is public) |
| `rootCommit` | first/root commit — the **network-wide group key** that unites mirrors under different owners into one logical repo |
| `source` | `local-node` (a working-copy holder / source of truth) or `remote-clone` (a mirror) |
| `sizeBytes`, `description`, `cloneUrl`, `solana`, `hostedSince`, `lastSync` | descriptive |
| `commit`, `branch`, counts, `platform`, `version`, `nodeId`, `clonesServed`, `websiteServed` | point-in-time node facts, shown even while the node is offline |
| `maintainer` | the publishing key (base64url pubkey) |
| `stateHash`, `stateSig` | the repo-state attestation (§3.2) |

The record is accepted only if:

1. `catalogSig` verifies for `forkmesh-catalog-v1 \n owner \n name \n updatedAt`
   against the account's registered key (`maintainer` must match).
2. If present, `stateSig` verifies the attestation (§3.2) — a *present but bad*
   attestation rejects the whole write; absent is allowed for backward
   compatibility.
3. `updatedAt` is not older than the stored record (rollback rejection).

The relay verifies **only** `catalogSig` and `stateSig`. Other fields (including
the descriptive JSON `signature`) are stored as-is; extend the record freely
without touching the verification path.

### 3.2 Repo-state attestation (the clone integrity gate)

To stop a tampered or rolled-back mirror from serving forged history, the owner
signs a fingerprint of the refs it serves:

- `stateHash` = SHA-256 over the **canonical heads+tags advertisement**
  (`advertised_refs_canonical`).
- `stateSig` signs `forkmesh-repostate-v1 \n owner \n repo \n stateHash \n updatedAt`.

The relay pins verified attestations from **source-of-truth** (`local-node`)
records into `repo_state_history` (newest N per repo). When a clone is served, the
serving node's live ref advertisement must hash to a pinned value
(`clone_state_pins`):

- A **source of truth** is validated against its *own* attestations (current pin +
  recent history) — self-attestation is fine, the pin is a consistency check and
  the history absorbs the publish-to-serve lag.
- A **mirror** (`remote-clone`) must serve a state some source in its logical-repo
  group (matched by `rootCommit`, name fallback) actually attested — a mirror's
  self-signed pin proves nothing, since a tampered mirror can always republish a
  hash for its forged refs.
- If *no* source in the group has ever attested (legacy), the mirror's own pin
  applies (fail-open), preserving old behavior.

A mirror running a local agent whose branches diverge from the source fails the
gate until its next pruning sync (~5 min) — clones of that mirror fail closed for
that window, by design.

### 3.3 Presence

`host_presence` is a one-row-per-repo table (`repo_bi` = blind index of
`owner/name`, upserted on each heartbeat) marking a repo's tunnel as live.
`touch_host_presence` refreshes it when a host connects and, throttled, while it
serves traffic. Rows go stale after `HOST_PRESENCE_STALE_MS` and are swept.

Presence is a *lagging* signal (it can't detect an unclean tunnel death until the
row expires), so the clone/browse path treats **live tunnel liveness** as ground
truth (a subrequest to the host DO's connected-host count), not the presence row —
see §5. The network page's online-node count is derived from presence
(distinct online nodes), not a raw row count.

---

## 4. HTTP API

All routes are rooted at the mainnode host. The route table is the single source
of truth in [`cloudflare_worker/src/urls.py`](../cloudflare_worker/src/urls.py);
`Default._route` in `entry.py` dispatches against it.

### 4.1 Rooms and live tunnel

| Method · path | Purpose |
|---|---|
| `GET /api/room/<room>/ws` \| `/clients` | bare room WebSocket / live client count |
| `GET /api/repo/<owner>/<repo>/rooms/<room>/ws` \| `/clients` | per-repo room WS / count (the mainnode `general` room lives here) |
| `* /api/repo/<owner>/<repo>/host` | desktop host connects the live tunnel (WebSocket) |
| `GET /api/repo/<owner>/<repo>/{tree,blob,blobs,raw,history,commit,branches,search}` | website browse — forwarded to the best-connected host |

The `/host` end is a desktop node offering to serve a repo; the browse endpoints
are pulled by the website and forwarded over that tunnel (`_forward_to_node`),
never redirected.

### 4.2 Signed inboxes (cross-user submissions)

People without write access submit signed records; the owner's node drains,
verifies, merges into git, and syncs back. Each is `POST` and each verifies with
the matching §1.3 scheme.

| Path | Submission |
|---|---|
| `POST /api/repo/<owner>/<repo>/issues` | signed issue open/comment/edit/status/… |
| `POST /api/repo/<owner>/<repo>/pulls` | signed pull request |
| `POST /api/repo/<owner>/<repo>/commits` | signed per-commit comment |
| `POST /api/repo/<owner>/<repo>/discussions` | signed discussion open/comment |
| `POST /api/repo/<owner>/<repo>/subscribe` | signed thread subscribe/unsubscribe |
| `POST /api/repo/<owner>/<repo>/bounty` | issue-bounty escrow (Solana deposit / confirm / split) |
| `* /api/repo/<owner>/<repo>/shares` | owner-signed private-repo collaborator ACL |

### 4.3 Catalog, mirrors, agents, releases

| Path | Purpose |
|---|---|
| `POST /api/repo/<owner>/<repo>/mirrors` | publish a signed catalog record (§3.1); `GET` reports mirror health |
| `* /api/repo/<owner>/<repo>/agents` · `/agents/list` · `/agents/<id>/prompt` | signed agent-session push/drain, password-gated read, queued prompt |
| `GET /api/repo/<owner>/<repo>/releases/blob/sha256/<hash>` | content-addressed release-asset download over the host tunnel (immutable, edge-cached) |
| `GET /api/repo/<owner>/<repo>/releases/downloads` | per-artifact download counts |

### 4.4 Git smart-HTTP (clone / fetch / push)

| Path | Purpose |
|---|---|
| `GET /<owner>/<repo>/info/refs` | clone/fetch ref advertisement (request 1 of 2) |
| `POST /<owner>/<repo>/git-upload-pack` | clone/fetch pack negotiation (request 2 of 2) |
| `POST /<owner>/<repo>/git-receive-pack` | `git push` (issue #358), gated by a `push` token in HTTP Basic |

`git clone https://<mainnode>/<owner>/<repo>` works with a stock git. The relay
serves `git-upload-pack` only for public repos openly; private repos challenge
with `401 Basic` and require a `view` token. Push challenges likewise and requires
a `push` token; a push never falls back to a mirror (§5). See §5 for the two-request
flow.

### 4.5 Accounts and platform

| Path | Purpose |
|---|---|
| `GET/POST /api/accounts/<name>` | reserve / finalize / login; `GET` looks up an account's public key |
| `GET /api/version` | `{ ok, rev, now }` — the live BUILD_REV, used to verify a deploy (§6) |
| `GET /api/network/stats` · `/online-history` · `/leaderboards` | cached homepage / network-page aggregates |
| `GET /api/status` | 30-day per-system uptime for the public status page |

Account registration is two steps: **reserve** a name (`forkmesh-reserve-v1`) then
**finalize** it with the key + email (`forkmesh-finalize-v1`). Once finalized, the
account name is bound to that Ed25519 key, and `verify_host_token` /
`verify_view_token` / `verify_push_token` all resolve the owner name → registered
pubkey before accepting a hosting/browse/push action. There is no self-assertion:
with no registered account, those gates fail closed.

---

## 5. The two-request clone flow and sticky pinning

A `git clone` is **two HTTP requests** that must reach the **same** serving node:

1. `GET /<owner>/<repo>/info/refs` — the ref advertisement.
2. `POST /<owner>/<repo>/git-upload-pack` — pack negotiation against the refs the
   first request advertised.

If those two land on *different* nodes, the pack is negotiated against a different
ref set and the clone breaks. The relay guarantees they don't, while still failing
a clone over to a healthy mirror when the named source is down. The logic lives in
`Default._git_host` (`entry.py`); the key idea is that the client's URL never
changes — there is **no redirect**. The relay rewrites the request path into the
serving node's namespace and forwards it (`_forward_to_node`).

Decision order for a **public** repo on `info/refs` / `upload-pack`:

1. **Auto-heal to the online source.** If a source of truth for this logical repo
   is online (`_online_source_of_truth`), hand the request to it — even a request
   addressed to a mirror. A mirror whose refs are stale/diverged would otherwise
   reject the clone; the canonical source serves it and the mirror clears once it
   re-syncs. Skipped when the source is offline (so the tamper gate still fully
   protects clones then). Both clone requests take this branch while the source
   stays online, so they reach the same node.

2. **Fail over to a sticky mirror.** If the named source has *no live host*
   (liveness is ground truth — the host DO's connected-host count, not the lagging
   presence row), pick a healthy mirror and **pin** it per repo in `clone_sticky`
   for `CLONE_STICKY_MS` (5 min). `refresh=True` on `info/refs` lets a new clone
   re-pick; the paired `upload-pack` POST reads the same pin (`_fresh_clone_pin`)
   so it follows the mirror the advertisement came from.

3. **Source live but info/refs stalled.** If the source's tunnel is live but its
   `info/refs` times out (504/503), retry the advertisement **once** from a live
   mirror and pin it — only `info/refs` (a bodyless idempotent GET) is safe to
   replay; the POST relies on the pin.

The mirror chosen at every step still passes the §3.2 integrity gate:
`_forward_to_node` reaches the mirror's host DO, which serves only refs whose hash
matches a source-attested pin. So serving in place never weakens the tamper check.

Private repos are excluded from all mirror fallback: they only ever clone from the
owner's own live host, gated by a `view` token. Push (`git-receive-pack`) likewise
never falls back — a push must reach the working-copy holder that can run
receive-pack and re-attest the integrity pin.

---

## 6. Self-hosting a mainnode

A mainnode is the Cloudflare Worker in
[`cloudflare_worker/`](../cloudflare_worker). Standing up your own:

### 6.1 Deploy the relay

1. Provision the Worker's bindings — D1 (the encrypted at-rest store), the
   `FORKMESH_HOST` Durable Object namespace (the live tunnel), and the room DO.
   `wrangler.toml` and `migrations/` define the schema; `migrate.sh` applies it.
2. Set the secrets the Worker needs (`DATA_KEY` / `HMAC` material for at-rest
   encryption, and any admin key). The catalog is encrypted at rest, so `DATA_KEY`
   must be stable across deploys.
3. Deploy with [`cloudflare_worker/deploy.sh`](../cloudflare_worker/deploy.sh). It
   stamps a `BUILD_REV` var into the Worker and then **verifies the live origin is
   actually serving it** by polling `GET /api/version` until the reported `rev`
   matches (allowing for edge propagation). A deploy that never reports the new rev
   fails loudly instead of silently no-op'ing — this same `/api/version` check is
   your health probe for a self-hosted mainnode.

Your relay now answers the routes in §4 at your own host.

### 6.2 Point a client at it

The desktop client's mainnode **host** is fully configurable and threaded through
everything downstream — you do not edit code:

- On the first-run screen, the **Relay server** field takes a bare host (e.g.
  `relay.example.com`, or `localhost:8787` for a local dev relay). The client
  expands it to the full room URL with `canonicalServerUrl`
  ([`MainWindowInternal.h`](../qt_client/src/MainWindowInternal.h)) and persists it
  under `server/url`. `localhost` / `127.0.0.1` get `ws://`; everything else gets
  `wss://`.
- The mainnode path shape (`/api/repo/mainnode/forkmesh/rooms/general/ws`) is a
  network-wide protocol constant (`kMainnodeRoomPath`), the same on every mainnode,
  so only the host varies. An advanced user can paste a full `wss://…` URL to
  override the whole path if their deployment differs.
- Every consumer (room connect, catalog publish, browse, clone) reads the stored
  `server/url`, so setting the host once repoints the entire client.

### 6.3 Point the website at it

The relay serves its own static site (`cloudflare_worker/public/`). Its room
WebSocket path is same-origin (`location.host`), so a self-hosted mainnode's site
talks to itself with zero configuration. If you serve the static site separately
from the relay, set `window.FORKMESH_RELAY_HOST` (e.g. `"relay.example.com"`)
before the chat scripts load and they will target that relay
([`chat.js`](../cloudflare_worker/public/chat.js),
[`dashboard-chat.js`](../cloudflare_worker/public/dashboard-chat.js)).

### 6.4 Acceptance

A second party who has done the above has a working mainnode: a stock desktop
client, given only the new host, registers an account, publishes a catalog record,
hosts a repo, and serves clones through the two-request tunnel — all against the
new relay, with no forkmesh.com dependency.
