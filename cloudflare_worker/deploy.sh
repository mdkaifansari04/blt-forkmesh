#!/usr/bin/env bash
# Deploy the ForkMesh website and relay Worker to Cloudflare.
#
# The Worker serves the static site from public/ (Cloudflare Static Assets)
# and hosts the API/relay/catalog routes, so a single deploy ships both.
#
#   ./deploy.sh          deploy to production
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

# Production Worker vars (admin creds, etc.) live in .env.production — gitignored
# so secrets stay out of the committed wrangler.toml. Pass each as `--var
# KEY:VALUE`, which overrides/supplements [vars]. (Plain `pywrangler deploy`
# only auto-loads .env / .env.local, not .env.production, so we do it here.)
ENV_FILE=".env.production"
VAR_ARGS=()
if [ -f "$ENV_FILE" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac   # skip blanks/comments
        case "$line" in *=*) ;; *) continue ;; esac # skip lines without KEY=VALUE
        key="${line%%=*}"
        value="${line#*=}"
        [ -z "$key" ] && continue
        VAR_ARGS+=(--var "${key}:${value}")          # .env uses =, wrangler uses :
    done < "$ENV_FILE"
    echo "Loaded ${#VAR_ARGS[@]} var override(s) from $ENV_FILE."
else
    echo "note: $ENV_FILE not found — deploying without its --var overrides." >&2
fi

case "${1:-deploy}" in
    deploy)
        echo "Deploying ForkMesh website + relay to Cloudflare..."
        pywrangler deploy ${VAR_ARGS[@]+"${VAR_ARGS[@]}"}
        echo "Done. Live at https://forkmesh.com (and any custom domain)."
        ;;
    dev)
        pywrangler dev ${VAR_ARGS[@]+"${VAR_ARGS[@]}"}
        ;;
    dry-run)
        pywrangler deploy --dry-run ${VAR_ARGS[@]+"${VAR_ARGS[@]}"}
        ;;
    *)
        echo "Usage: $0 [deploy|dev|dry-run]" >&2
        exit 2
        ;;
esac
