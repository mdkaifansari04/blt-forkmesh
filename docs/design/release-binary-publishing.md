# Release-Binary Publishing System — Design (issue #304)

Status: design proposal. Supersedes the committed-binary scheme described in
`releases/README.md` (see [§11 Migration](#11-migration-from-committed-binaries)).

## 0. Why this design, and how it fits ForkMesh

ForkMesh is not a single-server Git host with one SQL database and one S3 bucket.
It is a mesh:

- **Metadata is signed, git-committed, mesh-synced.** Issues, pull requests,
  commit comments, and discussions are files committed into the repo, signed with
  the author's Ed25519 identity, and replicated to every mirror node. There is no
  central row store — the repo *is* the database, and signatures are the integrity
  guarantee.
- **Binary delivery is host-forwarding, not a blob store.** The Cloudflare worker
  (`cloudflare_worker/src/entry.py`) routes `/blob`, `/tree`, clone, and
  `git-upload-pack` to the best-connected node that has the data. The relay keeps
  **no** durable blob store of its own, and the inline `/blob` path caps at 4 MB.
- **The current release scheme commits binaries into the repo** under
  `releases/<channel>/…` so the existing git mirror carries them. That violates
  this issue's first requirement ("release binaries must not be committed into Git
  repositories") and bloats every clone of the repo forever.

So the design splits cleanly along the grain of the system:

| Concern | Where it lives | Integrity | Replication |
|---|---|---|---|
| Release **metadata** (release, assets, checksums, signatures, audit) | Signed files committed under `releases/` in the repo | Ed25519 signature + git history | Existing mesh git sync |
| Release **payloads** (the actual artifact bytes) | Content-addressed blob store (CAS) on nodes, **outside git** | SHA-256 content address (self-verifying) | Lazy pull between nodes, on demand |

The metadata is small, immutable, and benefits from being in git (auditable,
signed, free replication). The payloads are large and must not be in git, so they
go in a CAS that is addressed *by the same hash the metadata commits to*. The
metadata is the single source of truth; a payload is only ever trusted because its
bytes hash to the value a signed manifest already recorded.

This reconciles the two seemingly opposed requirements — "make binaries available
the same way the repo is available" (i.e. served by the mesh through the worker)
and "do not commit binaries to git" — by serving them through the *same worker
host-forwarding mechanism* the repo already uses, just from a CAS instead of from
git objects.

## 1. Architecture overview

```
  maintainer                 CI runner (build node)            relay worker            mirror nodes / clients
      │                              │                              │                          │
      │  push signed tag v1.2.3      │                              │                          │
      ├─────────────────────────────┼──────────────────────────────────────────────────────► │ (tag syncs in mesh)
      │                              │                              │                          │
      │                       tag event triggers .forkmesh/ci      │                          │
      │                              │  build per-platform artifacts│                          │
      │                              │  sha256 each artifact        │                          │
      │                              │                              │                          │
      │           POST create release (draft, scoped token)        │                          │
      │                              ├─────────────────────────────► validate token+tag        │
      │                              │  PUT/POST upload assets       │  forward to home node    │
      │                              ├─────────────────────────────►│─────────► CAS write ───► │
      │                              │  (chunked, resumable, by hash)│                          │
      │                              │  POST finalize (sign manifest)│                          │
      │                              ├─────────────────────────────►│ commit signed metadata ─►│ (mesh sync)
      │                              │                              │                          │
  user │  GET /<owner>/<repo>/releases/v1.2.3/<asset>              │                          │
      ├──────────────────────────────────────────────────────────►│ look up asset→hash       │
      │                              │                              │ forward to a node w/ blob│
      │ ◄────────────────────────────────────────────────────────┤◄──────── stream bytes ────┤
      │                              │                              │ verify sha256 on the fly │
```

Components introduced:

1. **Release metadata store** — signed YAML/JSON files committed under `releases/`
   (detailed in §3/§4). Travels on the existing mesh git sync.
2. **Content-addressed blob store (CAS)** — a per-node on-disk directory holding
   artifact bytes keyed by `sha256`, *gitignored* so it never enters a commit.
3. **Release API + download router** — new worker routes that read the signed
   metadata, enforce permissions, and forward asset downloads to a node holding
   the blob (reusing the existing `/blob`-style host-forwarding, but streaming and
   uncapped for releases).
4. **CI publisher** — a small client (shipped as a sub-command of the desktop/CLI
   client and usable from `.forkmesh/*.yml` jobs) that creates the release,
   uploads assets resumably, and finalizes by signing the manifest.

## 2. Recommended user workflow

1. **Create a tag.** Maintainer creates an annotated, signed tag `v1.2.3` on the
   release commit and publishes it to the mesh. Tags are the *only* trigger; you
   cannot publish a release for a ref that is not a tag (§5).
2. **CI builds artifacts.** A tag event matches a release job in `.forkmesh/`
   (e.g. `release.yml`). Each platform node builds its artifact
   (`forkmesh-linux-x86_64`, `…-macos-arm64`, `…-windows-x86_64.exe`, plus any
   `tar.gz`/`zip`/`.dmg`), and computes `sha256` for each.
3. **CI creates or updates the release.** `POST …/releases` with the tag name and
   draft metadata (title, notes, prerelease flag). Idempotent on `(repo, tag)`:
   re-running returns the existing **draft** release. Publishing a finalized
   release a second time is rejected (§5 immutability).
4. **CI uploads assets.** For each artifact: initiate an upload, send chunks
   (resumable), and complete. The server keys the upload by the artifact's
   declared `sha256`; if a blob with that hash already exists anywhere it dedupes
   and the upload is a no-op (§4). Concurrent platform jobs upload in parallel to
   the same draft release (§7 concurrency).
5. **CI finalizes / publishes.** Once all expected assets are present, `POST
   …/releases/v1.2.3/finalize`. The server snapshots the asset list + checksums
   into a **manifest**, the publisher signs it with the repo/CI Ed25519 key, and
   the signed manifest is committed under `releases/` and synced across the mesh.
   The release flips from `draft` → `published` and becomes immutable.
6. **Users download.** Stable URL `…/<owner>/<repo>/releases/v1.2.3/<asset>` (and
   a `…/releases/latest/<asset>` alias). The installer/`install.sh` resolves the
   asset for the visitor's platform from the release manifest and downloads it; it
   verifies the `sha256` (and signature, if present) before use.

## 3. Data model

The logical model is five entities. On ForkMesh they are realized as signed files
(see §4); on a conventional single-server Git host the same model maps directly to
SQL tables. Field names are shared between both realizations.

### releases
| field | type | notes |
|---|---|---|
| `id` | string | `<repo_id>:<tag>` — stable, derived from the immutable tag |
| `repo` | string | `<owner>/<repo>` |
| `tag` | string | e.g. `v1.2.3`; must resolve to an existing tag ref |
| `tag_commit` | sha | commit the tag pointed to **at finalize time** (binds the release to a specific commit; see force-push handling §7) |
| `name` | string | display title (editable while draft; frozen at publish) |
| `body` | markdown | release notes (editable while draft; frozen at publish) |
| `prerelease` | bool | excludes from `latest` resolution |
| `draft` | bool | `true` until finalize |
| `state` | enum | `draft` \| `published` \| `yanked` |
| `created_by` | pubkey | Ed25519 identity that created it |
| `created_at` / `published_at` | ts | |
| `manifest_sha` | sha | hash of the finalized, signed manifest (the immutability anchor) |

### release_assets
One row per downloadable file attached to a release.
| field | type | notes |
|---|---|---|
| `id` | string | `<release_id>/<name>` |
| `release_id` | string | FK → releases |
| `name` | string | filename as downloaded, e.g. `forkmesh-linux-x86_64` |
| `blob_sha256` | sha | **the content address** — links to the artifact file/blob |
| `size` | int | bytes |
| `content_type` | string | best-effort MIME |
| `os` / `arch` | string | optional, for installer platform resolution |
| `label` | string | optional human label |
| `download_count` | int | mutable counter, kept *out* of the signed manifest (§6) |
| `state` | enum | `uploading` \| `complete` |
| `uploaded_at` | ts | |

`(release_id, name)` is unique — see §7 for same-name re-upload behavior.

### artifact files (CAS objects / blobs)
The actual bytes, deduplicated across all assets/releases/repos by hash. This is
*not* the same as `release_assets`: many assets (across releases or repos) may
point at one blob.
| field | type | notes |
|---|---|---|
| `sha256` | sha | primary key = content address |
| `size` | int | |
| `refcount` | int | number of `release_assets` referencing it (for GC, §4) |
| `locations` | list | node IDs known to hold the bytes |
| `created_at` | ts | first seen |

### checksums
Checksums are not a separate store — `blob_sha256` on each asset *is* the
canonical SHA-256. For convenience the server also publishes a generated
`SHASUMS256.txt` "asset" (and optionally `SHASUMS512.txt`) listing every real
asset, so existing `sha256sum -c` tooling works. These digest files are themselves
covered by the manifest signature (§5), so the checksum list cannot be tampered
with independently of the release.

### signatures
| field | type | notes |
|---|---|---|
| `target` | string | what is signed: the **manifest** (always), and optionally each asset |
| `algo` | enum | `ed25519` (native), plus optional `minisign` / `sigstore` detached sigs |
| `signer` | pubkey/identity | repo CI key or maintainer key |
| `sig` | base64url | detached signature bytes |

The mandatory signature is over the **canonical manifest** (release + ordered
asset list + each `blob_sha256` + `tag_commit`). One signature therefore transitively
authenticates every asset's content. Per-asset detached signatures (`<asset>.minisig`,
`<asset>.sig`) are optional uploaded assets for ecosystems that expect them.

### audit events
Append-only, signed, never mutated.
| field | type | notes |
|---|---|---|
| `id` | string | monotonic / ulid |
| `repo` / `release_id` | string | |
| `action` | enum | `release.create` \| `asset.upload` \| `asset.replace` \| `release.finalize` \| `release.edit_metadata` \| `release.yank` \| `tag.deleted_after_release` \| `tag.force_moved` |
| `actor` | pubkey | |
| `detail` | json | e.g. old/new `blob_sha256`, old/new `tag_commit` |
| `at` | ts | |
| `sig` | base64url | signed like other mesh events |

Because metadata is git-committed, the git history is itself an audit log; the
explicit `audit events` stream additionally captures actions that don't change a
committed file (e.g. a download spike, a rejected publish) and security-relevant
tag mutations detected after the fact.

## 4. Storage strategy & layout

### On-disk layout (per node)
```
<repo>/
  releases/                         # committed to git, mesh-synced
    index.json                      # list of releases for the repo (generated)
    v1.2.3/
      release.json                  # release + asset metadata (the manifest)
      release.sig                   # Ed25519 signature over the canonical manifest
      SHASUMS256.txt                # generated checksum list (covered by manifest)
    latest -> v1.2.3                # symlink/alias resolved by the API, not committed
  .forkmesh/release-blobs/          # CAS — GITIGNORED, never committed
    sha256/
      de/
        adbeef…/                    # full hash as the object name
          data                      # the artifact bytes
          meta.json                 # size, refcount, first-seen, content_type
```

`.forkmesh/release-blobs/` is added to `.gitignore` so artifact bytes can never be
accidentally committed — satisfying the issue's hard requirement. The metadata
under `releases/` is tiny (KB) and rides the existing mesh git sync.

### Local disk option
Default backend: the CAS directory on the publishing node's local disk. A node
serves any blob it holds; the worker forwards download requests to a node whose
manifest-recorded `locations` include the blob. This needs zero external
infrastructure and matches ForkMesh's self-hosting model.

### Content-addressed storage
Objects are named by `sha256` of their bytes. Benefits, all of which the design
relies on:
- **Self-verifying.** A node (or client) recomputes the hash on read; a corrupt or
  substituted blob is detected without trusting the source. This is what lets us
  forward downloads through *any* node safely.
- **Immutability for free.** A given hash always maps to the same bytes; you can
  cache it forever at the edge.
- **Trivial dedup** (below).

### Deduplication
Because assets reference blobs by hash:
- Re-running CI that produces a byte-identical artifact uploads nothing (hash
  already present) — common for docs/source tarballs that don't change between
  patch releases.
- The same artifact reused across channels (`latest` and `v1.2.3`) is stored once.
- Identical artifacts across forks/repos on the same node share one blob.
Dedup is global per node via `refcount` on the blob `meta.json`.

### Cleanup & retention
- **Refcounted GC.** Deleting/yanking a release decrements `refcount` on each
  referenced blob; a blob with `refcount == 0` is eligible for GC. GC is a
  mark-and-sweep over `releases/**/release.json` (the only roots) run on a timer,
  with a grace period so an in-flight upload/finalize isn't swept.
- **Draft TTL.** Draft releases (and their `uploading` assets / orphaned partial
  blobs) older than N days (default 7) are garbage-collected — this cleans up
  abandoned/partially-failed CI runs (§7).
- **Retention policy** (per-repo, configurable): e.g. keep all tagged releases,
  but prune assets of non-latest **prereleases** after 90 days, or cap total
  release storage per repo. Pruning an asset removes the blob ref but keeps the
  signed metadata (with the asset marked `pruned`), so the historical record and
  checksums survive even when the bytes are gone.
- **Replication for durability.** Mirror nodes may opt to pre-pull release blobs
  (not just lazily) so a release survives the origin node going offline — the same
  resilience the mesh already gives repo data.

## 5. Security & permissions

### Token scopes
Extend the existing auth-token system with release scopes (least privilege):
| scope | grants |
|---|---|
| `release:read` | list/get releases, download (also implicit for public repos) |
| `release:write` | create/update **draft** releases, upload/replace assets on drafts |
| `release:publish` | finalize a draft into an immutable published release |
| `release:delete` | yank/delete a release, prune assets |

CI gets a **repo-scoped** token with `release:write` + `release:publish`, ideally
short-lived and bound to the workflow run. Separating `write` from `publish`
allows a flow where CI builds+uploads (`write`) but a human approves the
irreversible publish (`publish`).

### Who can publish
- Repo **owner** and users/teams with **maintain/admin** role on the repo.
- A repo-scoped CI token carrying `release:publish`.
- Org policy may further require that the **manifest signer's key** be on the
  repo's allowlist of release-signing keys, so even a leaked write token cannot
  produce a release that verifies.

### Validating that releases match Git tags
Enforced at create and at finalize:
1. The release `tag` must resolve to an **existing tag ref** in the repo at create
   time — no release for a branch, arbitrary commit, or nonexistent tag.
2. At finalize, the server records `tag_commit` = the commit the tag currently
   points to, and the signed manifest includes it. Downloaders (and the release
   page) can confirm `tag → tag_commit` still holds; a mismatch surfaces as a
   "tag moved since release" warning (§7 force-push).
3. Optionally require the tag itself to be a **signed** tag whose signer is on the
   repo allowlist, chaining provenance from source ref → release artifacts.

### Immutable releases vs editable metadata
- A **published** release's manifest (asset set, every `blob_sha256`,
  `tag_commit`) is **immutable**. You cannot add, remove, or replace an asset, or
  repoint an asset's bytes. To ship different artifacts you cut a new tag/release.
