#!/usr/bin/env bash
# Deploy the ForkMesh website and relay Worker to Cloudflare.
#
# The Worker serves the static site from public/ (Cloudflare Static Assets)
# and hosts the API/relay/catalog routes, so a single deploy ships both.
#
#   ./deploy.sh          deploy to production (+ push secrets from .env.production)
#   ./deploy.sh secrets  (re)push only the .env.production secrets, no redeploy
#   ./deploy.sh dev      run the Worker locally instead of deploying
#   ./deploy.sh dry-run   build and validate without uploading
set -euo pipefail
cd "$(dirname "$0")"

# pywrangler is run through uv's ephemeral tool runner so there is no global
# install or node_modules to manage.
pywrangler() {
    if command -v uvx >/dev/null 2>&1; then
        uvx --from workers-py pywrangler "$@"
    elif command -v pywrangler >/dev/null 2>&1; then
        command pywrangler "$@"
    else
        echo "error: need 'uvx' (from uv) or 'pywrangler' on PATH." >&2
        echo "       install uv: https://docs.astral.sh/uv/getting-started/" >&2
        exit 1
    fi
}

# Production Worker config (admin creds, etc.) lives in .env.production —
# gitignored so secrets stay out of the committed wrangler.toml.
#
# IMPORTANT: these are pushed as Worker *secrets*, not plaintext vars. Plaintext
# vars (wrangler.toml [vars] and `--var`) are the COMPLETE set on every deploy, so
# a deploy that doesn't re-pass them ERASES them — that's why the admin dashboard
# vars kept vanishing. Secrets persist across deploys (a deploy never deletes a
# secret that isn't in the config), so the admin URL keeps working. The Worker
# reads them the same way (env.ADMIN_PATH, etc.).
ENV_FILE=".env.production"

# Inline --var args are built too, but used ONLY for local `dev`: workerd's dev
# server has no secret store unless you keep a .dev.vars file, so passing them
# inline keeps `./deploy.sh dev` working.
VAR_ARGS=()
if [ -f "$ENV_FILE" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac   # skip blanks/comments
        case "$line" in *=*) ;; *) continue ;; esac # skip lines without KEY=VALUE
        key="${line%%=*}"
        value="${line#*=}"
        [ -z "$key" ] && continue
        # CLOUDFLARE_* keys configure wrangler itself (which account/token to use),
        # not the Worker. Export them so every pywrangler call targets the right
        # account — this is how the maintainer pins the account without committing
        # the ID to this open-source wrangler.toml — and never ship them as Worker
        # vars or secrets.
        case "$key" in
            CLOUDFLARE_*) export "$key=$value"; continue ;;
        esac
        VAR_ARGS+=(--var "${key}:${value}")          # .env uses =, wrangler uses :
    done < "$ENV_FILE"
fi

# Push every KEY=VALUE in .env.production to the deployed Worker as a SECRET.
# Idempotent (re-running updates values) and persists across redeploys. Requires
# the Worker to already exist, so run it after `pywrangler deploy`.
push_secrets() {
    if [ ! -f "$ENV_FILE" ]; then
        echo "note: $ENV_FILE not found — no secrets to push." >&2
        return 0
    fi
    local count=0
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac
        case "$line" in *=*) ;; *) continue ;; esac
        local key="${line%%=*}"
        local value="${line#*=}"
        [ -z "$key" ] && continue
        # CLOUDFLARE_* are wrangler config (exported above), not Worker secrets.
        case "$key" in CLOUDFLARE_*) continue ;; esac
        echo "  secret: $key"
        printf '%s' "$value" | pywrangler secret put "$key"
        count=$((count + 1))
    done < "$ENV_FILE"
    echo "Pushed $count secret(s) from $ENV_FILE."
}

case "${1:-deploy}" in
    deploy)
        echo "Deploying ForkMesh website + relay to Cloudflare..."
        pywrangler deploy
        # Secrets are set after the Worker exists; unlike plaintext vars they
        # survive this and future deploys, so the admin dashboard keeps working.
        push_secrets
        echo "Done. Live at https://forkmesh.com (and any custom domain)."
        ;;
    secrets)
        # Re-push just the .env.production secrets, no full redeploy.
        push_secrets
        ;;
    dev)
        pywrangler dev ${VAR_ARGS[@]+"${VAR_ARGS[@]}"}
        ;;
    dry-run)
        pywrangler deploy --dry-run
        ;;
    *)
        echo "Usage: $0 [deploy|secrets|dev|dry-run]" >&2
        exit 2
        ;;
esac
