# ForkMesh deployment and scheduled automation

This document covers the isolated deployment and automation tools. None of
these tools writes wallet keys, Cloudflare API tokens, or GitHub tokens to the
repository.

## Cloudflare Worker, D1, DNS, and relay bootstrap

`tools/cloudflare_bootstrap.py` creates or reuses a D1 database, applies all
ForkMesh migrations, deploys the Worker and static assets, creates a proxied DNS
record, attaches the `hostname/*` Worker route, and verifies `/health`.

The deployment is a ForkMesh relay and mirror-routing instance. Setting
`--main-relay-url` lets its mirror operators participate in the existing relay
mesh after the main relay approves the new relay. Repository bytes remain on
independently operated mirror hosts; Cloudflare is discovery, routing, and
failover infrastructure rather than permanent repository storage.

Create a scoped Cloudflare API token with only:

- Account read
- Workers Scripts edit
- D1 edit
- Zone read
- DNS edit for the selected zone
- Workers Routes edit for the selected zone

Then run:

```bash
export CLOUDFLARE_API_TOKEN="locally-supplied-token"  # forkmesh-secret-scan:ignore-line
python3 tools/cloudflare_bootstrap.py --auto-configure
```

The token-only path succeeds only when the scoped token can see exactly one
account and one active zone. It derives `forkmesh.<zone>` for the relay,
`mirror.<zone>` for the direct endpoint, and stable Worker, D1, and node names.
It fails closed instead of choosing among multiple accounts or zones.

For a broader token or custom topology, provide the advanced fields explicitly:

```bash
export CLOUDFLARE_API_TOKEN="locally-supplied-token"  # forkmesh-secret-scan:ignore-line
export CLOUDFLARE_ACCOUNT_ID="account-id"
export FORKMESH_NODE_PUBLIC_KEY="base64url-ed25519-public-key"
export FORKMESH_MIRROR_MANIFEST_SIGNER="local-forkmesh-identity-signer"

python3 tools/cloudflare_bootstrap.py \
  --hostname mesh.example.com \
  --zone example.com \
  --worker-name example-forkmesh \
  --database-name example-forkmesh \
  --node-name example-node \
  --relay-label "Example relay" \
  --main-relay-url https://forkmesh.com
```

If a custom deployment's token can access exactly one account,
`CLOUDFLARE_ACCOUNT_ID` may be omitted. If the token is not in the environment,
the tool prompts for it on a terminal or reads one line from stdin in a
headless invocation.

Important security behavior:

- There is no `--api-token` command-line argument, so the token does not appear
  in the process list.
- The token is passed to Wrangler only through the child-process environment.
- A temporary Wrangler configuration contains public IDs and URLs only. It is
  permission-restricted and removed after deployment.
- The checked-in `wrangler.toml` and local `.env` files are not modified.
- Optional Worker application secrets must be named explicitly with
  `--secret-env NAME`. Each value is read from the local environment and piped
  directly to `wrangler secret put`; values are never logged or written to the
  generated configuration.
- `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` are explicitly rejected as
  Worker-secret names.
- Solana private keys are not accepted by this bootstrapper.

A trusted mirror deployment also requires the existing local ForkMesh Ed25519
identity. The public key is supplied through `FORKMESH_NODE_PUBLIC_KEY` or
`--mirror-public-key`. The command in `FORKMESH_MIRROR_MANIFEST_SIGNER` receives
a JSON signing request on stdin and returns:

```json
{
  "publicKey": "the-same-base64url-public-key",
  "signature": "64-byte-ed25519-signature-as-unpadded-base64url"
}
```

The request contains the canonical public payload as base64 and its SHA-256
digest. The signer command is invoked directly, without a shell, and private
identity material remains inside the Qt client, hardware signer, keychain, or
other local signer. A relay-only installation can explicitly use
`--skip-mirror-manifest`; it must not be selected as a trusted repository
mirror until it later publishes a valid signed manifest.

Example for an existing, locally managed application secret:

```bash
export DATA_KEY="value-from-the-operator-secret-store"
python3 tools/cloudflare_bootstrap.py \
  --hostname mesh.example.com \
  --zone example.com \
  --skip-mirror-manifest \
  --secret-env DATA_KEY
```

Use `--dry-run` for read-only account and zone validation. Existing matching
D1, DNS, and route resources are reused. A conflicting DNS record or Worker
route fails closed unless the operator explicitly supplies `--replace-dns` or
`--replace-route`.

The bootstrap does not approve itself on another operator’s main relay and does
not install a repository host on an unrelated computer. Those actions require
the respective owner’s authorization.

### Direct-HTTPS mirror discovery contract