- A small, explicitly-mutable set of *presentation* metadata may still be edited
  after publish: `name`, `body`/notes, `prerelease` flag, and the mutable
  `download_count`. These fields are **excluded from the signed manifest** so
  editing them doesn't invalidate the signature; every edit emits a signed
  `release.edit_metadata` audit event.
- `draft` releases are fully mutable (that's the staging area CI uploads into).

### Checksum generation
- The uploader declares each artifact's `sha256`; the server (and the receiving
  node) **recompute** it from the received bytes and reject on mismatch — the
  client's claim is never trusted.
- The canonical `sha256` is stored as the asset's content address and as the blob
  name; `SHASUMS256.txt` is generated server-side from those values.
- The manifest signature covers all checksums, so checksum integrity == release
  integrity.

## 6. Download behavior

### Stable URLs
- Per asset: `…/<owner>/<repo>/releases/<tag>/<asset-name>` — stable forever; the
  bytes are immutable for a published release.
- Latest alias: `…/<owner>/<repo>/releases/latest/<asset-name>` resolves to the
  highest non-prerelease, non-draft release by semver tag order. (`install.sh`
  uses this; the alias is computed by the API, never a committed pointer, so it
  can't get out of sync.)
- Content-addressed direct: `…/<owner>/<repo>/releases/blob/sha256/<hash>` for
  cache-busting and for clients that already know the hash from the manifest.
- The release page lists every asset, its size, `sha256`, signature status, and a
  one-click "verify" hint.

### Serving path
The worker resolves `tag + asset name → blob_sha256` from the signed manifest,
then forwards the request to a node in the blob's `locations` (reusing the
existing host-forwarding used for `/blob`/clone, but **streamed and uncapped** —
the 4 MB inline `/blob` cap does not apply to the release download route). The
node streams the bytes; the worker may verify/cache. Because the content is
immutable and content-addressed, the edge caches aggressively (`Cache-Control:
public, immutable, max-age=31536000`, keyed by hash), so most downloads never
touch a node.

