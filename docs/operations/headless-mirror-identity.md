# Headless mirror identity

`tools/headless_mirror_identity.py` provides the local cryptographic boundary
for a non-GUI mirror host. It is intended to run as the same dedicated Unix
account as `mirror_gateway.py`.

The helper:

- keeps one Ed25519 node identity and one native age identity in an owner-only
  directory;
- signs only the bounded ForkMesh manifest, health, reclaim, registration, and
  public catalog-v2/state protocols;
- verifies routing capabilities only with the configured router public key;
- snapshots a bare repository through Git and atomically seals exactly one
  `repository.git` tree with age; and
- authenticates and validates an age-encrypted tar before materializing it.

It has no command-line option for a private key, seed, token, password, or
credential. Requests use bounded JSON on standard input. Responses contain only
public keys, signatures, key references, hashes, sizes, and public records.

## Requirements

- Python 3.12 or newer
- `cryptography`
- the official `age` and `age-keygen` programs
- Git

Run the helper and gateway as a dedicated, unprivileged service account. Put
the identity directory and encrypted archives on storage owned by that account.
The identity directory and archive directory must be mode `0700`; identity
files are created mode `0600`.

```bash
install -d -m 0700 /var/lib/forkmesh/identity
install -d -m 0700 /var/lib/forkmesh/archives
```

The paths above are examples. They must be absolute. Do not put the identity
directory in the checkout, a shared Git repository, a container image, or a
configuration-management response.

## Initialize

All values in the initialization request are public. The allowed origin pin
prevents a local bootstrap process from asking the node key to sign a manifest
or registration for another hostname.

```bash
printf '%s\n' '{
  "schemaVersion": 1,
  "type": "forkmesh.headless-mirror-identity-init",
  "nodeName": "mirror-two",
  "routerPublicKey": "<MIRROR_ROUTER_PUBLIC_KEY>",
  "allowedOrigins": ["https://mirror2.forkmesh.com"]
}' | python3 tools/headless_mirror_identity.py \
  --state-dir /var/lib/forkmesh/identity init
```

Initialization is idempotent. If a process interruption leaves a valid
`node-ed25519.pem` or `age-identity.txt` before `public.json` is committed,
rerun the same request. The helper reuses the valid identity; deleting those
files would unnecessarily rotate the node.

To inspect the public half:

```bash
python3 tools/headless_mirror_identity.py \
  --state-dir /var/lib/forkmesh/identity public-info
```

`configure` changes only the public router-key and origin pins. It never rotates
either private identity:

```json
{
  "schemaVersion": 1,
  "type": "forkmesh.headless-mirror-identity-configure",
  "routerPublicKey": "<MIRROR_ROUTER_PUBLIC_KEY>",
  "allowedOrigins": ["https://mirror2.forkmesh.com"]
}
```

## Gateway command arrays

Use direct JSON command arrays. Do not place them behind a shell:

```json
{
  "requestVerifierCommand": [
    "/usr/bin/python3",
    "/opt/forkmesh/tools/headless_mirror_identity.py",
    "--state-dir",
    "/var/lib/forkmesh/identity",
    "capability-verify"
  ],
  "healthSignerCommand": [
    "/usr/bin/python3",
    "/opt/forkmesh/tools/headless_mirror_identity.py",
    "--state-dir",
    "/var/lib/forkmesh/identity",
    "health-sign"
  ]
}
```

Each enabled public repository uses the same helper for its encrypted archive:

```json
{
  "encryptedArchive": {
    "scheme": "age-encrypted-tar-v1",
    "ciphertextPath": "/var/lib/forkmesh/archives/forkmesh.age",
    "ciphertextSha256": "<seal response ciphertextSha256>",
    "keyReference": "<seal response keyReference>",
    "materializeCommand": [
      "/usr/bin/python3",
      "/opt/forkmesh/tools/headless_mirror_identity.py",
      "--state-dir",
      "/var/lib/forkmesh/identity",
      "materialize"
    ]
  },
  "integrity": {
    "expectedRefsSha256": "<seal response expectedRefsSha256>"
  }
}
```

The gateway passes the ciphertext path, digest, opaque key reference, and an
empty owner-only temporary destination on stdin. The helper returns only:

