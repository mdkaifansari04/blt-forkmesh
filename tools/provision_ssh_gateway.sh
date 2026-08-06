#!/usr/bin/env bash
# Install the public Git SSH gateway (read-only) on one mirror host.
#
# ssh.forkmesh.com used to be a single machine (mirror2). When that node was
# retired the published ssh:// clone URL died with it, because nothing else
# in the fleet ran the gateway. This script makes any mirror able to serve
# that URL so the name can round-robin across several of them —
# docs/operations/git-ssh-gateway.md describes the security model it
# implements.
#
#   tools/provision_ssh_gateway.sh <host-ip> [<repo-owner>/<repo-name>]
#
# Idempotent: safe to re-run, and re-running is how a host picks up a new
# gateway script or token. Read-only by design — pushes keep flowing through
# the owner node's own authenticated path, so this gateway never needs write
# permission, a storage quota, or the receive-pack code path.
#
# The sshd change is additive (one Match block) and is applied only after
# `sshd -t` validates the merged configuration; a bad config aborts before
# the reload, leaving the running sshd untouched.
set -euo pipefail

HOST="${1:?usage: provision_ssh_gateway.sh <host-ip> [owner/name]}"
REPO_SPEC="${2:-forkmesh/forkmesh}"
REPO_OWNER="${REPO_SPEC%%/*}"
REPO_NAME="${REPO_SPEC##*/}"

REPO_ROOT_DEFAULT="/var/lib/forkmesh/.local/share/ForkMesh/ForkMesh/mirrors"
REPO_DIR_DEFAULT="forkmesh-forkmesh.git"
REPO_ROOT="${FORKMESH_GATEWAY_REPO_ROOT:-$REPO_ROOT_DEFAULT}"
REPO_DIR="${FORKMESH_GATEWAY_REPO_DIR:-$REPO_DIR_DEFAULT}"

cd "$(dirname "$0")/.."
ENV_FILE="cloudflare_worker/.env.production"
SSH_KEY="${FORKMESH_FLEET_SSH_KEY:-$HOME/.local/share/ForkMesh/ForkMesh/ssh/vultr_mirror_ed25519}"

token="$(sed -n 's/^SSH_GATEWAY_TOKEN=//p' "$ENV_FILE" | tr -d '\r' | head -n1)"
api_origin="$(sed -n 's/^PUBLIC_BASE_URL = "//p' cloudflare_worker/wrangler.toml |
    tr -d '"' | head -n1)"
api_origin="${api_origin:-https://forkmesh.com}"
if [ -z "$token" ]; then
    echo "ERROR: SSH_GATEWAY_TOKEN missing from $ENV_FILE." >&2
    exit 1
fi

echo "== provisioning gateway on $HOST for $REPO_OWNER/$REPO_NAME"

SSH_OPTS=(-o BatchMode=yes -o StrictHostKeyChecking=accept-new -i "$SSH_KEY")

# Stage the payload with scp rather than piping it into the remote shell:
# `bash -s` reads the script itself from stdin, so a piped payload never
# reaches the script (it silently installs nothing).
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
umask 077
printf '%s\n' "$token" > "$stage/ssh-gateway.token"
cp tools/ssh_gateway.py "$stage/ssh_gateway.py"
cp tools/ssh_authorization_broker.py "$stage/broker.py"
cp packaging/ssh/ssh-gateway-entrypoint "$stage/entrypoint"
cp packaging/ssh/ssh-refresh-notify "$stage/notifier"

ssh "${SSH_OPTS[@]}" "root@$HOST" \
    'rm -rf /root/.forkmesh-gateway-stage && mkdir -m 0700 -p /root/.forkmesh-gateway-stage'