### Rate limits
- Per-IP and per-token request-rate limits on the download route (token buckets at
  the edge), with higher ceilings for authenticated `release:read` tokens.
- Upload routes are rate-limited separately and require `release:write`.
- Anonymous large-file downloads get a concurrency cap per IP to blunt abuse.

### Bandwidth accounting
- Each completed download increments the asset `download_count` and adds to a
  per-repo / per-release bytes-served counter (kept outside the signed manifest so
  it can change freely).
- Counters are aggregated at the edge and flushed periodically (avoids a write per
  request). Per-node egress is also tallied so the mesh can attribute serving cost
  and, if desired, feed the existing payout/leaderboard accounting.

## 7. Edge cases

- **Re-uploading an asset with the same name.**
  - *Draft release:* allowed; replaces the asset, repointing its `blob_sha256`
    (old blob `refcount--`, GC-eligible if it hits 0). Emits `asset.replace`
    audit.
  - *Published release:* rejected (immutability). Same name + same hash = idempotent
    no-op; same name + different hash = `409 Conflict`.
- **Deleting a tag after a release exists.** The release does **not** vanish — its
  metadata and blobs are independent of the tag ref. The release is flagged
  `tag-missing` (its `tag_commit` is still recorded, so downloads keep working and
  remain verifiable). A signed `tag.deleted_after_release` audit event is emitted.
  Re-creating the tag at the same commit clears the flag; at a different commit it
  becomes a force-move (below).
