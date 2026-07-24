# Headless mirror refresh and publication

`tools/headless_mirror_refresh.py` is the fail-closed bridge between an
SSH-writable bare Git repository and `mirror_gateway.py`. It does not load a
node private key, an age identity, a Cloudflare token, or a wallet key. All
signing and encryption operations go through the bounded protocols in
`headless_mirror_identity.py`.

The four modes form an intentional deployment and renewal sequence:

1. `refresh` checks the exact bare source with `git fsck --full --strict`,
   creates a new age-encrypted snapshot, validates a staged gateway
   configuration, and atomically installs that configuration.
2. The service manager restarts `mirror_gateway.py` against the new
   configuration.
3. `register` validates the active source/archive/configuration, registers the
   HTTPS endpoint, and then publishes the node-owner catalog-v2 record.
4. `renew` re-authenticates the immutable active generation and republishes the
   signed endpoint/catalog lease without a full Git fsck or a second encrypted
   repository materialization.

`check` performs the active-state validation from step 3 but makes no network
request and does not reseal the repository. It is suitable for a gateway
`ExecStartPre`. It opens the refresh-created advisory lock read-only, so the
check remains compatible with a `ProtectSystem=strict` service sandbox. A
missing lock fails closed instead of making the pre-start check mutate state.

This separation prevents the public catalog from advertising new refs before
the gateway has restarted. If endpoint registration or catalog publication
fails, the valid local generation remains active and `register` can be retried.

## Storage and permissions

Run the identity helper, refresh tool, and gateway as the same dedicated
unprivileged account. The following are mandatory:

- The refresh JSON is a regular, single-link, owner-only file, normally mode
  `0600`.
- The identity, archive, and gateway-state directories are owned by that
  account and mode `0700`.
- The bare source is a real directory owned by that account and is not
  world-writable. Group write is permitted for a deliberately configured SSH
  repository-sharing group.
- The helper, gateway, Python, and Git programs are owned by root or the
  service account and are not group- or world-writable.
- The manifest is a real regular file owned by root or the service account and
  is not group- or world-writable.
- Sensitive paths may not be symbolic links or traverse symbolic-link
  components.

For example:

```bash
install -d -o forkmesh-mirror -g forkmesh-mirror -m 0700 \
  /var/lib/forkmesh-mirror/identity \
  /var/lib/forkmesh-mirror/encrypted \
  /var/lib/forkmesh-mirror/gateway

install -o root -g root -m 0644 \
  tools/headless_mirror_identity.py \
  tools/headless_mirror_refresh.py \
  tools/mirror_gateway.py \
  /opt/forkmesh/tools/
```

The source repository must already be a bare Git repository. Do not point the
tool at a working tree, an alternate-object overlay, or a symlink.

## Secret-free configuration

The exact contract is
`docs/headless-mirror-refresh.schema.json`. The runtime rejects duplicate
fields, unknown fields, secret-shaped fields, oversized JSON, unsafe file
modes, unexpected owners, and symlinks even when an external schema validator
was not run.

```json
{
  "schemaVersion": 1,
  "type": "forkmesh.headless-mirror-refresh",
  "sourceRepository": "/srv/git/forkmesh.git",
  "archiveDirectory": "/var/lib/forkmesh-mirror/encrypted",
  "releaseStore": "/var/lib/forkmesh-mirror/releases",
  "gatewayConfigPath": "/var/lib/forkmesh-mirror/gateway/mirror-gateway.json",
  "identityStateDirectory": "/var/lib/forkmesh-mirror/identity",
  "identityHelperPath": "/opt/forkmesh/tools/headless_mirror_identity.py",
  "mirrorGatewayPath": "/opt/forkmesh/tools/mirror_gateway.py",
  "pythonProgram": "/usr/bin/python3",
  "gitProgram": "/usr/bin/git",
  "manifestPath": "/var/lib/forkmesh-mirror/gateway/forkmesh-mirror.json",
  "nodeOwner": "mirror2",
  "repositoryName": "forkmesh",
  "ownerAliases": [
    "mirror2",
    "forkmesh"
  ],
  "workerOrigin": "https://forkmesh.com",
  "publicOrigin": "https://mirror2.forkmesh.com",
  "listen": {
    "host": "127.0.0.1",
    "port": 8790
  },
  "operations": [
    "git-info-refs",
    "git-upload-pack",
    "tree",
    "blobs",
    "blob",
    "raw",
    "history",
    "commit",
    "compare",
    "branches",
    "search",
    "stats",
    "sizes",
    "release-blob"
  ],
  "catalog": {
    "description": "ForkMesh mirror",
    "branch": "main",
    "platform": "git"
  }
}
```