```json
{"ok": true, "repositoryPath": "repository.git"}
```

It rejects a non-empty destination and rejects absolute/traversal names,
duplicate paths, symbolic links, hard links, devices, FIFOs, sockets, and
unknown tar entry types before extraction. Files are then created through
directory file descriptors with no-follow and exclusive-create flags.

For a Tunnel endpoint, give `cloudflare_tunnel_bootstrap.py` this helper as its
manifest signer command and supply `nodePublicKey` from `public-info` as the
mirror public key:

```text
/usr/bin/python3 /opt/forkmesh/tools/headless_mirror_identity.py --state-dir /var/lib/forkmesh/identity manifest-sign
```

The signer accepts the existing strict Cloudflare Tunnel manifest. It also
understands the legacy Worker-route manifest and a secret-free
`cloudflare-proxied-origin` manifest, but the deployed Worker and manifest
schema must support the selected variant. Tunnel is the production default.

For a service manager, set a restrictive umask, a dedicated `User`, no
privilege escalation, a read-only application tree, and write access only to
the identity, encrypted archive, and ephemeral runtime directories. The
gateway itself must continue listening only on loopback behind Cloudflare
Tunnel or an equivalently restricted TLS proxy.

## Initial sync and post-receive reseal

`seal-repository` accepts paths over stdin so repository and archive paths do
not become helper arguments:

```json
{
  "schemaVersion": 1,
  "type": "forkmesh.repository-archive-seal",
  "sourceRepository": "/srv/git/forkmesh.git",
  "ciphertextPath": "/var/lib/forkmesh/archives/forkmesh.age"
}
```

The helper verifies that the source is bare, clones a fresh owner-only mirror
snapshot with local hard-linking disabled, packages exactly
`repository.git`, streams the tar to age, verifies the age header, and
atomically replaces the requested ciphertext. It returns public metadata:

```json
{
  "ok": true,
  "scheme": "age-encrypted-tar-v1",
  "ciphertextSha256": "<sha256>",
  "ciphertextBytes": 123,
  "keyReference": "forkmesh-headless-age:<opaque-public-id>",
  "expectedRefsSha256": "<canonical-heads-and-tags-sha256>"
}
```

An SSH post-receive orchestrator can run this operation, atomically update the
public gateway JSON with the returned hashes, run
`mirror_gateway.py --check`, and restart the gateway. Service restart policy is
intentionally external to the identity helper. Never restart against a new
ciphertext while retaining the old digest in the gateway configuration.

## Operator-only signatures

The following modes are for a local operator workflow. Do not expose them as
HTTP endpoints or include them in the gateway configuration.

- `sign-reclaim` accepts
  `forkmesh.account-reclaim-signing` with `nodeName` and a six-digit
  `linkCode`; it generates the current timestamp and returns the exact public
  `/api/accounts/reclaim-node` body.
- `sign-endpoint-registration` accepts
  `forkmesh.https-endpoint-registration-signing` with `node` and `baseUrl`;
  it generates the current timestamp and returns the exact
  `/api/mirrors/https` body.
- `sign-catalog-v2` accepts
  `forkmesh.catalog-v2-publication-signing` with a public `record`. The record
  must include `owner`, `name`, `visibility: "public"`, and the
  `expectedRefsSha256` value as `stateHash`. It returns the normalized public
  record with `stateSig`, `catalogSigVersion: 2`, and `catalogSig`, ready for
  `/api/repositories`.

These modes construct the canonical messages internally; there is no
arbitrary-message signing mode.

## Key handling and rotation

Back up the identity directory through an encrypted, owner-controlled process.
ForkMesh does not upload it and cannot recover it. Filesystem permissions
protect the stopped host from other local accounts, not from root or a
compromised service account; use an encrypted volume or hardware-backed signer
when that threat is in scope.

To rotate the routing key, update the Worker secret first as a coordinated
maintenance action, then use `configure` on each mirror. To rotate the node or
age identity, provision a new state directory, rebind/reclaim the node identity,
reseal every archive, publish new manifests and catalog state, validate health,
and retire the old directory according to the operator retention policy.
