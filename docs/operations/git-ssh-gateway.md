# Git SSH public-key authentication

ForkMesh supports familiar SSH clone and push URLs without pretending that a
Cloudflare HTTP Worker can terminate raw SSH. The implementation has two
separate planes:

1. The Worker stores each user's normalized **public** key in an encrypted D1
   envelope, indexes it by a keyed blind index, and enforces the existing
   repository owner and organization-team permissions.
2. An independently operated OpenSSH origin runs
   [`tools/ssh_gateway.py`](../../tools/ssh_gateway.py) as both
   `AuthorizedKeysCommand` and a forced command. It executes only
   `git-upload-pack` or `git-receive-pack` against an exact local bare-repository
   allowlist after the Worker authorizes that key, repository, and operation.

Private keys and passphrases stay in the user's SSH agent or filesystem.
ForkMesh never asks for, uploads, derives, or stores them.

## User flow

An authenticated user opens `/dashboard/settings/ssh-keys`, pastes one OpenSSH
public-key line, and gives it a private display label. Supported key types are:

- Ed25519 and FIDO/U2F-backed Ed25519
- ECDSA P-256, P-384, P-521, and FIDO/U2F-backed P-256
- RSA with a modulus from 3072 through 16384 bits

DSA, undersized RSA, authorized-keys options, multiple lines, malformed wire
blobs, and a key already attached (or previously revoked) are rejected. The
settings page lists labels, algorithms, SHA-256 fingerprints, creation dates,
and last-use times; it never returns the stored public-key line after
registration.

Pasting a public key reserves that key to the signed-in account; the form itself
cannot prove possession of the private half. OpenSSH proves possession on every
connection before the forced command can run. Use a fresh, account-specific key
instead of reusing a publicly listed key. A globally duplicate key returns the
same generic conflict whether it is active or revoked, so the API does not
disclose which account registered it; rotate to a fresh key and use the abuse
reporting path if a known key was reserved without permission.

When a gateway is configured, repository Code menus show:

```text
ssh://git@ssh.example.org/owner/repository.git
```

The same URL clones and pushes. Registering a key is not itself write
permission: every `git-receive-pack` is allowed only when the mapped account
owns the repository node or holds `write`, `maintain`, or `admin` permission
through a linked organization. Revocation takes effect on the next SSH
authentication/authorization check.

## Worker configuration

Create one random infrastructure bearer token:

```bash
python3 -c 'import secrets; print(secrets.token_urlsafe(48))'
```

Configure these Worker secrets/variables through `.env.production` or the
one-click deployment variables:

```dotenv
SSH_GATEWAY_HOST=ssh.example.org
SSH_GATEWAY_PORT=22
SSH_GATEWAY_TOKEN=<random token>
SSH_GATEWAY_REPOSITORIES=alice-node/widget=read-write,alice-node/docs=read-only
```

`SSH_GATEWAY_HOST` is a bare hostname or IP address, never a URL.
`SSH_GATEWAY_PORT` is 1–65535. `SSH_GATEWAY_TOKEN` must contain 32–512
base64url characters (`A-Z`, `a-z`, `0-9`, `_`, or `-`).
`SSH_GATEWAY_REPOSITORIES` is the exact Worker-side copy of the
gateway's local allowlist, using `owner/repo=read-write` or
`owner/repo=read-only` entries. ForkMesh publishes no SSH URL unless all four
values are valid, and publishes a URL only for an explicitly listed repository.
The bearer token authenticates only the gateway's HTTPS control-plane calls; it
is not a user key and grants no repository permission by itself.

The gateway calls:

- `POST /api/ssh/authorize`, action `lookup`, during public-key authentication.
- `POST /api/ssh/authorize`, action `authorize`, for the exact
  upload-pack/receive-pack request.

The endpoint is bearer-gated, fail-closed, and returns no repository data.
Public-key registration and revocation, plus successful or denied push
authorizations, produce metadata-only sensitive-action audit entries.

## Node-side configuration

Create `/etc/forkmesh/ssh-gateway.json` using
[`ssh-gateway-config.schema.json`](../ssh-gateway-config.schema.json):

```json
{
  "schemaVersion": 1,
  "apiOrigin": "https://forkmesh.example.org",
  "gatewayExecutable": "/opt/forkmesh/tools/ssh_gateway.py",
  "gatewayTokenFile": "/etc/forkmesh/ssh-gateway.token",
  "repositoryRoot": "/srv/forkmesh/git",
  "repositories": [
    {
      "owner": "alice-node",
      "name": "widget",
      "path": "alice-node/widget.git",
      "access": "read-write"
    }
  ]
}
```

The executable must be one canonical absolute path containing only letters,
digits, `_`, `.`, `/`, `+`, or `-`; forced commands run through the account's
shell, so whitespace, shell metacharacters, empty components, `.` and `..` are
rejected. A nondefault configuration path needs a root-owned executable wrapper
that sets `FORKMESH_SSH_GATEWAY_CONFIG` and then `exec`s the Python tool; do not
put command-line arguments in `gatewayExecutable`.