`nodeOwner` must match the node name in the identity helper's `public-info`
response and must appear in `ownerAliases`. Every alias becomes a separate
public gateway route, but all entries point to the exact same ciphertext,
ciphertext digest, age key reference, and refs digest. Only the `nodeOwner`
record is sent to `/api/repositories`, because catalog publication is bound to
that registered account's Ed25519 key. An organization alias such as
`forkmesh/forkmesh` remains an explicit Worker-side organization/repository
mapping; an alias does not grant organization membership or private access.

`workerOrigin` and `publicOrigin` are credential-free HTTPS origins. The node
identity must already pin `publicOrigin`. `catalog.solana`, when present, is
only a public, self-custodial payout address. Never place a seed phrase, private
key, API token, password, or wallet credential in this file.

`releaseStore` is optional. When configured, it must be an existing owner-only
directory containing the node's content-addressed `sha256/<prefix>/<digest>/data`
release assets. It is rendered onto every public alias backed by this encrypted
repository so immutable release downloads can round-robin with clone and browse
traffic. The refresh never copies assets into that directory and never treats
it as repository source; operators must replicate a published blob and verify
its SHA-256 before exposing it.

Install the final configuration with:

```bash
install -o forkmesh-mirror -g forkmesh-mirror -m 0600 \
  /path/to/mirror-refresh.json \
  /var/lib/forkmesh-mirror/gateway/mirror-refresh.json
```

## Initial refresh and service validation

Run as the dedicated service account:

```bash
/usr/bin/python3 /opt/forkmesh/tools/headless_mirror_refresh.py \
  --config /var/lib/forkmesh-mirror/gateway/mirror-refresh.json \
  refresh

/usr/bin/python3 /opt/forkmesh/tools/headless_mirror_refresh.py \
  --config /var/lib/forkmesh-mirror/gateway/mirror-refresh.json \
  check
```

A successful invocation emits only a small object such as:

```json
{"aliasCount":2,"event":"refresh_complete","ok":true}
```

It intentionally does not print paths, node names, repository names, HTTP
response bodies, Git output, or helper output. Subprocess stderr is discarded.
Detailed repository identities remain in the owner-only configuration, while
normal gateway request logs retain their existing generalized format.

Configure the gateway service to read the generated
`gatewayConfigPath`. A typical start boundary is:

```ini
ExecStartPre=/usr/bin/python3 /opt/forkmesh/tools/headless_mirror_refresh.py --config /var/lib/forkmesh-mirror/gateway/mirror-refresh.json check
ExecStart=/usr/bin/python3 /opt/forkmesh/tools/mirror_gateway.py --config /var/lib/forkmesh-mirror/gateway/mirror-gateway.json
```

Keep the gateway listener on loopback. Cloudflare Tunnel is the public TLS
boundary.

## Register after restart

After the gateway and Tunnel are running with the new generation:

```bash
/usr/bin/python3 /opt/forkmesh/tools/headless_mirror_refresh.py \
  --config /var/lib/forkmesh-mirror/gateway/mirror-refresh.json \
  register
```

Registration performs these operations in order:

1. Reload and validate the owner-local public identity.
2. Run strict Git fsck and require the active refs pin to equal the exact bare
   source.
3. Validate the active encrypted archive and run the gateway's own `--check`.
4. Ask the identity helper to sign the bounded endpoint-registration request
   and POST it to `/api/mirrors/https`.
5. Ask the helper to sign the normalized public catalog-v2/state record and
   POST it to `/api/repositories`.

No Cloudflare API token is needed for these signed Worker endpoints. Redirects
and ambient HTTP proxies are disabled, TLS certificate verification uses the
system trust store, response sizes are bounded, and remote response bodies are
never included in an error.

