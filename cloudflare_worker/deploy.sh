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
#
# A production deploy stamps a BUILD_REV var and then VERIFIES the live origin is
# serving it (GET /api/version). If the public site never reports the new rev the
# deploy is treated as FAILED (exit 1) — this is what catches an upload that
# "succeeded" but landed on the wrong Cloudflare account, leaving stale code live.
# Override the verified origin with DEPLOY_VERIFY_URL (default https://forkmesh.com).
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

# Strip a trailing CR (CRLF-saved files) and surrounding whitespace. A stray \r
# or space on a value is a classic cause of a secret that "exists" in the
# dashboard but never matches at runtime (admin path / basic-auth comparisons).
trim() {
    local s="$1"
    s="${s%$'\r'}"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

# Inline --var args are built too, but used ONLY for local `dev`: workerd's dev
# server has no secret store unless you keep a .dev.vars file, so passing them
# inline keeps `./deploy.sh dev` working.
VAR_ARGS=()
CF_ACCOUNT_ID_SET=0
if [ -f "$ENV_FILE" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac   # skip blanks/comments
        case "$line" in *=*) ;; *) continue ;; esac # skip lines without KEY=VALUE
        key="$(trim "${line%%=*}")"
        value="$(trim "${line#*=}")"
        [ -z "$key" ] && continue
        # CLOUDFLARE_* keys configure wrangler itself (which account/token to use),
        # not the Worker. Export them so every pywrangler call targets the right
        # account — this is how the maintainer pins the account without committing
        # the ID to this open-source wrangler.toml — and never ship them as Worker
        # vars or secrets.
        case "$key" in
            CLOUDFLARE_ACCOUNT_ID)
                if [ -n "$value" ]; then
                    CF_ACCOUNT_ID_SET=1
                    export "$key=$value"
                fi
                continue
                ;;
            CLOUDFLARE_*)
                [ -n "$value" ] && export "$key=$value"
                continue
                ;;
        esac
        VAR_ARGS+=(--var "${key}:${value}")          # .env uses =, wrangler uses :
    done < "$ENV_FILE"
fi

require_cloudflare_account() {
    if [ "$CF_ACCOUNT_ID_SET" = "1" ] || [ -n "${CLOUDFLARE_ACCOUNT_ID:-}" ]; then
        return 0
    fi
    echo "ERROR: CLOUDFLARE_ACCOUNT_ID is missing." >&2
    echo "       Set it in $ENV_FILE (or the CI secret that writes that file) before deploying." >&2
    return 1
}

# A stamp identifying exactly which build we are shipping. The git rev (marked
# -dirty when the tree has uncommitted changes) when available, else a UTC
# timestamp. Passed to the Worker as the BUILD_REV var and echoed back by
# /api/version so a deploy can prove the new code is actually live.
build_rev() {
    local rev
    if rev="$(git rev-parse --short=12 HEAD 2>/dev/null)" && [ -n "$rev" ]; then
        if ! git diff --quiet HEAD 2>/dev/null || \
           [ -n "$(git status --porcelain 2>/dev/null)" ]; then
            rev="${rev}-dirty-$(date -u +%s)"
        fi
        printf '%s' "$rev"
    else
        printf 'ts-%s' "$(date -u +%Y%m%d%H%M%S)"
    fi
}

# After a deploy, confirm the live origin is actually serving the build we just
# shipped. Polls /api/version (allowing for edge propagation) and matches its
# reported rev against the BUILD_REV we stamped. A mismatch that never resolves
# means the upload didn't take effect on the public origin — most often because
# it landed on the wrong Cloudflare account — so we FAIL LOUDLY rather than let a
# phantom "Done." hide stale code. Override the origin with DEPLOY_VERIFY_URL.
verify_deploy() {
    local expected="$1"
    local base="${DEPLOY_VERIFY_URL:-https://forkmesh.com}"
    base="${base%/}"
    local url="$base/api/version"
    if ! command -v curl >/dev/null 2>&1; then
        echo "note: curl not found — skipping post-deploy verification." >&2
        return 0
    fi
    echo "Verifying $url is serving BUILD_REV=$expected ..."
    local attempt body got
    for attempt in $(seq 1 20); do
        body="$(curl -fsS --max-time 15 "$url" 2>/dev/null || true)"
        # Pull "rev":"<value>" out of the JSON without needing jq.
        got="$(printf '%s' "$body" | sed -n 's/.*"rev"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"
        if [ "$got" = "$expected" ]; then
            echo "Verified: live origin is serving build $expected."
            return 0
        fi
        echo "  attempt $attempt/20: live rev='${got:-<none>}' (want '$expected'); retrying in 6s..." >&2
        sleep 6
    done
    echo "ERROR: $base never reported BUILD_REV=$expected after the deploy." >&2
    echo "       Last live rev was '${got:-<none>}'. The upload did NOT take effect on" >&2
    echo "       this origin (most likely it hit the wrong Cloudflare account, or the" >&2
    echo "       custom domain still routes to an old Worker). Check that" >&2
    echo "       CLOUDFLARE_ACCOUNT_ID in $ENV_FILE matches the account that owns" >&2
    echo "       forkmesh.com, then redeploy." >&2
    return 1
}