- **Force-pushed / moved tags.** The release is pinned to `tag_commit` captured at
  finalize. If the tag later points elsewhere, the API detects `tag != tag_commit`
  and shows a "tag moved after release" warning + `tag.force_moved` audit event;
  the *artifacts are unchanged* (they were never derived from the live ref, only
  from the captured commit). This prevents a force-push from silently re-defining
  what a published release contains.
- **Partially failed uploads.** Uploads are resumable (init → chunk → complete).
  An incomplete asset stays `state=uploading` and its partial blob is not
  reachable for download. The publisher can query received chunks and resume.
  Drafts/partials are swept by the draft TTL (§4). Finalize fails (and can be
  safely retried) if any expected asset is not `complete`.
- **Very large files.** Chunked/resumable multipart upload with per-chunk hashing;
  streamed straight into the CAS (never buffered whole in the worker). Downloads
  stream with HTTP range support. Optional max-asset-size guard per repo/plan. The
  CAS handles arbitrarily large objects; only metadata transits git.
- **Interrupted downloads.** Range requests (`Accept-Ranges: bytes`) let clients
  resume from an offset; immutable content-addressed caching makes resumed/partial
  fetches consistent. `install.sh` resumes and then verifies the final `sha256`.
- **Concurrent CI jobs.** Multiple platform jobs upload to the **same draft**
  release in parallel — safe, because each asset is keyed by `(release, name)` and
  by content hash, and writes are independent. Release creation is idempotent on
  `(repo, tag)` so racing `create` calls converge on one draft. Finalize is a
  single atomic transition guarded by a compare-and-set on `state` (`draft →
  published`); the first finalize wins, later ones are no-ops or `409`. Asset
  replace on a draft takes a per-asset lock to avoid lost updates.