The endpoint must already resolve through a proxied Cloudflare Tunnel, its
signed manifest must be publicly reachable, and its node key must already be
bound to the registered node account. The Worker will reject registration
otherwise.

## Renew the signed health lease

Healthy endpoints are deliberately short-lived so a dead or disconnected
mirror falls out of routing. Each mirror therefore renews its own signed lease
every four minutes:

```bash
/usr/bin/python3 /opt/forkmesh-mirror/headless_mirror_refresh.py \
  --config /var/lib/forkmesh-mirror/gateway/mirror-refresh.json \
  renew
```

`renew` verifies the owner-local identity, active configuration, complete
encrypted-archive digest, exact source refs, and signatures before publishing.
It omits only the expensive full Git fsck and second gateway materialization;
the Worker still performs a fresh signed repository challenge and leaves the
endpoint fail-closed if the gateway or Tunnel is unavailable.

Install and enable `forkmesh-mirror-renew.service` and
`forkmesh-mirror-renew.timer` from `packaging/systemd/`. The timer runs after
boot and every four minutes. Its cadence, scheduling jitter, and bounded
five-minute service runtime remain below the ten-minute endpoint-registration
lease, so independently healthy mirrors stay eligible for round-robin
selection. A registration lease alone never authorizes repository bytes: the
Worker still requires a node-signed repository proof matching the canonical
refs, and caches a successful proof for no more than one minute before
revalidating it on later use.
Renewal and repository refresh share the same exclusive owner-only lock, so a
push refresh completes before a queued renewal can publish.

## SSH post-receive integration

The SSH forced-command gateway may write the bare source, but it must not
receive access to the node or age private files. Run Git and mirror publication
under separate accounts. After a successful receive, the Git gateway invokes a
fixed root-owned notifier with no arguments or push-derived environment. A
systemd path unit consumes its coalesced marker and runs the refresh tool as the
mirror account. The complete account, socket-broker, notification, signed
health, and service boundary is documented in
[`git-ssh-gateway.md`](git-ssh-gateway.md).

The post-receive flow is:

```text
successful receive-pack
  -> start serialized refresh service
  -> headless_mirror_refresh.py refresh
  -> restart mirror gateway
  -> within one bounded deadline, verify identity-bound signed health
     over both loopback and the configured public Cloudflare origin
  -> headless_mirror_refresh.py register
```

The health clients ignore ambient proxy settings and reject redirects, so a
different endpoint cannot satisfy either proof. The tracked example allows 180
seconds for the gateway and Cloudflare Tunnel to become ready. Registration
fails closed if either signed proof is unavailable or invalid; the independent
four-minute renewal timer remains the fallback for a later transient outage.

Do not place this sequence behind `sh -c` with user-controlled repository
arguments. Use fixed command arrays and a fixed config path. A service manager
should serialize the sequence; the tool also takes an owner-only advisory lock
so overlapping refresh/check/register/renew invocations cannot interleave.

Concurrent Git ref movement is detected by comparing the refs before sealing,
the helper's sealed-snapshot digest, and the refs after a second strict fsck.
If they differ, refresh fails and the next post-receive notification can retry.

## Last-good and recovery behavior

Encrypted archives are installed under a SHA-256 content-addressed filename.
The generated gateway JSON is the only active pointer and is replaced
atomically only after:

- strict fsck succeeds;
- the helper has atomically produced and authenticated an age file;
- the source refs remain stable;
- every configured owner alias references identical returned metadata; and
- `mirror_gateway.py --check` materializes and validates the staged generation.

The previous ciphertext is not overwritten or deleted. A failure before the
config rename leaves the old config/ciphertext pair active. A newly created but
unreferenced content-addressed archive may remain after a failed staged
validation; retain it until the incident is understood, then remove only files
that are not referenced by the active gateway configuration or an operator
backup. The tool deliberately performs no automatic destructive garbage
collection.

If `register` fails, do not reseal merely to retry publication. Confirm that
the gateway and Tunnel are live, then rerun `check` and `register`. If `check`
reports source drift, run the full refresh/restart/register sequence.