Trusted deployments publish:

```text
https://<mirror-host>/forkmesh-mirror.json
```

The strict JSON Schema is `docs/mirror-endpoint.schema.json`. Its signed payload
contains:

- The node name and base64url Ed25519 public key.
- `https://<mirror-host>/health`.
- `https://<mirror-host>/{owner}/{repository}` as the direct repository URL
  template.
- The proxied DNS record name and `<mirror-host>/*` Worker route.
- `transport: direct-https` and `mainProxyMode: masked`, allowing the main
  Worker to stream the response while the browser keeps the main ForkMesh URL.
- `repositoryBytesInD1: false`. D1 is limited to discovery, health, routing
  metadata, and signed manifests; repository bytes remain on the independently
  operated host.

The `signature` covers every other manifest field using compact UTF-8 JSON with
sorted keys (`forkmesh-json-sort-v1`). A main router should fetch the manifest
over HTTPS, validate the schema and health URL, recompute `payloadSha256`,
verify the Ed25519 signature against the account-bound node public key, and
reject manifests whose hostname does not match the fetched origin.

## Daily one-way GitHub issue synchronization

`.github/workflows/github-issue-sync.yml` runs every day at 04:17 UTC and can
also be started manually. Manual runs default to dry-run mode.

The implemented direction is:

```text
one GitHub repository -> another GitHub repository
```

It is deliberately not bidirectional. It does not write destination edits back
to the source and does not synchronize pull requests or comments.

Configure these repository variables:

- `FORKMESH_ISSUE_SYNC_SOURCE`: optional `owner/name`; defaults to the workflow
  repository.
- `FORKMESH_ISSUE_SYNC_DESTINATION`: required destination `owner/name`.

For synchronization within the workflow repository, the scoped
`GITHUB_TOKEN` fallback can be sufficient. Cross-repository synchronization
requires encrypted Actions secrets because a repository token cannot access a
different repository:

- `FORKMESH_ISSUE_SYNC_SOURCE_TOKEN`: fine-grained token with source repository
  metadata read and issues read.
- `FORKMESH_ISSUE_SYNC_DESTINATION_TOKEN`: fine-grained token with destination
  repository metadata read and issues read/write.

`FORKMESH_ISSUE_SYNC_TOKEN` can be used instead when one narrowly scoped GitHub
App or fine-grained token has both sets of permissions. Tokens are read only
from environment variables and are never added to reports.

Each destination issue contains a hidden `forkmesh-issue-sync:v1` mapping with
the source repository, source issue number, and synchronized content digest.
That persistent mapping prevents duplicates. Unchanged digests perform no
destination write. New or changed issues preserve:

- Title and body
- Labels, creating missing destination labels
- Open or closed state
- Original author attribution
- Source creation and update timestamps in an attribution footer

The script retries rate limits and temporary server failures with bounded
backoff. It publishes a JSON mapping/count report and Markdown job summary. On
failure, the workflow creates or updates a single administrator-visible issue
in the destination repository and links to the failed workflow run.

Run the same logic locally without exposing tokens on the command line:

```bash
export SOURCE_GITHUB_TOKEN="source-token"
export DESTINATION_GITHUB_TOKEN="destination-token"

python3 tools/github_issue_sync.py sync \
  --source source-owner/source-repo \
  --destination destination-owner/destination-repo \
  --source-token-env SOURCE_GITHUB_TOKEN \
  --destination-token-env DESTINATION_GITHUB_TOKEN \
  --dry-run \
  --report /tmp/forkmesh-issue-sync.json \
  --summary /tmp/forkmesh-issue-sync.md
```

## Native daily public-safe security scan

Daily scans run on each repository owner or mirror node, not in a GitHub
workflow and not through a centrally enumerable queue. Schedule
`tools/native_security_scan_runner.py` once per hour with systemd, launchd, or
the desktop control node. The runner decides whether the repository is due,
uses an exclusive local lease, and requests a short-lived relay lease bound to
the exact repository and 40-character commit.

```bash
export FORKMESH_SESSION_TOKEN="owner-session-token"
python3 tools/native_security_scan_runner.py \
  --checkout /srv/forkmesh/widget \
  --repository alice/widget \
  --relay-url https://forkmesh.com \
  --state-dir /var/lib/forkmesh/security-scans/widget
```

The session token stays in process memory. The relay returns a one-use
repository-and-commit-scoped capability and stores only its blind index. The
runner checks Git HEAD before and after scanning, so a moving checkout cannot
be published under the leased commit. Concurrent leases return a bounded retry
time; completed scans are idempotent for 24 hours.

