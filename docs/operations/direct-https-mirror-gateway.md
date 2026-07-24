# Direct-HTTPS independent mirror gateway

`tools/mirror_gateway.py` is the repository-byte origin for an independently
operated ForkMesh mirror. It serves loopback HTTP only. A Cloudflare Tunnel
creates the public TLS connection, and the main ForkMesh Worker proxies each
authorized request in place so the browser or Git client keeps the original
ForkMesh URL.

This is separate from the multiplayer socket:

- Git clone/fetch, trees, blobs, raw files, history, commits, branches, search,
  statistics, size maps, and release downloads use ordinary HTTPS.
- Chat, avatar movement, emotes, and transient world presence may use the
  multiplayer socket.
- Repository updates are discovered through bounded HTTPS sync. Persistent
  sockets remain limited to chat, avatar movement, emotes, and world presence.

The gateway's Git data plane is read-only. It implements Git `upload-pack`; it
does not implement `receive-pack`, general pushes, issue writes, administration,
or arbitrary Git service selection. An operator may separately opt one
repository into the typed exact-OID `merge-pull` control operation described
below. The main Worker returns
`501 direct_https_receive_pack_required` for receive-pack advertisement and
POST requests before reading a body. Push remains disabled until it has a
direct-HTTPS write protocol; there is no repository-byte socket fallback.

## Security boundary

The local process enforces all of these properties:

- The listener must be `127.0.0.1`, `::1`, or `localhost`.
- Every repository request needs a fresh, single-use Ed25519 capability from
  the configured routing Worker.
- The capability binds the exact method, path plus query, body digest, node,
  request ID, and epoch-millisecond issue time.
- Unknown, private, disabled, and integrity-quarantined repositories all return
  the same `404 {"ok":false,"error":"not_found"}` response.
- Health and manifest responses never enumerate repository names.
- Only explicitly configured public repositories can enter the serving index.
- Enabled public repositories need a SHA-256 pin over their sorted heads and
  tags. The pin is checked at startup, during signed repository health checks,
  and before serving.
- Git is invoked directly without a shell. System/global Git configuration,
  prompts, optional locks, external protocol helpers, credential helpers,
  fsmonitor commands, alternate-ref commands, and upload-pack pack hooks are
  disabled at the serving boundary.
- `refs/forkmesh/` is internal node state. Upload-pack hides the entire
  namespace and rejects arbitrary tip/reachable-object wants, so merge recovery
  refs and their otherwise-unreachable objects cannot be advertised or fetched.
  The SSH gateway applies the same upload rule and also uses
  `receive.hideRefs` so a push cannot inspect, create, update, or delete those
  refs.
- Release files are content addressed and re-hashed before any bytes are sent.
- Default HTTP logging is disabled because it would expose client addresses and
  raw query paths. Operational logs contain only time, a bounded request ID,
  generalized operation, status, duration, and byte count.
- Git stderr, repository paths, source contents, request queries, user agents,
  and client addresses are not logged.
- The gateway accepts no private-key, token, password, seed, mnemonic, keypair,
  or credential field in its JSON configuration.

The strict configuration contract is
`docs/mirror-gateway-config.schema.json`. A Tunnel-backed endpoint manifest uses
`docs/mirror-gateway-manifest.schema.json`; it records a proxied CNAME and
Cloudflare Tunnel and does not falsely claim that the hostname has a Worker
route.

## Identity helpers

Python's standard library does not provide Ed25519. ForkMesh does not substitute
an invented cipher or a shared HMAC. Two direct, shell-free local commands
provide the cryptographic boundary:

For a non-GUI VPS, the production implementation and command-array examples
are in [Headless mirror identity](headless-mirror-identity.md).

1. `healthSignerCommand` holds or accesses the node identity in the Qt client,
   OS keychain, HSM, or another owner-controlled signer.
2. `requestVerifierCommand` verifies signatures made by the routing Worker's
   configured public Ed25519 key.

Both commands receive one bounded JSON object on standard input and return one
bounded JSON object on standard output. The gateway removes ambient environment
variables whose names look like credentials. Helpers should access private
identity material from the owner-controlled keychain or hardware device, not
from their command arguments or response.