## 8. API endpoints

All under `/api/repo/<owner>/<repo>`. Write/publish/delete require the matching
token scope (§5).

| method | path | scope | purpose |
|---|---|---|---|
| `POST` | `/releases` | write | create or fetch the draft release for a tag (idempotent on tag) |
| `GET` | `/releases` | read | list releases (filter by `prerelease`, `draft`) |
| `GET` | `/releases/<tag>` | read | release + asset metadata + signature status |
| `PATCH` | `/releases/<tag>` | write/publish | edit mutable metadata (`name`,`body`,`prerelease`) |
| `POST` | `/releases/<tag>/assets` | write | initiate an upload (declares `name`,`sha256`,`size`) → returns `upload_id` |
| `PUT` | `/releases/<tag>/assets/<upload_id>?offset=` | write | send a chunk (resumable) |
| `POST` | `/releases/<tag>/assets/<upload_id>/complete` | write | finalize one asset (server verifies hash) |
| `GET` | `/releases/<tag>/assets/<upload_id>` | write | query received offset (resume) |
| `POST` | `/releases/<tag>/finalize` | publish | freeze + sign manifest, commit, publish |
| `POST` | `/releases/<tag>/yank` | delete | mark `yanked` (keeps metadata, drops blob refs) |
| `DELETE` | `/releases/<tag>` | delete | delete draft (or hard-delete with policy) |
| `GET` | `/releases/<tag>/<asset-name>` | read | **download** (streamed, range-capable, edge-cached) |
| `GET` | `/releases/latest/<asset-name>` | read | download from the latest stable release |
| `GET` | `/releases/blob/sha256/<hash>` | read | content-addressed download |

