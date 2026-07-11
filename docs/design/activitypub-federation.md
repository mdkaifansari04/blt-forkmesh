# ActivityPub federation (fediverse interop)

Status: implemented in the relay Worker (2026-07). Desktop/host wiring for
release + merge announcements is a follow-up (the relay endpoint is live).

## What federates

ForkMesh joins the ActivityPub network (Mastodon-compatible) with two kinds of
followable actors on the relay's domain:

| Actor | Handle | Actor URL | Type |
|---|---|---|---|
| User profile | `@alice@forkmesh.com` | `/ap/users/alice` | `Person` |
| Repository | `@owner.repo@forkmesh.com` | `/ap/repos/owner/repo` | `Group` |
| Service actor | `@forkmesh.com@forkmesh.com` | `/ap/actor` | `Application` |

User names can never contain a dot, so `owner.repo` splits unambiguously on
the first dot (repo names may themselves contain dots). Handles and actor ids
are lowercase-normalized. Only active, public user accounts and published,
non-private repos federate; the visibility checks mirror the public-profile
and catalog rules.

Outbound: verified signed-inbox submissions federate as `Create(Note)` —
issue opens/comments, PR opens/comments/reviews, discussion opens/comments,
commit comments. The repo actor posts to its followers; when the event's
author maps to a local account, the user actor posts to theirs as well.
Releases and merges never pass through the relay inboxes (they are canonical
on the owner's node), so the owner-signed `POST
/api/repo/{owner}/{repo}/ap-publish` endpoint lets the desktop push those
announcements; until the desktop adopts it they simply don't federate.

Inbound: `Follow` (auto-accepted), `Undo(Follow)`, `Create(Note)` replies to
our objects, `Delete`. Remote replies become **federated comments** — stored
in `ap_comments`, surfaced via `GET
/api/repo/{owner}/{repo}/fedi-comments?kind=issue&number=N`, and shown as
clearly-marked fediverse comments. They are deliberately kept OUTSIDE the
Ed25519-signed event log: a remote reply can never carry a forkmesh author
signature, so it must never enter the signed spine that nodes replicate.

## Wire protocol

Follows Mastodon/Wildebeest practice:

* RSA-2048 keypair per actor, public key published as `publicKeyPem`
  (`<actor>#main-key`), private key stored AES-GCM-encrypted under `DATA_KEY`
  (the standard `encrypt_row` pattern) in `ap_actors`.
* draft-cavage HTTP signatures. Outbound signs
  `(request-target) host date digest content-type` (GETs:
  `(request-target) host date accept`), `algorithm="rsa-sha256"`. Inbound
  verifies whatever header list the peer signed but REQUIRES
  `(request-target)` and `digest`, checks `Digest: SHA-256=<b64>`
  byte-for-byte, enforces a ±12 h date window, and requires the `keyId` host
  to match the activity actor's host. Key fetches are signed with the
  service actor (secure-mode servers answer) and cached in
  `ap_remote_actors` for 24 h with one forced refresh on verify failure
  (key rotation).
* Discovery: `/.well-known/webfinger`, `/.well-known/nodeinfo` →
  `/nodeinfo/2.1`, and content negotiation on `/@name` (an ActivityPub
  `Accept` header gets the actor document instead of HTML).

The pure protocol spine (documents, signing strings, digest, sanitization)
lives in `cloudflare_worker/src/activitypub.py` — stdlib-only, imported
directly by `tests/test_activitypub.py`. WebCrypto RSA glue, D1 state and
handlers live in `entry.py` (section "ActivityPub federation").

## Delivery without Queues (free plan)

There is no Cloudflare Queues binding. A publish writes one `ap_outbox` row
per unique destination inbox (sharedInbox preferred), best-effort drains 5
inline, and the per-minute cron's `minute % 5 == 0` slot drains 20 more with
exponential backoff (5 m → 24 h cap, 8 attempts, 404/410 drop immediately).
Cost while unused is near zero: actor rows are minted lazily on the first
WebFinger lookup, so an unfollowed repo's publish path is two indexed
SELECT misses.

## Configuration

Operator config is admin-managed (signed admin API, same gate as the relay
allowlist — the caller signs with their node key and must carry `is_admin`):

* `GET /api/accounts/admin-ap?node=&ts=&sig=` — signature over
  `forkmesh-admin-ap-v1\n<node>\n<ts>`. Returns `{enabled, blockedDomains,
  stats}` (actors, followers, remote actors, objects, comments, queued
  deliveries).
* `POST /api/accounts/admin-ap-update` — body `{node, ts, sig, action,
  domain?}`, signature over
  `forkmesh-admin-ap-update-v1\n<node>\n<action>\n<domain>\n<ts>`. Actions:
  `enable` / `disable` (global switch — while disabled every AP endpoint 404s,
  publish hooks no-op and the cron drain pauses) and `block` / `unblock`
  (defederation — blocking a domain rejects its inbound activities, refuses
  actor fetches, and purges its followers, cached actors and queued
  deliveries, subdomains included).

Settings live in `ap_settings` / `ap_blocked_domains` with a 60 s per-isolate
cache. Deployment-level config stays in wrangler vars: `PUBLIC_BASE_URL`
(actor-id origin for self-hosted relays; defaults to the request host).

The Solana payout relay mesh is a separate system; its canonical routes moved
to `/api/relay-mesh/*` with `/api/federation/*` kept as a served legacy alias
(federated relays also retry the legacy path against older main relays), so
the two federations no longer share a name.

## Known limitations / follow-ups

* Desktop node should call `ap-publish` on release publish + PR merge, and
  render federated comments in issue/PR/discussion views (API is live).
* Local users cannot yet follow remote fediverse accounts (no outbound
  Follow); Like/Announce/Update inbound are acknowledged and dropped.
* Followers collections expose `totalItems` only (no paging) — Mastodon is
  fine with this.
* Object content is public by design (public repos only), stored encrypted
  at rest like everything else.