scp "${SSH_OPTS[@]}" -q "$stage"/* "root@$HOST:/root/.forkmesh-gateway-stage/"

ssh "${SSH_OPTS[@]}" "root@$HOST" \
    "REPO_OWNER='$REPO_OWNER' REPO_NAME='$REPO_NAME' REPO_ROOT='$REPO_ROOT' \
     REPO_DIR='$REPO_DIR' API_ORIGIN='$api_origin' bash -s" <<'REMOTE'
set -euo pipefail
umask 077

staging=/root/.forkmesh-gateway-stage
trap 'rm -rf "$staging"' EXIT
for required in ssh_gateway.py broker.py entrypoint notifier ssh-gateway.token; do
    [ -s "$staging/$required" ] || { echo "ERROR: staged $required missing." >&2; exit 1; }
done

# --- accounts -----------------------------------------------------------
getent group forkmesh-ssh-gateway >/dev/null || groupadd --system forkmesh-ssh-gateway
for account in git forkmesh-ssh-auth forkmesh-ssh-lookup; do
    getent passwd "$account" >/dev/null || \
        useradd --system --create-home --shell /bin/sh "$account"
done
usermod -aG forkmesh-ssh-gateway git
usermod -aG forkmesh-ssh-gateway forkmesh-ssh-lookup

# The gateway reads the bare mirror through the repository-sharing group; the
# mirror node account keeps ownership and all signing material.
repo_path="$REPO_ROOT/$REPO_DIR"
if [ ! -d "$repo_path" ]; then
    echo "ERROR: bare repository $repo_path not found on this host." >&2
    exit 1
fi
mirror_owner="$(stat -c '%U' "$repo_path")"
usermod -aG "$(stat -c '%G' "$repo_path")" git || true
# Traverse-only on the parents, read+execute inside the bare repository.
chmod o+x /var/lib/forkmesh /var/lib/forkmesh/.local \
    /var/lib/forkmesh/.local/share /var/lib/forkmesh/.local/share/ForkMesh \
    /var/lib/forkmesh/.local/share/ForkMesh/ForkMesh "$REPO_ROOT" 2>/dev/null || true
chmod -R o+rX "$repo_path" 2>/dev/null || true

# --- programs -----------------------------------------------------------
install -d -m 0755 -o root -g root /opt/forkmesh-mirror
install -m 0755 -o root -g root "$staging/ssh_gateway.py" /opt/forkmesh-mirror/ssh_gateway.py
install -m 0755 -o root -g root "$staging/broker.py" /opt/forkmesh-mirror/ssh_authorization_broker.py
install -m 0755 -o root -g root "$staging/entrypoint" /opt/forkmesh-mirror/ssh-gateway-entrypoint
# Required by the gateway config schema. Read-only hosts never run
# receive-pack, so this notifier is validated but never invoked.
install -m 0755 -o root -g root "$staging/notifier" /opt/forkmesh-mirror/ssh-refresh-notify

# --- configuration ------------------------------------------------------
install -d -m 0755 -o root -g root /etc/forkmesh
# private=True in the broker: 0600, and owned by the broker account itself
install -m 0600 -o forkmesh-ssh-auth -g forkmesh-ssh-auth "$staging/ssh-gateway.token" /etc/forkmesh/ssh-gateway.token

cat > /etc/forkmesh/ssh-gateway.json <<JSON
{
  "schemaVersion": 1,
  "authorizationSocket": "/run/forkmesh-ssh-auth/authorize.sock",
  "authorizationBrokerUser": "forkmesh-ssh-auth",
  "gatewayExecutable": "/opt/forkmesh-mirror/ssh-gateway-entrypoint",
  "refreshNotifier": "/opt/forkmesh-mirror/ssh-refresh-notify",
  "repositoryRoot": "$REPO_ROOT",
  "limits": {
    "capacityDirectory": "/run/forkmesh-ssh-gateway",
    "maxConcurrentSessions": 16,
    "reservedReceiveSessions": 1,
    "maxConcurrentProcesses": 12,
    "reservedReceiveProcesses": 1,
    "uploadPackDeadlineSeconds": 900,
    "receivePackDeadlineSeconds": 600,
    "terminationGraceSeconds": 3,
    "receiveMaxInputBytes": 268435456,
    "defaultRepositoryMaxBytes": 17179869184,
    "storageScanDeadlineSeconds": 10,
    "cleanupDeadlineSeconds": 5,
    "maxPackThreads": 2
  },
  "repositories": [
    {
      "owner": "$REPO_OWNER",
      "name": "$REPO_NAME",
      "path": "$REPO_DIR",
      "access": "read-only",
      "maxStorageBytes": 17179869184
    }
  ]
}
JSON
chmod 0644 /etc/forkmesh/ssh-gateway.json

cat > /etc/forkmesh/ssh-authorization-broker.json <<JSON
{
  "schemaVersion": 1,
  "type": "forkmesh.ssh-authorization-broker",
  "apiOrigin": "$API_ORIGIN",
  "gatewayTokenFile": "/etc/forkmesh/ssh-gateway.token",
  "socketPath": "/run/forkmesh-ssh-auth/authorize.sock",
  "socketGroup": "forkmesh-ssh-gateway",
  "lookupUser": "forkmesh-ssh-lookup",
  "gitUser": "git"
}
JSON
chmod 0644 /etc/forkmesh/ssh-authorization-broker.json

cat > /etc/tmpfiles.d/forkmesh-ssh-gateway.conf <<'CONF'
d /run/forkmesh-ssh-auth 0750 forkmesh-ssh-auth forkmesh-ssh-gateway -
d /run/forkmesh-ssh-gateway 0700 git git -
CONF
systemd-tmpfiles --create /etc/tmpfiles.d/forkmesh-ssh-gateway.conf

cat > /etc/systemd/system/forkmesh-ssh-auth.service <<'UNIT'
[Unit]
Description=ForkMesh SSH authorization broker
After=network-online.target

[Service]
Type=simple
User=forkmesh-ssh-auth
Group=forkmesh-ssh-gateway
RuntimeDirectory=forkmesh-ssh-auth
RuntimeDirectoryMode=0750
ExecStart=/usr/bin/python3 -I /opt/forkmesh-mirror/ssh_authorization_broker.py \
  --config /etc/forkmesh/ssh-authorization-broker.json
Restart=always
RestartSec=2
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now forkmesh-ssh-auth.service

# --- sshd ---------------------------------------------------------------
# Additive Match block, applied only after sshd -t validates it.
conf_dir=/etc/ssh/sshd_config.d
install -d -m 0755 "$conf_dir"
cat > "$conf_dir/60-forkmesh-git.conf" <<'SSHD'
Match User git
    AuthorizedKeysCommand /opt/forkmesh-mirror/ssh-gateway-entrypoint authorized-key --key-type %t --key-blob %k
    AuthorizedKeysCommandUser forkmesh-ssh-lookup
    AuthorizedKeysFile none
    PermitTTY no
    X11Forwarding no
    AllowTcpForwarding no
    AllowAgentForwarding no
    PermitTunnel no
SSHD
if ! grep -q '^Include /etc/ssh/sshd_config.d/\*.conf' /etc/ssh/sshd_config; then
    printf '\nInclude /etc/ssh/sshd_config.d/*.conf\n' >> /etc/ssh/sshd_config
fi
if ! sshd -t; then
    rm -f "$conf_dir/60-forkmesh-git.conf"
    echo "ERROR: sshd rejected the gateway configuration; reverted, sshd untouched." >&2
    exit 1
fi
systemctl reload ssh 2>/dev/null || systemctl reload sshd

printf 'gateway ready on %s: %s/%s -> %s (read-only)\n' \
    "$(hostname)" "$REPO_OWNER" "$REPO_NAME" "$repo_path"
/usr/bin/python3 -I /opt/forkmesh-mirror/ssh_gateway.py \
    --config /etc/forkmesh/ssh-gateway.json check || true
REMOTE