The JSON config must be a regular nonsymlink file owned by root or the executing
user and must not be group/world writable. The token file must be a regular
nonsymlink file readable only by its owner (`0600`). It contains exactly the
same `SSH_GATEWAY_TOKEN` configured at the Worker. Keep repository paths
relative to `repositoryRoot`; the gateway resolves them, rejects traversal and
symlink escapes, and verifies each target is a bare Git repository.

Example installation:

```bash
sudo install -d -m 0755 /etc/forkmesh /opt/forkmesh /srv/forkmesh/git
sudo cp -a /path/to/forkmesh /opt/forkmesh/source
sudo install -D -o root -g root -m 0755 \
  /opt/forkmesh/source/tools/ssh_gateway.py /opt/forkmesh/tools/ssh_gateway.py
sudo useradd --system --create-home --home-dir /var/lib/forkmesh-git --shell /bin/sh git
sudo install -o root -g root -m 0644 ssh-gateway.json /etc/forkmesh/ssh-gateway.json
sudo install -o git -g git -m 0600 ssh-gateway.token /etc/forkmesh/ssh-gateway.token
sudo chown -R git:git /srv/forkmesh/git
```

Install an sshd drop-in (syntax varies slightly by distribution):

```text
Match User git
    AuthenticationMethods publickey
    PubkeyAuthentication yes
    PasswordAuthentication no
    KbdInteractiveAuthentication no
    AuthorizedKeysFile none
    AuthorizedKeysCommand /opt/forkmesh/tools/ssh_gateway.py --config /etc/forkmesh/ssh-gateway.json authorized-key --key-type %t --key-blob %k
    AuthorizedKeysCommandUser git
    PermitTTY no
    AllowTcpForwarding no
    X11Forwarding no
    PermitTunnel no
    GatewayPorts no
```

The emitted authorized-key line also carries OpenSSH's `restrict` option and a
forced `serve --key-id …` command. The helper parses `SSH_ORIGINAL_COMMAND`
without a shell, accepts one `<owner>/<repository>.git` path, asks the Worker
for authorization, resolves the Worker's canonical namespace through the local
allowlist, strips ambient credential environment variables, and `exec`s Git
with an argument array.

Validate before reloading sshd:

```bash
sudo -u git /opt/forkmesh/tools/ssh_gateway.py \
  --config /etc/forkmesh/ssh-gateway.json check
sudo -u git /opt/forkmesh/tools/ssh_gateway.py \
  --config /etc/forkmesh/ssh-gateway.json worker-allowlist
sudo sshd -t
sudo systemctl reload sshd
```

Copy the single `worker-allowlist` output line into
`SSH_GATEWAY_REPOSITORIES`, then run `./deploy.sh secrets`. Regenerate and
repush it whenever the local gateway allowlist changes. This deliberate
duplication prevents the Web UI from advertising a repository that only exists
in the catalog and is not configured on the SSH host. The Worker also checks
the entry and its read-only/read-write mode again during authorization; the
gateway independently enforces its own local copy.

To disable SSH publication without relying on secret deletion, set
`SSH_GATEWAY_REPOSITORIES=disabled`, run `./deploy.sh secrets`, and verify the
catalog no longer returns `sshUrl`. The invalid allowlist fails closed. You may
then delete the four SSH secrets in Cloudflare. Merely removing an optional line
from `.env.production` does not erase an already-pushed Cloudflare secret.

For private repositories, operate the SSH origin on an owner-authorized machine
that is permitted to materialize that repository. Do not point an untrusted
mirror operator at private plaintext; ordinary private mirror replicas retain
their owner-sealed encrypted transport.

## DNS and Cloudflare transport

An ordinary orange-cloud Cloudflare DNS proxy does not transparently proxy raw
SSH on port 22. Use one of these technically valid arrangements:

- A DNS-only `A`/`AAAA` record to the hardened SSH origin.
- Cloudflare Spectrum (or an equivalent TCP proxy) in front of the SSH origin.
- Cloudflare Access/Tunnel with a required client-side `cloudflared`
  `ProxyCommand`; this is not a universally usable plain SSH URL and therefore
  should not be advertised as one unless every intended client is configured.

Keep the HTTPS Worker and SSH origin on separate hostnames. Apply network rate
limits, host patching, process/resource limits, and repository backups at the
SSH origin. A Cloudflare DNS record alone is discovery, not an SSH security
boundary.

## Failure and privacy behavior

- Unknown, revoked, malformed, and wrong-account keys all fail authentication
  without revealing the owning account.
- Missing repositories and insufficient permissions both fail closed.
- The gateway never invokes an interactive shell, accepts arbitrary commands,
  or constructs a shell command from a repository path.
- Request paths, repository bytes, client IPs, SSH key material, and the bearer
  token are not written by the helper.
- Account rename retargets the key's blind-index account link; gateway lookup
  resolves the current account name rather than trusting registration-time
  encrypted metadata.
- Account deletion removes its SSH public-key rows.
