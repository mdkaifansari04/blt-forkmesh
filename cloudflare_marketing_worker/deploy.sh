#!/usr/bin/env bash
# Deploy the ForkMesh marketing Worker (the forkmesh.com/ landing page, /pricing,
# and the /blog index plus posts) to Cloudflare.
#
# This Worker owns the marketing routes forkmesh.com/, /pricing, /blog, and
# /blog/* — every other path stays on the relay Worker in ../cloudflare_worker
# (deployed by its own deploy.sh). The documents are copied from
# ../cloudflare_worker/public/ by the wrangler [build] step, so there is a single
# source of truth for each page.
#
#   ./deploy.sh          deploy to production
#   ./deploy.sh dev      run the Worker locally instead of deploying
#   ./deploy.sh dry-run  build and validate without uploading
#
# A production deploy VERIFIES the live origin: forkmesh.com/ must answer with
# the x-forkmesh-worker: marketing header this Worker stamps on every landing
# response. If it never appears, the route did not take effect (wrong account,
# or the zone still sends / to the relay Worker) and the deploy FAILS loudly.
# Override the verified origin with DEPLOY_VERIFY_URL (default https://forkmesh.com).
set -euo pipefail
cd "$(dirname "$0")"

# Cloudflare credentials live in the relay Worker's gitignored .env.production —
# both Workers deploy to the same account/zone, so share it rather than keeping
# a second secrets file in sync. Only CLOUDFLARE_* keys are read; everything
# else in that file is relay-Worker secrets and never touches this Worker.
ENV_FILE="../cloudflare_worker/.env.production"

# Same pinned wrangler as the relay's pywrangler pipeline (see pywrangler.sh).
WRANGLER_NPM_SPEC="${WRANGLER_NPM_SPEC:-wrangler@4.42.1}"

trim() {
    local s="$1"
    s="${s%$'\r'}"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

if [ -f "$ENV_FILE" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac
        case "$line" in *=*) ;; *) continue ;; esac
        key="$(trim "${line%%=*}")"
        value="$(trim "${line#*=}")"
        case "$key" in
            CLOUDFLARE_*) [ -n "$value" ] && export "$key=$value" ;;
        esac
    done < "$ENV_FILE"
fi

# Mirror the relay deploy's token-alias adoption: if the token was saved under a
# common near-miss name, promote it rather than failing over naming.
if [ -z "${CLOUDFLARE_API_TOKEN:-}" ]; then
    for name in CF_API_TOKEN CLOUDFLARE_TOKEN CF_TOKEN; do
        val="$(trim "${!name:-}")"
        if [ -n "$val" ]; then
            export CLOUDFLARE_API_TOKEN="$val"
            echo "note: found a Cloudflare token in \$$name; using it as CLOUDFLARE_API_TOKEN." >&2
            break
        fi
    done
fi

require_cloudflare_account() {
    if [ -n "${CLOUDFLARE_ACCOUNT_ID:-}" ]; then
        return 0
    fi
    echo "ERROR: CLOUDFLARE_ACCOUNT_ID is missing." >&2
    echo "       Set it in $ENV_FILE (shared with the relay Worker deploy)." >&2
    return 1
}

require_cloudflare_auth() {
    [ -n "${CLOUDFLARE_API_TOKEN:-}" ] && return 0
    if [ -t 0 ] && [ -t 1 ]; then
        return 0  # interactive: wrangler can OAuth at a TTY
    fi
    echo "ERROR: CLOUDFLARE_API_TOKEN is not set and this is a non-interactive run." >&2
    echo "       Set it in $ENV_FILE (shared with the relay Worker deploy)." >&2
    return 1
}

wrangler_cmd() {
    if ! command -v npm >/dev/null 2>&1; then
        echo "ERROR: npm is required to run wrangler for this JS Worker." >&2
        return 1
    fi
    npm exec --yes --package "$WRANGLER_NPM_SPEC" -- wrangler "$@"
}

# Prove forkmesh.com/ is actually answered by THIS Worker after the deploy: the
# x-forkmesh-worker: marketing header is stamped on every landing response and
# on the logged-in 302, so its absence means the exact-match route never took
# effect and the relay Worker is still serving the root.
verify_deploy() {
    local base="${DEPLOY_VERIFY_URL:-https://forkmesh.com}"
    base="${base%/}"
    if ! command -v curl >/dev/null 2>&1; then
        echo "note: curl not found — skipping post-deploy verification." >&2
        return 0
    fi
    echo "Verifying $base/ is served by the marketing Worker ..."
    local attempts=20 sleep_s=6 attempt headers
    for attempt in $(seq 1 "$attempts"); do
        headers="$(curl -sSI --max-time 20 "$base/" 2>/dev/null || true)"
        if printf '%s' "$headers" | grep -qi '^x-forkmesh-worker:[[:space:]]*marketing'; then
            echo "Verified: $base/ is answered by forkmesh-marketing."
            return 0
        fi
        echo "  attempt $attempt/$attempts: marketing header not seen yet; retrying in ${sleep_s}s..." >&2
        sleep "$sleep_s"
    done
    echo "ERROR: $base/ never reported x-forkmesh-worker: marketing." >&2
    echo "       The exact-match route forkmesh.com/ did not take effect — check that" >&2
    echo "       CLOUDFLARE_ACCOUNT_ID matches the account that owns forkmesh.com and" >&2
    echo "       that no conflicting route/redirect owns the zone root." >&2
    return 1
}

case "${1:-deploy}" in
    deploy)
        require_cloudflare_account
        require_cloudflare_auth
        echo "Deploying ForkMesh marketing Worker (forkmesh.com/) to Cloudflare..."
        wrangler_cmd deploy
        verify_deploy
        echo "Done. forkmesh.com/ is served by forkmesh-marketing; everything else stays on forkmesh-relay."
        ;;
    dev)
        wrangler_cmd dev
        ;;
    dry-run)
        require_cloudflare_account
        require_cloudflare_auth
        wrangler_cmd deploy --dry-run
        ;;
    *)
        echo "Usage: $0 [deploy|dev|dry-run]" >&2
        exit 2
        ;;
esac