When the relay is offline, use `--offline` or allow the runner's connectivity
fallback. It writes only redacted public-safe artifacts with owner-only file
permissions, records `offline_artifact_pending`, and retries with exponential
backoff capped at six hours. Retention is bounded to the newest 30 commit
directories. No source excerpts, matched secret values, private diagnostics,
session credentials, or lease capabilities are written to disk.

The scanner:

- Inventories public-registry npm, PyPI, and Pub lockfile dependencies.
- Queries OSV with package names and versions only. No source code is sent.
- Detects credential-shaped content without publishing the matched value.
- Performs focused Python and JavaScript static checks.
- Emits only rule names, paths, line numbers, controlled summaries,
  recommendations, and identifiers derived only from the non-secret rule,
  path, and line. Secret matches are never included in a public digest.

Each local artifact directory contains:

- `security-scan.json`
- `security-clipboard.json`
- `security-scan.md`

The rich artifact contract explicitly prohibits raw secret values and source
excerpts. The compact clipboard converter preserves the scan status, commit,
scanner/policy identity, scope, severity/category counts, recommendations, and
limitations in the shape consumed by the virtual world. OSV outages produce a
`Scope limited` report instead of silently claiming complete coverage.

Owners and scoped security reviewers can triage an immutable scan through
`GET/PATCH /api/repo/<owner>/<repo>/security-scans/triage`. Review rows are an
encrypted overlay keyed by opaque scan/finding fingerprints; they do not rewrite
the scanner artifact. Public clipboards expose only aggregate
`unreviewed`/`confirmed`/`dismissed` counts. Per-finding paths, notes, and review
controls require owner or `security_reviewer` authorization, and every mutation
is recorded in the sensitive-action audit log.

The leased ingest request uses `Authorization: Bearer …` and a JSON envelope:

```json
{
  "schemaVersion": 1,
  "type": "forkmesh.security-scan-ingest",
  "rich": {},
  "clipboard": {}
}
```

The Worker authenticates the one-use lease before reading the body, rejects
bodies over 1 MiB, validates both schemas and their cross-document counts, and
binds the body repository and commit to the lease. It never logs the
credential, request body, artifact, or endpoint response body.

The validated envelope is encrypted with the normal Worker `DATA_KEY` before it
is written to D1. D1 stores only a keyed repository blind index, bounded
metadata, and at most the 90 newest encrypted artifacts for that repository.
Re-publishing the same canonical artifact is idempotent. The generic platform
administrator table browser cannot read or edit this table.

Read routes are:

```text
GET /api/repo/<owner>/<repo>/security-scans/latest
GET /api/repo/<owner>/<repo>/security-scans/history?limit=10
```

Anonymous reads of an explicitly public repository receive the compact,
redacted `clipboard` only. `?detail=rich` requires the repository owner's
normal signed-node query or authenticated owner account session. Every read of
a private repository requires the same owner authorization; unauthorized
private and missing repositories return the identical `404 {"error":
"not_found"}` response. Platform-administrator status is not a substitute for
repository-owner authorization.

Run an offline, deterministic scan locally:

```bash
python3 tools/security_scan.py \
  --offline \
  --repository local/forkmesh \
  --output /tmp/security-scan.json \
  --clipboard-output /tmp/security-clipboard.json \
  --summary /tmp/security-scan.md
```

Every report states that automated scans can miss vulnerabilities, applies only
to the scanned commit, and is not a guarantee of security.

## External Solana signer and reward scheduling

`tools/reward_scheduler.py` is isolated from the legacy Worker payout paths. It
does not generate or accept a private key.

The scheduler:

- Requires online, healthy, attested, integrity-checked ForkMesh mirrors.
- Requires a valid self-custodial Solana destination address.
- De-duplicates operator identities and wallet addresses for each round.
- Freezes and hashes the eligible snapshot.
- Selects one recipient deterministically from public entropy that must become
  unpredictable after the snapshot closes.
- Defaults production intents to `mainnet-beta`. `devnet` and `testnet` require
  an explicit development/test selection and matching RPC.

An external wallet or hardware-signer adapter receives the public intent over
stdin and returns a signed transaction over stdout. The signer command is
invoked directly without a shell. Responses containing fields such as
`privateKey`, `seed`, `mnemonic`, or `keypair` are rejected.

Pending rewards are metadata reservations for at most 24 hours. Funds do not
move from the source wallet until an external signer approves a claim. Expiry
marks the allocation returned to source without creating a ForkMesh-controlled
wallet or requiring an on-chain refund transfer.

This is scheduler and signer-protocol support, not a replacement of the legacy
custodial Worker paths. Those paths must not be presented as non-custodial until
they are retired or migrated.