Health signer input:

```json
{
  "schemaVersion": 1,
  "type": "forkmesh.health-challenge-signing",
  "algorithm": "Ed25519",
  "encoding": "base64url-no-padding",
  "publicKey": "<32-byte public key>",
  "messageBase64": "<canonical message>",
  "messageSha256": "<sha256>"
}
```

Health signer output:

```json
{
  "publicKey": "<same public key>",
  "signature": "<64-byte Ed25519 signature>"
}
```

Capability verifier input:

```json
{
  "schemaVersion": 1,
  "type": "forkmesh.request-capability-verification",
  "algorithm": "Ed25519",
  "encoding": "base64url-no-padding",
  "publicKey": "<routing Worker public key>",
  "messageBase64": "<canonical message>",
  "messageSha256": "<sha256>",
  "signature": "<64-byte Ed25519 signature>"
}
```

Capability verifier output:

```json
{
  "valid": true,
  "publicKey": "<same routing Worker public key>"
}
```

No helper response may contain a private-key-shaped field.

## Configuration

First compute the mirror's refs pin:

```bash
python3 tools/mirror_gateway.py \
  --refs-sha256 /srv/forkmesh/mirrors/alice-project.git
```

Create an operator-owned JSON file outside the checkout:

```json
{
  "schemaVersion": 1,
  "node": {
    "name": "alice-mirror",
    "publicKey": "<base64url Ed25519 public key>"
  },
  "routerPublicKey": "<base64url routing Worker Ed25519 public key>",
  "publicOrigin": "https://mirror1.example.com",
  "listen": {
    "host": "127.0.0.1",
    "port": 8790
  },
  "manifestPath": "/etc/forkmesh/forkmesh-mirror.json",
  "requestVerifierCommand": [
    "/usr/local/bin/forkmesh-verify-router-capability"
  ],
  "healthSignerCommand": [
    "/usr/local/bin/forkmesh-sign-health"
  ],
  "limits": {
    "maxReleaseBytes": 2147483648
  },
  "repositories": [
    {
      "owner": "alice",
      "name": "project",
      "visibility": "public",
      "enabled": true,
      "encryptedArchive": {
        "scheme": "age-encrypted-tar-v1",
        "ciphertextPath": "/srv/forkmesh/encrypted/alice-project.tar.age",
        "ciphertextSha256": "<sha256 of ciphertext>",
        "keyReference": "keychain:forkmesh/alice-project",
        "materializeCommand": [
          "/usr/local/bin/forkmesh-materialize-age-repository"
        ]
      },
      "releaseStore": "/srv/forkmesh/releases/alice-project",
      "integrity": {
        "expectedRefsSha256": "<64 lowercase hex characters>"
      }
    },
    {
      "owner": "alice",
      "name": "private-project",
      "visibility": "private",
      "enabled": false
    }
  ]
}
```

An omitted `visibility` fails closed to private. A private entry cannot be
enabled and cannot include a storage path. Its name exists only in the local
operator file; the process does not inspect, materialize, index, or announce it.

Validate without opening a listener:

```bash
python3 tools/mirror_gateway.py \
  --config /etc/forkmesh/mirror-gateway.json \
  --check
```

Run locally:

```bash
python3 tools/mirror_gateway.py \
  --config /etc/forkmesh/mirror-gateway.json
```

## Encryption at rest

Every enabled public mirror must use the external strong-envelope archive
adapter. A plaintext `gitDir` configuration is rejected. The gateway does not
implement or invent encryption.

