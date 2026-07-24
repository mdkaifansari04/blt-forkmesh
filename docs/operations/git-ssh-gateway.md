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
disclose which account registered it; rotate to a fresh key and contact the
account-support channel if a known key was reserved without permission.

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

The production boundary uses four distinct non-root identities:

- `forkmesh-ssh-auth` owns the Worker bearer and runs the local authorization
  broker. It cannot read repositories or mirror identity material.
- `forkmesh-ssh-lookup` is used only by `AuthorizedKeysCommand`. It may submit
  `lookup` requests but cannot authorize repositories.
- `git` runs the forced Git service. It may submit `authorize`
  requests and write the selected bare repository, but cannot read the bearer
  or the mirror's node/age private material.
- `forkmesh-mirror` reads the bare repository and owns all encryption and node
  signing material. It is not an SSH login account.

The broker in
[`tools/ssh_authorization_broker.py`](../../tools/ssh_authorization_broker.py)
creates a group-readable Unix socket in a directory that neither SSH-facing
account can write. It checks Linux `SO_PEERCRED` on every connection, permits
one role-specific bounded request, adds a fresh request ID, disables redirects
and ambient HTTP proxies, and validates the exact Worker response. The Git
gateway checks the broker's kernel-reported uid in the other direction. Thus a
Git process or repository hook cannot read or inherit the infrastructure
bearer.

Create `/etc/forkmesh/ssh-gateway.json` from the tracked
[`ssh-gateway.json.example`](../../packaging/ssh/ssh-gateway.json.example) and
validate it with
[`ssh-gateway-config.schema.json`](../ssh-gateway-config.schema.json):

```json
{
  "schemaVersion": 1,
  "authorizationSocket": "/run/forkmesh-ssh-auth/authorize.sock",
  "authorizationBrokerUser": "forkmesh-ssh-auth",
  "gatewayExecutable": "/opt/forkmesh-mirror/ssh-gateway-entrypoint",
  "refreshNotifier": "/opt/forkmesh-mirror/ssh-refresh-notify",
  "repositoryRoot": "/srv/forkmesh-git",
  "repositories": [
    {
      "owner": "mirror2",
      "name": "forkmesh",
      "path": "mirror2/forkmesh.git",
      "access": "read-write"
    }
  ]
}
```

Create the broker config from
[`ssh-authorization-broker.json.example`](../../packaging/ssh/ssh-authorization-broker.json.example).
Only its dedicated account may read `/etc/forkmesh/ssh-gateway.token` (`0600`).
The token is never present in JSON, an environment variable, a Git process, or
an SSH wrapper. Configs, programs, wrappers, and every executable parent
directory are root-owned and not writable by the Git account.

Use a root-owned `0750` repository root and a dedicated repository-sharing
group. The bare repository itself can be owned by `forkmesh-mirror` with that
group and mode `2770`; add only `git` and `forkmesh-mirror` to the
group. Do not recursively make a private repository world-readable.

Install the tracked broker service, fixed wrappers, and
[`99-forkmesh-git.conf`](../../packaging/ssh/99-forkmesh-git.conf). The Match
block affects only `git`, retains the host's existing administrative
SSH policy, denies passwords, forwarding, TTYs, user rc files, and trusted-user
CA authentication, and uses the separate lookup account. A dedicated
SSH-only daemon should additionally use global `PermitRootLogin no`,
`PermitUserEnvironment no`, no broad `AcceptEnv`, and an `AllowUsers` list.
Those global settings must not be copied into a shared administrative daemon
without an explicit access migration.

The emitted authorized-key line also carries OpenSSH's `restrict` option and a
forced `serve --key-id …` command. The helper parses `SSH_ORIGINAL_COMMAND`
without a shell, accepts one `<owner>/<repository>.git` path, asks the Worker
for authorization, resolves the Worker's canonical namespace through the local
allowlist, and runs Git with a fixed argument array and a clean environment.
The root-owned entrypoint rebuilds the pre-Python environment from scratch,
carrying forward only OpenSSH's required `SSH_ORIGINAL_COMMAND` and the optional
`GIT_PROTOCOL` hint; the Python gateway then accepts only Git protocol versions
1 or 2 and an exact upload-pack/receive-pack command.
Command-line configuration disables repository hooks, pack-object hooks,
alternate-ref commands, fsmonitor programs, credential helpers, and the `ext`
transport. After a successful receive only, the gateway invokes the fixed
root-owned notifier with no arguments or inherited push data. A failed
notification never rewrites an already-committed push as failed.

The notifier atomically coalesces pushes into one marker. The tracked systemd
path/service then runs refresh as `forkmesh-mirror`, restarts only
`forkmesh-mirror.service`, and verifies identity-bound signed health over both
loopback and the gateway configuration's public Cloudflare origin using the
pinned Ed25519 node key before registering the active generation. Both checks
share one bounded deadline, ignore ambient proxies, and reject redirects. The
flow fails closed if either proof is unavailable; the independent renewal timer
can republish after a later transient outage. It never derives a command, path,
unit, or argument from a pushed ref, push option, repository content, or SSH
environment. See
[`ssh-post-receive-refresh.schema.json`](../ssh-post-receive-refresh.schema.json).

Validate before reloading sshd:

```bash
sudo systemctl start forkmesh-ssh-authorization-broker.service
sudo -u git /opt/forkmesh-mirror/ssh-gateway-entrypoint check
sudo -u git \
  /opt/forkmesh-mirror/ssh-gateway-entrypoint worker-allowlist
sudo sshd -t
sudo sshd -T -C user=git,host=localhost,addr=127.0.0.1
sudo systemctl reload sshd
```

Keep an existing administrative session open, confirm its effective
configuration is unchanged, and open a second administrative connection before
closing the first. Never install the Match block until `sshd -t` succeeds.

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
- Missing, locally absent, read-only, and unauthorized repositories produce the
  same generic forced-command denial.
- The gateway never invokes an interactive shell, accepts arbitrary commands,
  or constructs a shell command from a repository path.
- Request paths, repository bytes, client IPs, SSH key material, and the bearer
  token are not written by the helper.
- The broker authenticates before schema or database work, returns no account
  principal, and binds each success to a fresh request ID plus the exact key,
  operation, and canonical repository response.
- Account rename retargets the key's blind-index account link; gateway lookup
  resolves the current account name rather than trusting registration-time
  encrypted metadata.
- Account deletion removes its SSH public-key rows.