The download routes are also exposed at the short, human path
`/<owner>/<repo>/releases/<tag>/<asset>` (mirroring the clone path style) for the
release page's stable links.

## 9. CI integration flow

`.forkmesh/release.yml` (rewritten — no more committing binaries):

```yaml
name: Publish release
on:
  tag: ['v*']            # tag-only trigger
jobs:
  build:
    strategy:
      matrix: [linux-x86_64, macos-arm64, windows-x86_64]
    steps:
      - build artifact for the matrix platform     # cmake --build … Release
      - name+hash it: forkmesh-<os>-<arch>[.exe], sha256
      - forkmesh release upload \                   # client sub-command
          --tag "$FORKMESH_TAG" \
          --asset "forkmesh-<os>-<arch>[.exe]" \    # resumable, dedup by hash
          --token "$FORKMESH_RELEASE_TOKEN"         # repo-scoped release:write
  publish:
    needs: build                                    # waits for all platforms
    steps:
      - forkmesh release finalize \                 # freeze + sign + commit manifest
          --tag "$FORKMESH_TAG" \
          --sign-key "$FORKMESH_RELEASE_KEY" \       # release:publish
          --notes-from CHANGELOG.md
```

`forkmesh release upload` is idempotent and resumable, so a re-run of a failed
matrix leg re-uploads only what's missing; `finalize` is the single
publish-the-release-to-the-mesh step.

## 10. Realization note (ForkMesh vs. generic Git server)

The data model (§3) and API (§8) are server-agnostic. On a conventional
single-server Git host, `releases`/`release_assets`/`artifact files` are SQL
tables and the CAS is a local dir or S3 bucket — done. ForkMesh's realization
maps the *same* model onto its mesh primitives:

- `releases` + `release_assets` + `checksums` + `signatures` → the signed
  `releases/<tag>/release.json` + `release.sig` committed to the repo and synced
  like issues/PRs (uses the existing Ed25519 event-signing machinery in
  `entry.py`).
- `artifact files` → the gitignored `.forkmesh/release-blobs/` CAS; download
  routing reuses the worker's existing host-forwarding.
- `audit events` → signed append-only events alongside the existing issue/PR
  event streams, plus the git history of `releases/` itself.

## 11. Migration from committed binaries

1. Add the new routes + CAS + `forkmesh release` client sub-command behind a flag.
2. New tags publish via this system; `install.sh` learns to resolve assets from a
   release manifest, falling back to the old `releases/<channel>/…` path during
   transition.
3. Backfill: for existing committed binaries, ingest the bytes into the CAS, write
   signed manifests for the corresponding tags, then **remove the binaries from
   the repo** (and ideally from history with a filter to reclaim clone size),
   leaving only the small `releases/` metadata.
4. Retire the "commit the binary" step in `.forkmesh/release.yml` and update
   `releases/README.md`.

## 12. Open questions

- Signature ecosystem: ship native Ed25519 manifest signatures only, or also
  emit `minisign`/`sigstore` per-asset sigs for external verifiers? (Proposed:
  native always-on; minisign/sigstore optional.)
- Edge caching cost vs. node egress accounting — where to draw the line so popular
  releases don't all stream from one origin node.
- Per-repo storage quotas / retention defaults and who configures them.
- Whether to support delta/patch downloads between adjacent releases for very
  large artifacts (CAS makes this feasible later, out of scope here).