The only accepted scheme is `age-encrypted-tar-v1`, using the established
[age file format](https://age-encryption.org/v1). The gateway requires a native
or ASCII-armored age header and the configured full-file SHA-256 before invoking
the materializer. The external materializer must run an age implementation that
authenticates the file, fail on any authentication/decryption error, and extract
exactly one bare-repository tar archive. Generic or operator-defined envelope
labels are rejected.

An enabled public mirror declares:

```json
{
  "encryptedArchive": {
    "scheme": "age-encrypted-tar-v1",
    "ciphertextPath": "/srv/forkmesh/encrypted/alice-project.tar.age",
    "ciphertextSha256": "<sha256 of ciphertext>",
    "keyReference": "keychain:forkmesh/alice-project",
    "materializeCommand": [
      "/usr/local/bin/forkmesh-materialize-age-repository"
    ]
  }
}
```

The materializer receives the ciphertext path and digest, opaque key reference,
and an owner-only temporary destination. It resolves the key locally, verifies
and decrypts with the named established scheme, extracts one bare repository,
and returns:

```json
{"ok": true, "repositoryPath": "repo.git"}
```

The key itself is never present in the gateway configuration, request, log, or
response. Plaintext runtime data lives only in a mode-0700 temporary directory
and is removed when the gateway exits normally.

The Qt client implements this contract in `PublicMirrorRuntime`. It clones or
refreshes a canonical bare repository in an owner-only temporary directory,
streams `tar` output directly into the official `age` executable, and
atomically installs only the `.age` ciphertext plus non-secret integrity
metadata in the durable public-mirror directory. It does not write a plaintext
tar file. A per-archive age X25519 identity is held in an AES-256-GCM vault
bound to the local node identity. `age`, `age-keygen`, `tar`, and Git are
required; if any is missing or authentication fails, sync and publication fail
closed without creating a plaintext durable mirror. Existing managed plaintext
mirrors are removed only after the encrypted replacement has been authenticated
and successfully reopened.

Exact limitations:

- A mirror operator who controls the running machine can inspect plaintext
  after their approved materializer decrypts a public repository. Encryption at
  rest protects stopped storage; it is not protection from the active host
  administrator.
- Unexpected process termination can leave operating-system or filesystem
  artifacts. Use an encrypted volume or ephemeral RAM-backed runtime directory
  when that threat matters.
- Private repositories are never materialized by this gateway. The Qt control
  node persists them with `PrivateMirrorStore`; the gateway's separate
  `/v1/private-replicas/<opaque-id>` route streams that validated ciphertext
  only after a short-lived routing-Worker capability. It has no route from a
  private repository name to an opaque id and cannot decrypt the archive.
- Rotation is performed by the external encryption tool: write a new ciphertext
  under a new key reference, update its ciphertext digest and reference
  atomically, validate, then restart. Old ciphertext and keys are retired using
  the operator's retention policy.

### Private replica boundary

`qt_client/src/PrivateMirrorStore.cpp` is the private-repository storage
boundary. It stores no owner account name, organization, repository name,
source path, or private identity material. A replica filename is a random
256-bit opaque id and the file contains only:

- an AES-256-GCM encrypted whole-mirror archive;
- a hybrid X25519 + ML-KEM-768 content-key wrap for each authorized recipient;
- the owner's public-key identifier, ciphertext and exact-file digests, key
  epoch, and non-identifying timestamps; and
- recipient public-key identifiers needed to select a key wrap.

The replica directory is owner-only and each file is mode `0600`. Writes and
recipient rotations are atomic. Rotation decrypts locally with the current
owner identity, creates a fresh content key and wraps it to the exact new
recipient set, increments the authenticated key epoch, and replaces the stored
ciphertext. Removing a recipient therefore revokes access to new epochs.
Private identity halves stay in the owner or collaborator's separate local
identity vault; they are never stored beside replica ciphertext or sent to the
Worker.

A mirror operator can observe that opaque ciphertext exists, its approximate
size, timestamps, and recipient key identifiers. The operator cannot derive the
repository name or plaintext without being an authorized recipient. An
authorized recipient—and anyone who controls that recipient's unlocked
device—can read the decrypted repository. Revocation cannot erase an older
ciphertext or plaintext copy that a recipient already retained, so owners
should rotate immediately and apply an appropriate backup/retention policy.
Disk encryption is recommended to reduce filesystem-journal and swap leakage
during authorized local decryption.

The Qt control-node workflow is:

1. The device derives a vault-unlock secret from its local Ed25519 identity and
   loads or creates a hybrid X25519 + ML-KEM-768 identity. Its private halves
   are AES-256-GCM encrypted in a separate mode-`0600` vault. A signed-in,
   desktop-capable account registers only the public bundle through
   `POST /api/security/owner-keys`.
2. An owner sync bundles a local Git repository in memory. A remote source is
   accepted only as a canonical HTTPS URL without user info, query, fragment,
   or control characters. Git runs with system/global configuration,
   credential helpers, URL rewrites, hooks, redirects, and non-HTTPS protocols
   disabled. The compact authorization header is supplied only as an explicit
   process argument and is never logged.
3. The first seal creates an opaque replica; every later sync uses a fresh
   content key and increments the epoch. A migrated managed plaintext bare
   mirror is deleted only after the encrypted replica authenticates and a new
   temporary materialization succeeds.
4. Before refreshing a published private repository, and after every share
   add/remove, the owner fetches a fresh owner-signed `/shares` response. Qt
   requires a one-to-one match between grantees and public recipient bundles,
   verifies every key identifier, and requires `encryptionReady: true`,
   `missingEncryptionKeys: []`, and `privateKeysStored: false`. Any mismatch
   blocks rotation and publication. Successful rotation seals to the exact
   owner-plus-collaborator set before catalog-v2 and private-route publication.
5. An authorized collaborator registers their public bundle. Their signed,
   ACL-gated, no-store catalog response then includes a random 64-hex
   `privateAccessId`, and Qt downloads
   `GET /api/private-replicas/<privateAccessId>` with a short-lived
   grantee-signed Basic authorization header. Owner and repository names never
   appear in this request URL or query string. Qt follows no redirect, accepts
   only `application/vnd.forkmesh.private-replica+json`, requires the exact
   quoted SHA-256 ETag and content length, and buffers at most 256 MiB. Import
   rejects file-digest mismatches, conflicting same-epoch bytes, owner changes,
   and epoch rollback.
6. Owners and collaborators decrypt only with their local vault identity. The
   bundle is staged mode `0600`, imported into a mode-`0700` temporary bare
   repository, and deleted immediately; the temporary repository is recursively
   removed when its authorization scope ends. Private repositories do not open
   a named repository control socket, so their identity cannot leak through
   presence or probing.

Private-replica code must not log plaintext, repository identities, source
paths, opaque ids, envelope bodies, or decryption errors containing user data.
Gateway logs continue to use only the generalized `private-replica` operation
and a bounded opaque request id.

Abrupt termination, swap, crash dumps, filesystem journals, or an administrator
of an unlocked recipient device can expose authorized runtime plaintext. The
application's cleanup narrows that window but cannot eliminate operating-system
artifacts. Revocation protects new epochs and prevents future authorized
downloads; it cannot erase ciphertext or plaintext a former recipient already
retained.

For public repositories, confidentiality is optional but authenticity is not.
The main router must compare the signed observed refs digest with the repository
owner's signed canonical state, not blindly trust a mirror operator's local pin.

## One-click Cloudflare Tunnel

`tools/cloudflare_tunnel_bootstrap.py` creates or reuses:

- A remotely managed Cloudflare Tunnel.
- A remote ingress from the requested hostname to
  `http://127.0.0.1:<gateway-port>`.
- A catch-all `http_status:404` ingress rule.
- A proxied CNAME from the hostname to
  `<tunnel-id>.cfargotunnel.com`.
- An optional public signed `forkmesh-mirror.json`.

This tool does not replace or change `tools/cloudflare_bootstrap.py`. The
existing bootstrap remains responsible for the Worker, D1, migrations, static
assets, and Worker route. A deployment can run both, or an existing ForkMesh
instance can add the independent gateway later.

Use a scoped Cloudflare API token with Account read, Zone read, DNS edit for the
selected zone, and Cloudflare Tunnel/Connector write. Supply it through
`CLOUDFLARE_API_TOKEN`, a hidden terminal prompt, or one line on stdin:

```bash
export CLOUDFLARE_API_TOKEN="locally-supplied-token"  # forkmesh-secret-scan:ignore-line
export CLOUDFLARE_ACCOUNT_ID="account-id"
export FORKMESH_NODE_PUBLIC_KEY="<public Ed25519 key>"
export FORKMESH_MIRROR_MANIFEST_SIGNER="/path/to/local-manifest-signer"

python3 tools/cloudflare_tunnel_bootstrap.py \
  --hostname mirror1.example.com \
  --zone example.com \
  --tunnel-name alice-mirror \
  --node-name alice-mirror \
  --origin-port 8790 \
  --manifest-output /etc/forkmesh/forkmesh-mirror.json \
  --gateway-config /etc/forkmesh/mirror-gateway.json \
  --launch
```

`--launch` passes the connector credential to `cloudflared` only in its process
environment. It never passes the Cloudflare API token to either child.

For a system service, explicitly choose a connector token file:

```bash
python3 tools/cloudflare_tunnel_bootstrap.py \
  --hostname mirror1.example.com \
  --zone example.com \
  --tunnel-name alice-mirror \
  --node-name alice-mirror \
  --origin-port 8790 \
  --tunnel-token-file /etc/forkmesh/cloudflared-token

cloudflared tunnel run --token-file /etc/forkmesh/cloudflared-token
```

The connector file is created with mode `0600`. It is a sensitive Tunnel
connector credential and should be placed in the operator's secret store and
rotated through Cloudflare. It is not the Cloudflare API token, a ForkMesh
identity key, a repository decryption key, or a wallet key.

With neither `--launch` nor `--tunnel-token-file`, the tool provisions the
Cloudflare resources and deliberately does not retrieve or print the connector
credential. Use `--dry-run` for read-only account/zone/existing-tunnel checks.
Conflicting DNS fails closed unless `--replace-dns` is explicit.

### Qt Control Node lifecycle

The desktop Control Node page operates both deployment stages:

1. **Deploy Worker + mirror** runs the pinned Worker bootstrap for the relay
   hostname, then the pinned Tunnel bootstrap for a distinct direct-mirror
   hostname. The session API token is cleared from the input immediately,
   exists only in the child environment while provisioning runs, is redacted
   from displayed output, and is never written to settings or argv.
2. The Tunnel bootstrap writes its connector token to the explicit local
   owner-only file and writes the signed endpoint manifest. This connector
   credential is intentionally retained on the operator's device so the
   desktop can restart `cloudflared`; it is not a Cloudflare API token,
   repository key, wallet key, or ForkMesh identity key.
3. **Start mirror services** validates and rewrites the strict gateway
   configuration from authenticated encrypted archives, starts the loopback
   gateway, starts `cloudflared` with the connector token only in its child
   environment, checks local health, and posts the owner-signed endpoint
   registration to the Worker. If no system connector is present, the desktop
   safely downloads the exact SHA-256-pinned Cloudflare `2026.7.2` release to
   owner-only app data with the bundled `cloudflared_install.py`; it uses no
   shell pipeline and never executes bytes before both archive and executable
   verification succeed.
4. The desktop refreshes health and registration every five minutes.
   **Stop mirror services** terminates the gateway, Tunnel connector, and
   minimal repository update channels. Closing the desktop also terminates and
   clears every deployment/service child process.

Configuration refreshes caused by sync, visibility, or serving-permission
changes restart the running gateway. An unsealed or unauthenticated public
repository is omitted rather than falling back to a plaintext `gitDir`.
Private repositories are represented only by the optional opaque ciphertext
store; they are never added as named repository entries.

## Main Worker integration contract

The main Worker owns authorization, selection, geographic/latency preference,
and bounded failover. D1 stores discovery and health metadata only. It never
stores repository bytes.

Configure a dedicated Ed25519 capability identity in the main instance's
gitignored `cloudflare_worker/.env.production`:

```bash
python3 - <<'PY'
import base64
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

private = Ed25519PrivateKey.generate()
seed = private.private_bytes(
    serialization.Encoding.Raw,
    serialization.PrivateFormat.Raw,
    serialization.NoEncryption(),
)
public = private.public_key().public_bytes(
    serialization.Encoding.Raw,
    serialization.PublicFormat.Raw,
)
encode = lambda value: base64.urlsafe_b64encode(value).decode().rstrip("=")
print("MIRROR_ROUTER_PUBLIC_KEY=" + encode(public))
print("MIRROR_ROUTER_SIGNING_SEED=" + encode(seed))
PY
```

Move the seed directly into the instance operator's secret manager or
`.env.production`; do not put it in a repository, D1, a mirror manifest, or a
gateway command argument. `deploy.sh secrets` uploads it as a Worker secret.
This is a service capability key, not a user, repository-decryption, Solana, or
wallet key. Mirrors receive only its public key.

### 1. Endpoint registration

`GET /api/mirrors/https` returns the routing public key and protocol identifiers
without listing endpoints or repositories. A node registers or rotates its
endpoint with:

```http
POST /api/mirrors/https
Content-Type: application/json

{
  "node": "alice-mirror",
  "baseUrl": "https://alice-mirror.example.com",
  "publicKey": "<account-bound Ed25519 public key>",
  "issuedAt": 1784800000000,
  "signature": "<base64url Ed25519 signature>"
}
```

The signature covers:

```text
forkmesh-https-endpoint-v1
<node>
<normalized base URL>
<public key>
<issuedAt>
```

The timestamp must be fresh and updates are monotonic. The public key must
already be the node account's primary key or an enabled owner-sign device key.
The response reports public organization aliases currently linked to the node;
actual organization routing continues to use the authoritative
`org_repos.node_owner` link. An org name never grants a mirror access to a
private repository.

Fetch `https://<origin>/forkmesh-mirror.json` over HTTPS and require:

- The fetched origin equals `endpoint.origin`,
  `endpoint.healthUrl`, and `endpoint.manifestUrl`.
- `transport` is `direct-https`; `mainProxyMode` is `masked`.
- The manifest node/public key matches the account-bound mirror identity.
- `payloadSha256` equals SHA-256 of compact sorted-key UTF-8 JSON after removing
  `signature`.
- The Ed25519 signature is valid.
- The base URL passes the public HTTPS/SSRF policy.
- `dns.recordType` is `CNAME`, `dns.proxied` is true, its target is the
  manifest's `<tunnel-id>.cfargotunnel.com`, and `edge.kind` is
  `cloudflare-tunnel` with `originExposure: loopback-only`.

A signed `dns.proxied` claim is not sufficient on its own. Registration also
resolves the endpoint through Cloudflare's authenticated DNS-over-HTTPS
service and requires every returned A/AAAA address to fall within the current,
bounded IPv4/IPv6 range documents fetched from `www.cloudflare.com`. The
scheduled health path repeats that fail-closed check with short in-memory
caches before the endpoint remains eligible.

Store or update `mirror_https_endpoints`:

```text
node_bi, node_name, base_url, public_key, registration_sig, issued_at,
checked_at, latency_ms, region, healthy, integrity,
health_sig, forkmesh_verified_at, forkmesh_refs_sha256,
forkmesh_operations_sha256, forkmesh_active, updated_at
```

The row contains no repository bytes, private names, keys, raw IP addresses, or
wallet material.

### 2. Signed health and repository proof

Generic challenge:

```text
GET /health?nonce=<base64url-16..128>&issuedAt=<epoch-ms>
```

The node signs this newline-joined UTF-8 message:

```text
forkmesh-https-health-v1
<node>
<nonce>
<issuedAt>
```

To prove eligibility for mirroring the public `forkmesh/forkmesh` repository:

```text
GET /health?nonce=<nonce>&issuedAt=<epoch-ms>&owner=forkmesh&repo=forkmesh
```

The response adds:

```json
{
  "repositoryProof": {
    "owner": "forkmesh",
    "repository": "forkmesh",
    "available": true,
    "integrity": "ok",
    "refsSha256": "<observed heads/tags digest>",
    "operations": ["blob", "git-info-refs", "git-upload-pack"],
    "operationsSha256": "<digest of newline-joined sorted operations>"
  }
}
```

The signature covers this newline-joined UTF-8 message:

```text
forkmesh-https-health-repository-v1
<node>
<nonce>
<issuedAt>
<owner>
<repo>
<1 when available, otherwise 0>
<ok or unavailable>
<refsSha256>
<operationsSha256>
```

For an unavailable, unknown, or private repository, both digest fields are the
SHA-256 of empty bytes/list and the signed state is `0` plus `unavailable`.
Private and unknown are not distinguished.

Reward eligibility may project the verified public proof into:

```text
forkmesh_active INTEGER NOT NULL DEFAULT 0
forkmesh_verified_at INTEGER NOT NULL DEFAULT 0
forkmesh_refs_sha256 TEXT NOT NULL DEFAULT ''
forkmesh_operations_sha256 TEXT NOT NULL DEFAULT ''
```

Set `forkmesh_active=1` only after checking freshness, node signature,
`available=true`, `integrity=ok`, observed refs against the owner-signed
canonical refs state, and the required operation set. Clear it after a failed or
stale proof. Reward selection must additionally require endpoint
`healthy=1`, fresh `checked_at`, and general `integrity='ok'`.

The every-minute cron challenges an oldest-first bounded batch. A failed,
expired, revoked-key, malformed, or invalidly signed response immediately clears
`healthy` and `forkmesh_active`; routing accepts health for no more than two
minutes. A healthy endpoint that does not carry `forkmesh/forkmesh` may still
serve other public repositories, but it is not eligible for the flagship mirror
reward projection.

### 3. Per-request capability

After normal public/private repository authorization and mirror selection, map
the client route to one internal target:

```text
/v1/repositories/<owner>/<repo>/<operation>?<allowed-query>
```

Compute the SHA-256 of the exact request body and sign this newline-joined UTF-8
message with the routing Worker's Ed25519 key:

```text
forkmesh-masked-proxy-v1
<node>
<GET|HEAD|POST>
<exact path and query>
<lowercase body sha256>
<request id>
<epoch-millisecond issuedAt>
```

Send these headers:

```text
X-ForkMesh-Node: <node_name>
X-ForkMesh-Request-Id: <12..80 base64url characters>
X-ForkMesh-Issued-At: <epoch milliseconds>
X-ForkMesh-Body-Sha256: <64 lowercase hex>
X-ForkMesh-Signature: <64-byte Ed25519 signature, base64url no padding>
```

The request ID is single-use for the 60-second acceptance window. The gateway
recomputes the body digest and verifies the signature through its configured
external verifier.

### 4. Operations

| Operation | Method | Allowed query | Response |
| --- | --- | --- | --- |
| `git-info-refs` | GET/HEAD | `service=git-upload-pack` | Git upload-pack advertisement |
| `merge-pull` | POST | none; bounded typed JSON body | Asynchronous exact-OID pull merge through an explicitly configured node executor |
| `actions-status` | GET | none | Owner/write-authorized bounded redacted run summaries from this executor |
| `git-upload-pack` | POST | none | streamed Git upload-pack result |
| `tree` | GET/HEAD | `path`, `ref` | bounded JSON tree with sizes/commit activity |
| `blobs` | GET/HEAD | repeated `path`, optional `ref` | bounded compatibility batch for the web UI |
| `blob` | GET/HEAD | `path`, `ref` | bounded UTF-8/base64 JSON blob |
| `raw` | GET/HEAD | `path`, `ref` | streamed blob bytes |
| `history` | GET/HEAD | `ref` | last 60 commits |
| `commit` | GET/HEAD | `path` (commit hash) | metadata, file list, bounded diff |
| `branches` | GET/HEAD | none | heads/remotes |
| `search` | GET/HEAD | `path` (query), `ref` | bounded fixed-string results |
| `stats` | GET/HEAD | `ref` | files, extensions, contributors |
| `sizes` | GET/HEAD | `ref` | bounded directory-size tree |
| `release-blob` | GET/HEAD | `sha256` | verified streamed release bytes |

`actions-status` is never enabled by default. Its gateway configuration must
name an `actionsSummaryPath` in an owner-protected real directory. The file is
limited to 256 KiB, twenty runs, 16 KiB of already-redacted log tail per run,
and a fifteen-minute lease. The gateway filters it to the exact requested
repository, and the Worker independently revalidates every field and disables
caching. Workflow variables, commands, local paths, and full logs are not part
of this response or the public mirror catalog.

For upload-pack, require:

```text
Content-Type: application/x-git-upload-pack-request
```

An upload-pack request may use `Content-Encoding: gzip`. The capability body
digest covers the transported compressed bytes; the gateway verifies that
digest first, then expands gzip with the same 8 MiB bound before invoking Git.
Other content encodings fail closed.

Upload-pack explicitly leaves `allowTipSHA1InWant`,
`allowReachableSHA1InWant`, and `allowAnySHA1InWant` disabled. This is required
in addition to hiding `refs/forkmesh/`: enabling an object-id escape hatch can
make a guessed internal ref tip fetchable even when its name is absent from the
advertisement.

Do not forward client cookies, authorization headers, Cloudflare credentials,
raw client IP headers, or arbitrary query fields to the mirror. Do not expose
the selected origin through `Location`, `Server`, cookies, response debug
headers, HTML, or error bodies.

### 5. Masked response and failover

The main Worker streams the selected response under the original
`https://forkmesh.com/<owner>/<repo>...` URL. It allowlists response content
headers, adds `X-Content-Type-Options: nosniff`, and does not send a browser
redirect.

Retry the next eligible endpoint only before response bytes have been committed.
A started Git pack/raw/release stream cannot safely jump mirrors midway. A
subsequent request can use the next healthy endpoint. No eligible endpoint
returns the existing truthful mirror-unavailable response; it must not fall
back to sending repository bytes over the multiplayer socket.

The Worker applies this path to public smart-HTTP upload-pack clone traffic,
repository browse APIs, raw files, and release blobs. Endpoint candidates must
belong to the same public catalog mirror group, pass owner-attested refs pins,
have fresh signed health, and report integrity `ok`.
Approximate request country and measured challenge latency influence ordering;
the per-repository cursor rotates ties, and the clone advertisement pins its
node briefly so the following upload-pack request starts with the same mirror.

Private and unknown repositories never enter public endpoint selection and are
never included in endpoint, health, or routing listings. Unauthenticated probes
receive the same not-found response. After a valid owner/share token, private
reads use the separate opaque ciphertext path below; the Worker never falls
back to the desktop content socket.

### 6. Owner-authorized private ciphertext routes

The owner registers or revokes a route with `POST /api/mirrors/private`. The
body names the repository only inside the authenticated TLS request:

```json
{
  "owner": "alice",
  "repository": "private-project",
  "node": "alice-mirror",
  "opaqueId": "<64 lowercase hex characters>",
  "replicaSha256": "<sha256 of the exact .fm-private file>",
  "keyEpoch": 3,
  "active": true,
  "issuedAt": 1784800000000,
  "signature": "<owner Ed25519 signature>"
}
```

The signature covers newline-joined UTF-8:

```text
forkmesh-private-route-v1
<owner>
<repository>
<node>
<opaqueId>
<replicaSha256>
<keyEpoch>
<1 when active, otherwise 0>
<issuedAt>
```

The Worker requires a fresh timestamp, the registered owner's key, an exact
private `owner-sealed-v1` catalog record at the same key epoch, and an existing
account-bound HTTPS endpoint. Updates are monotonic. Migration
`0050_private_mirror_routes.sql` stores only blind repository/binding indexes,
the endpoint blind index, random opaque id, ciphertext digest, epoch, public
signature, and timestamps. There is no list API.
Migration `0053_private_replica_access.sql` adds the exact opaque-id lookup
index for databases that applied the original route migration before the
identity-free client endpoint existed.

An owner or authorized collaborator downloads through
`GET /api/private-replicas/<privateAccessId>` using the existing
`forkmesh-view-v1` or `forkmesh-share-view-v1` token in HTTP Basic. Query
credentials and named private-repository download routes are not accepted.
The Worker first resolves the random id to exactly one encrypted private
catalog record, then checks the owner/share signature. Unknown, inactive,
ambiguous, and unauthorized ids all return the same no-store `404` response.
Only after that authorization does the Worker select a healthy,
integrity-verified endpoint and internally request:

```text
/v1/private-replicas/<opaqueId>
```

The repository name, viewer token, cookies, authorization header, raw client
address, and query string are not forwarded. The gateway validates the
`.fm-private` envelope shape and embedded ciphertext digest, then streams the
exact file with an exact-body SHA-256 ETag. The Worker compares that ETag with
the owner-signed route before streaming under the original ForkMesh URL and
forces `Cache-Control: no-store`. The recipient verifies and decrypts locally.
Wrong authorization, unknown repositories, and missing private repositories
remain indistinguishable before authorization.