# Push every KEY=VALUE in .env.production to the deployed Worker as a SECRET.
# Idempotent (re-running updates values) and persists across redeploys. Requires
# the Worker to already exist, so run it after `pywrangler deploy`.
push_secrets() {
    if [ ! -f "$ENV_FILE" ]; then
        echo "note: $ENV_FILE not found — no secrets to push." >&2
        return 0
    fi
    # Worker secrets that MUST be set for the site to work; an empty/missing one
    # is a hard error, not a silent skip (that's what made a broken deploy look
    # successful). TREASURY_BCH_ADDRESS is optional (legacy), so it's not listed.
    local required=" ADMIN_PATH ADMIN_USER ADMIN_PASS TREASURY_SOLANA_ADDRESS "

    echo "Pushing secrets from: $(cd "$(dirname "$ENV_FILE")" && pwd)/$(basename "$ENV_FILE")"
    local count=0
    local pushed=()
    local empties=()
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac
        case "$line" in *=*) ;; *) continue ;; esac
        local key value
        key="$(trim "${line%%=*}")"
        value="$(trim "${line#*=}")"
        [ -z "$key" ] && continue
        # CLOUDFLARE_* are wrangler config (exported above), not Worker secrets.
        case "$key" in CLOUDFLARE_*) continue ;; esac
        # Never push an empty value: it sets a blank secret, which looks "set" in
        # the dashboard but locks out the admin path / basic auth at runtime.
        if [ -z "$value" ]; then
            echo "  skip (empty): $key" >&2
            empties+=("$key")
            continue
        fi
        case "$value" in
            *CHANGE-ME*|change-me*)
                echo "  warning: $key still has a placeholder value ($value)" >&2 ;;
        esac
        echo "  secret: $key"
        # Feed the value on stdin terminated by a newline — the documented
        # non-interactive form; wrangler strips the single trailing newline. The
        # previous no-newline pipe could leave the value unread (blank secret).
        printf '%s\n' "$value" | pywrangler secret put "$key"
        pushed+=("$key")
        count=$((count + 1))
    done < "$ENV_FILE"
    echo "Pushed $count secret(s) from $ENV_FILE."

    # Hard-fail if any REQUIRED secret never got a value (empty or missing from
    # the file). This is what previously slipped through as a "successful" deploy
    # with no admin creds / treasury address set.
    local req missing_req=()
    for req in $required; do
        case " ${pushed[*]-} " in *" $req "*) ;; *) missing_req+=("$req") ;; esac
    done
    if [ "${#missing_req[@]}" -gt 0 ]; then
        echo "ERROR: required secret(s) empty or missing in $ENV_FILE: ${missing_req[*]}" >&2
        echo "       Set them (real values, not blank) and re-run './deploy.sh secrets'." >&2
        return 1
    fi

    # Verify: confirm each pushed name actually exists on the Worker now, so a
    # silently-failed `secret put` becomes a loud error instead of a mystery.
    local listed
    if listed="$(pywrangler secret list 2>/dev/null)"; then
        local missing=()
        local k
        for k in ${pushed[@]+"${pushed[@]}"}; do
            case "$listed" in *"\"$k\""*) ;; *) missing+=("$k") ;; esac
        done
        if [ "${#missing[@]}" -gt 0 ]; then
            echo "ERROR: these secrets did not register: ${missing[*]}" >&2
            echo "       re-run './deploy.sh secrets' or check 'pywrangler secret list'." >&2
            return 1
        fi
        echo "Verified ${#pushed[@]} secret(s) present on the Worker."
    else
        echo "note: could not list secrets to verify (continuing)." >&2
    fi
}

case "${1:-deploy}" in
    deploy)
        require_cloudflare_account
        BUILD_REV="$(build_rev)"
        echo "Deploying ForkMesh website + relay to Cloudflare (build $BUILD_REV)..."
        # Stamp the build into the Worker as a plaintext var so /api/version can
        # report it. --var is MERGED with wrangler.toml [vars] (it does not wipe
        # them) and we re-pass it every deploy, so it persists; secrets are
        # untouched. This is the marker verify_deploy checks below.
        pywrangler deploy --var "BUILD_REV:${BUILD_REV}"
        # Secrets are set after the Worker exists; unlike plaintext vars they
        # survive this and future deploys, so the admin dashboard keeps working.
        push_secrets
        # Prove the public origin is actually serving what we just uploaded. A
        # failed/no-op/wrong-account deploy now aborts here instead of printing a
        # phantom success.
        verify_deploy "$BUILD_REV"
        echo "Done. Live at https://forkmesh.com (and any custom domain)."
        ;;
    secrets)
        require_cloudflare_account
        # Re-push just the .env.production secrets, no full redeploy.
        push_secrets
        ;;
    dev)
        pywrangler dev ${VAR_ARGS[@]+"${VAR_ARGS[@]}"}
        ;;
    dry-run)
        require_cloudflare_account
        pywrangler deploy --dry-run
        ;;
    *)
        echo "Usage: $0 [deploy|secrets|dev|dry-run]" >&2
        exit 2
        ;;
esac
