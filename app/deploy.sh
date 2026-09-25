#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

. ./pywrangler.sh

ENV_FILE=".env.production"
USE_WRANGLER_OAUTH="${FORKMESH_USE_WRANGLER_OAUTH:-0}"

trim() {
    local s="$1"
    s="${s%$'\r'}"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

VAR_ARGS=()
CF_ACCOUNT_ID_SET=0
EDGE_ROUTER_PUBLIC_KEY=""
EDGE_ROUTER_SIGNING_SEED=""
if [ -f "$ENV_FILE" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac   # skip blanks/comments
        case "$line" in *=*) ;; *) continue ;; esac # skip lines without KEY=VALUE
        key="$(trim "${line%%=*}")"
        value="$(trim "${line#*=}")"
        [ -z "$key" ] && continue
        if [ "$key" = "MIRROR_ROUTER_PUBLIC_KEY" ]; then
            EDGE_ROUTER_PUBLIC_KEY="$value"
        elif [ "$key" = "MIRROR_ROUTER_SIGNING_SEED" ]; then
            EDGE_ROUTER_SIGNING_SEED="$value"
        fi
        case "$key" in
            CLOUDFLARE_ACCOUNT_ID)
                if [ -n "$value" ]; then
                    CF_ACCOUNT_ID_SET=1
                    export "$key=$value"
                fi
                continue
                ;;
            CLOUDFLARE_API_TOKEN)
                if [ "$USE_WRANGLER_OAUTH" != "1" ] && [ -n "$value" ]; then
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

adopt_cloudflare_token_alias() {
    [ -n "${CLOUDFLARE_API_TOKEN:-}" ] && return 0
    local name val
    for name in CF_API_TOKEN CLOUDFLARE_TOKEN CF_TOKEN; do
        val="$(trim "${!name:-}")"
        if [ -n "$val" ]; then
            export CLOUDFLARE_API_TOKEN="$val"
            echo "note: found a Cloudflare token in \$$name; using it as CLOUDFLARE_API_TOKEN." >&2
            return 0
        fi
    done
    return 1
}

require_cloudflare_auth() {
    if [ "$USE_WRANGLER_OAUTH" = "1" ]; then
        unset CLOUDFLARE_API_TOKEN
        echo "note: using the existing Wrangler OAuth session for this deploy." >&2
        return 0
    fi
    adopt_cloudflare_token_alias || true
    [ -n "${CLOUDFLARE_API_TOKEN:-}" ] && return 0
    if [ -t 0 ] && [ -t 1 ]; then
        return 0   # a human at a terminal: let wrangler do its OAuth login
    fi
    echo "ERROR: no Cloudflare credentials for a non-interactive deploy." >&2
    echo "       wrangler can't open an interactive OAuth login here, so set a" >&2
    echo "       scoped API token (permissions: Workers Scripts: Edit + D1: Edit)." >&2
    echo "       In ForkMesh: Settings -> Secrets & Coves, add CLOUDFLARE_API_TOKEN" >&2
    echo "       (and CLOUDFLARE_ACCOUNT_ID); the deploy workflow substitutes those" >&2
    echo "       vars into $ENV_FILE for you. Outside ForkMesh, set it directly in" >&2
    echo "       $ENV_FILE (deploy.sh exports it for wrangler):" >&2
    echo "         CLOUDFLARE_API_TOKEN=..." >&2
    echo "       Create one: https://developers.cloudflare.com/fundamentals/api/get-started/create-token/" >&2
    local seen
    seen="$(compgen -v 2>/dev/null | grep -E '^(CF_|CLOUDFLARE_)' \
        | grep -v '^CLOUDFLARE_ACCOUNT_ID$' \
        | awk 'NR>1{printf ", "} {printf "%s",$0}' || true)"
    if [ -n "$seen" ]; then
        echo "       NOTE: this run's environment has Cloudflare-related vars named:" >&2
        echo "         $seen" >&2
        echo "       If one of those holds your API token, rename it to" >&2
        echo "       CLOUDFLARE_API_TOKEN in Settings -> Secrets & Coves." >&2
    fi
    return 1
}

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

app_version() {
    local cmake="../desktop/CMakeLists.txt"
    [ -f "$cmake" ] || return 0
    sed -n 's/^project(ForkMesh VERSION \([0-9][0-9.]*\).*/\1/p' "$cmake" | head -n1
}

signal_deploy_status() {
    local state="$1"
    local revision="$2"
    local now_ms
    local sql
    now_ms="$(date -u +%s)000"
    case "$state" in
        deploying)
            sql="INSERT INTO world_deploy_status(singleton,state,revision,started_at,finished_at) VALUES(1,'deploying','$revision',$now_ms,0) ON CONFLICT(singleton) DO UPDATE SET state='deploying',revision='$revision',started_at=$now_ms,finished_at=0"
            ;;
        ready|failed)
            sql="INSERT INTO world_deploy_status(singleton,state,revision,started_at,finished_at) VALUES(1,'$state','$revision',$now_ms,$now_ms) ON CONFLICT(singleton) DO UPDATE SET state='$state',revision='$revision',finished_at=$now_ms"
            ;;
        *)
            return 2
            ;;
    esac
    if ! pywrangler d1 execute "${FORKMESH_D1_NAME:-forkmesh}" --remote \
        --command "$sql"; then
        return 1
    fi
    echo "Deploy alert semaphore: state=$state revision=$revision at=$now_ms"
}

DEPLOY_SIGNAL_ACTIVE=0
mark_interrupted_deploy() {
    local exit_code=$?
    if [ "$DEPLOY_SIGNAL_ACTIVE" = "1" ] && [ -n "${BUILD_REV:-}" ]; then
        signal_deploy_status failed "$BUILD_REV" >/dev/null 2>&1 || true
    fi
    return "$exit_code"
}

build_dashboard_assets() {
    python3 tools/build_dashboard_assets.py
    python3 tools/build_worker_footprint.py
}

_py_http_get() {
    command -v python3 >/dev/null 2>&1 || return 1
    python3 - "$1" <<'PYEOF'
import sys, urllib.request
req = urllib.request.Request(sys.argv[1], headers={
    "User-Agent": "forkmesh-deploy-verify/1.0", "Accept": "application/json"})
try:
    with urllib.request.urlopen(req, timeout=25) as r:
        sys.stdout.write(r.read().decode(errors="replace"))
        sys.stdout.write("\n" + str(r.status))
except Exception as e:
    code = getattr(e, "code", None)
    if code is None:
        sys.exit(1)          # no HTTP response at all (DNS/TCP/TLS failure)
    sys.stdout.write("\n" + str(code))
PYEOF
}

verify_deploy() {
    local expected="$1"
    local endpoint="${2:-/api/version}"
    local base="${3:-${DEPLOY_VERIFY_URL:-https://app.forkmesh.com}}"
    local expected_fingerprint="${4:-}"
    local expected_router_public_key="${5:-}"
    base="${base%/}"
    case "$endpoint" in
        /*) ;;
        *)
            echo "ERROR: deploy verification endpoint must be an absolute path." >&2
            return 2
            ;;
    esac
    local url="$base$endpoint"
    echo "Verifying $url is serving BUILD_REV=$expected ..."
    local attempts="${FORKMESH_DEPLOY_VERIFY_ATTEMPTS:-30}"
    local max_time="${FORKMESH_DEPLOY_VERIFY_TIMEOUT:-25}"
    local sleep_s="${FORKMESH_DEPLOY_VERIFY_BACKOFF:-6}"
    local attempt body got worker fingerprint router_public_key router_ready
    local http_code curl_rc via
    for attempt in $(seq 1 "$attempts"); do
        if command -v curl >/dev/null 2>&1; then
            curl_rc=0
            via=curl
            body="$(curl -sS --max-time "$max_time" -w $'\n%{http_code}' "$url" 2>/dev/null)" || curl_rc=$?
            if [ "$curl_rc" != 0 ] && body="$(_py_http_get "$url")"; then
                curl_rc=0
                via=python3
            fi
        else
            curl_rc=0
            via=python3
            body="$(_py_http_get "$url")" || curl_rc=$?
        fi
        http_code="${body##*$'\n'}"
        body="${body%$'\n'*}"
        got="$(printf '%s' "$body" | sed -n 's/.*"rev"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"
        worker="$(printf '%s' "$body" | sed -n 's/.*"worker"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"
        fingerprint="$(printf '%s' "$body" | sed -n 's/.*"deployFingerprint"[[:space:]]*:[[:space:]]*"\([0-9a-f]\{64\}\)".*/\1/p')"
        router_public_key="$(printf '%s' "$body" | sed -n 's/.*"routerPublicKey"[[:space:]]*:[[:space:]]*"\([A-Za-z0-9_-]\{43\}\)".*/\1/p')"
        router_ready="$(printf '%s' "$body" | sed -n 's/.*"routerIdentityReady"[[:space:]]*:[[:space:]]*\(true\|false\).*/\1/p')"
        if [ "$curl_rc" = 0 ] && [ "$http_code" = "200" ] && \
           [ "$got" = "$expected" ] && [ "$worker" = "app" ] && \
           { [ -z "$expected_fingerprint" ] || \
             [ "$fingerprint" = "$expected_fingerprint" ]; } && \
           { [ -z "$expected_router_public_key" ] || \
             { [ "$router_public_key" = "$expected_router_public_key" ] && \
               [ "$router_ready" = "true" ]; }; }; then
            echo "Verified: live origin is serving build $expected (via $via)."
            if [ "$via" = "python3" ]; then
                echo "note: curl could not reach $url but python3 could — a host" >&2
                echo "      firewall rule is likely blocking the curl binary itself" >&2
                echo "      (e.g. an OpenSnitch 'deny /usr/bin/curl' rule)." >&2
            fi
            return 0
        fi
        if [ "$curl_rc" != 0 ]; then
            echo "  attempt $attempt/$attempts: curl failed (exit $curl_rc, e.g. timeout/DNS/TLS); retrying in ${sleep_s}s..." >&2
        else
            echo "  attempt $attempt/$attempts: HTTP $http_code, live rev='${got:-<none>}', worker='${worker:-<none>}', fingerprint='${fingerprint:-<none>}', router-ready='${router_ready:-<none>}' (want '$expected' via app with the deployed identity, via $via); retrying in ${sleep_s}s..." >&2
        fi
        sleep "$sleep_s"
    done
    echo "ERROR: $base never reported BUILD_REV=$expected after the deploy." >&2
    if [ "$curl_rc" != 0 ]; then
        echo "       Last attempt failed to connect (curl exit $curl_rc) — check network egress" >&2
        echo "       from the deploy runner and that app.forkmesh.com resolves and is reachable." >&2
    elif [ "$http_code" != "200" ]; then
        echo "       Last attempt got HTTP $http_code from $url (expected 200). That's a" >&2
        echo "       server-side/routing error, not a stale-code mismatch — check the Worker's" >&2
        echo "       error log (admin dashboard) for what's failing on that origin." >&2
    else
        echo "       Last live rev/fingerprint was '${got:-<none>}'/'${fingerprint:-<none>}'. The upload did NOT take effect on" >&2
        echo "       this origin (most likely it hit the wrong Cloudflare account, or the" >&2
        echo "       custom domain still routes to an old Worker). Check that" >&2
        echo "       CLOUDFLARE_ACCOUNT_ID in $ENV_FILE matches the account that owns" >&2
        echo "       app.forkmesh.com, then redeploy." >&2
    fi
    return 1
}

_json_string_field() {
    local field="$1"
    command -v python3 >/dev/null 2>&1 || return 1
    python3 - "$field" 3<&0 <<'PYEOF'
import json
import os
import sys

try:
    value = json.load(os.fdopen(3)).get(sys.argv[1], "")
except (AttributeError, json.JSONDecodeError, OSError, TypeError):
    raise SystemExit(1)
if not isinstance(value, str):
    raise SystemExit(1)
sys.stdout.write(value)
PYEOF
}

_json_boolean_field() {
    local field="$1"
    command -v python3 >/dev/null 2>&1 || return 1
    python3 - "$field" 3<&0 <<'PYEOF'
import json
import os
import sys

try:
    value = json.load(os.fdopen(3)).get(sys.argv[1])
except (AttributeError, json.JSONDecodeError, OSError, TypeError):
    raise SystemExit(1)
if value is True:
    sys.stdout.write("true")
elif value is False:
    sys.stdout.write("false")
else:
    raise SystemExit(1)
PYEOF
}

_router_identity_is_disabled() {
    command -v python3 >/dev/null 2>&1 || return 1
    python3 - 3<&0 <<'PYEOF'
import json
import os

try:
    value = json.load(os.fdopen(3))
except (json.JSONDecodeError, OSError, TypeError):
    raise SystemExit(1)
valid = (
    isinstance(value, dict)
    and value.get("ok") is False
    and value.get("protocol") == "forkmesh-masked-proxy-v1"
    and value.get("registration") == "forkmesh-https-endpoint-v1"
    and value.get("routerPublicKey") == ""
)
raise SystemExit(0 if valid else 1)
PYEOF
}

_http_json_get() {
    local url="$1" response status
    if command -v curl >/dev/null 2>&1; then
        response="$(curl -sS --max-time 25 -w $'\n%{http_code}' \
            -H "Accept: application/json" "$url" 2>/dev/null)" || response=""
    else
        response="$(_py_http_get "$url")" || response=""
    fi
    [ -n "$response" ] || return 1
    status="${response##*$'\n'}"
    [ "$status" = "200" ] || return 1
    printf '%s' "${response%$'\n'*}"
}

live_router_public_key() {
    local base="${1:-$(edge_control_verify_url)}" body key
    body="$(_http_json_get "${base%/}/api/mirrors/https")" || return 1
    key="$(printf '%s' "$body" | _json_string_field routerPublicKey)" || return 1
    [[ "$key" =~ ^[A-Za-z0-9_-]{43}$ ]] || return 1
    printf '%s' "$key"
}

verify_live_router_public_key() {
    local expected="$1"
    local base="${2:-$(edge_control_verify_url)}"
    local attempts="${FORKMESH_ROUTER_VERIFY_ATTEMPTS:-${3:-10}}"
    local sleep_s="${FORKMESH_ROUTER_VERIFY_BACKOFF:-${4:-3}}" attempt live=""
    for attempt in $(seq 1 "$attempts"); do
        live="$(live_router_public_key "$base")" || live=""
        if [ "$live" = "$expected" ]; then
            echo "Verified the live mirror-router public identity."
            return 0
        fi
        if [ "$attempt" -lt "$attempts" ]; then
            echo "  attempt $attempt/$attempts: mirror-router identity is unavailable or different; retrying in ${sleep_s}s..." >&2
            sleep "$sleep_s"
        fi
    done
    echo "ERROR: ${base%/}/api/mirrors/https did not report the expected mirror-router public key." >&2
    return 1
}

_router_key_pair_matches() {
    command -v python3 >/dev/null 2>&1 || return 1
    python3 - 3<<<"$EDGE_ROUTER_PUBLIC_KEY" \
        4<<<"$EDGE_ROUTER_SIGNING_SEED" <<'PYEOF'
import base64
import hashlib
import os


def decode(value):
    if len(value) != 43:
        raise ValueError
    raw = base64.b64decode(value + "=", altchars=b"-_", validate=True)
    if len(raw) != 32:
        raise ValueError
    canonical = base64.urlsafe_b64encode(raw).decode("ascii").rstrip("=")
    if canonical != value:
        raise ValueError
    return raw


def add(left, right):
    prime = 2**255 - 19
    curve_d = -121665 * pow(121666, prime - 2, prime) % prime
    x1, y1 = left
    x2, y2 = right
    product = x1 * x2 * y1 * y2 % prime
    x3 = (x1 * y2 + y1 * x2) * pow(1 + curve_d * product, prime - 2, prime)
    y3 = (y1 * y2 + x1 * x2) * pow(1 - curve_d * product, prime - 2, prime)
    return x3 % prime, y3 % prime


def public_from_seed(seed):
    digest = hashlib.sha512(seed).digest()
    scalar = int.from_bytes(digest[:32], "little")
    scalar = (scalar & ((1 << 254) - 8)) | (1 << 254)
    point = (0, 1)
    addend = (
        15112221349535400772501151409588531511454012693041857206046113283949847762202,
        46316835694926478169428394003475163141307993866256225615783033603165251855960,
    )
    while scalar:
        if scalar & 1:
            point = add(point, addend)
        addend = add(addend, addend)
        scalar >>= 1
    x, y = point
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


try:
    public = decode(os.fdopen(3).read().strip())
    seed = decode(os.fdopen(4).read().strip())
except (OSError, UnicodeError, ValueError):
    raise SystemExit(1)
raise SystemExit(0 if public_from_seed(seed) == public else 1)
PYEOF
}

official_multi_host_enabled() {
    grep -q 'PUBLIC_BASE_URL = "https://forkmesh.com"' wrangler.toml
}

_split_check() {
    local url="$1" want_worker="$2" want_status="$3"
    local attempts=10 sleep_s=3 attempt headers status worker
    for attempt in $(seq 1 "$attempts"); do
        headers="$(curl -sSI --max-time 20 "$url" 2>/dev/null || true)"
        status="$(printf '%s\n' "$headers" | awk 'toupper($1) ~ /^HTTP\// { code=$2 } END { print code }')"
        worker="$(printf '%s\n' "$headers" | awk -F': *' 'tolower($1) == "x-forkmesh-worker" { value=$2 } END { sub(/\r$/, "", value); print value }')"
        if [ "$status" = "$want_status" ] && [ "$worker" = "$want_worker" ]; then
            return 0
        fi
        sleep "$sleep_s"
    done
    echo "ERROR: $url answered HTTP ${status:-<none>} via '${worker:-unknown}' (expected $want_status via '$want_worker')." >&2
    return 1
}

_secret_bulk_error_is_transient() {
    case "$1" in
        *"code: 10013"*|\
        *"Internal Server Error"*|*"Bad Gateway"*|*"Service Unavailable"*|\
        *"Gateway Time-out"*|*"Gateway Timeout"*|\
        *"fetch failed"*|*"socket hang up"*|*"ECONNRESET"*|*"ETIMEDOUT"*|\
        *"EAI_AGAIN"*|*"Client network socket disconnected"*)
            return 0
            ;;
    esac
    return 1
}

verify_remote_data_key() {
    local value="$1"
    local action="${2:-publish}"
    local base="${DEPLOY_VERIFY_URL:-https://app.forkmesh.com}"
    local timestamp proof response status body
    timestamp="$(date -u +%s)000"
    proof="$(python3 - "$timestamp" 3< <(printf '%s' "$value") <<'PYEOF'
import hashlib
import hmac
import os
import sys

key = os.fdopen(3, "rb").read()
canonical = b"forkmesh-bootstrap-readiness-v1\n" + sys.argv[1].encode("ascii")
print(hmac.new(key, canonical, hashlib.sha256).hexdigest())
PYEOF
)" || return 1
    response="$(curl -sS --max-time 25 \
        -H "Accept: application/json" \
        -H "Authorization: Bearer $proof" \
        -H "X-ForkMesh-Readiness-Timestamp: $timestamp" \
        -w $'\n%{http_code}' \
        "${base%/}/api/bootstrap/readiness")" || {
        echo "ERROR: the live DATA_KEY readiness probe could not be reached." >&2
        echo "       The existing remote key was not changed; retry when the App is reachable." >&2
        return 1
    }
    status="${response##*$'\n'}"
    body="${response%$'\n'*}"
    if [ "$status" = "404" ] && [ "$action" = "validate" ]; then
        echo "  first-cutover preflight: the live Worker predates authenticated DATA_KEY readiness"
        echo "  preserving its remote DATA_KEY; the new Worker must prove the local recovery key before secrets publish"
        return 3
    fi
    if [ "$status" != "200" ]; then
        echo "ERROR: local DATA_KEY did not authenticate against the live Worker (HTTP ${status:-<none>})." >&2
        echo "       The existing remote key was not changed. Restore the matching recovery value before deploying." >&2
        return 1
    fi
    grep -Eq '"ok"[[:space:]]*:[[:space:]]*true' <<<"$body" || {
        echo "ERROR: live DATA_KEY readiness did not prove database encryption." >&2
        return 1
    }
}

push_secrets() {
    local action="${1:-publish}"
    case "$action" in
        validate|publish|publish-preserve-router) ;;
        *)
            echo "ERROR: invalid internal push_secrets action." >&2
            return 2
            ;;
    esac
    if [ ! -f "$ENV_FILE" ]; then
        echo "ERROR: $ENV_FILE not found; production secrets cannot be validated or published." >&2
        echo "       Copy .env.production.example, supply real values, and keep the file untracked." >&2
        return 1
    fi
    local required=" ADMIN_PATH ADMIN_PASS MAILTRAP_API_TOKEN MAILTRAP_WEBHOOK_SECRET DATA_KEY TREASURY_SOLANA_ADDRESS MIRROR_ROUTER_PUBLIC_KEY MIRROR_ROUTER_SIGNING_SEED DISCORD_CLIENT_ID DISCORD_CLIENT_SECRET DISCORD_BOT_TOKEN "
    local allowed=" ADMIN_ALERT_EMAILS ADMIN_PASS ADMIN_PATH COMMUNITY_REWARD_POOL_ADDRESS DATA_KEY DISCORD_BOT_TOKEN DISCORD_CLIENT_ID DISCORD_CLIENT_SECRET DISCORD_OAUTH_REDIRECT_URI MAILTRAP_API_TOKEN MAILTRAP_API_URL MAILTRAP_SENDER MAILTRAP_SENDER_NAME MAILTRAP_WEBHOOK_SECRET MIRROR_ROUTER_PUBLIC_KEY MIRROR_ROUTER_SIGNING_SEED OUTREACH_SENDER OUTREACH_SENDER_NAME POLAR_ACCESS_TOKEN POLAR_API_BASE_URL POLAR_ORGANIZATION_ID POLAR_PRO_PRODUCT_ID POLAR_SUPPORTER_PRODUCT_ID POLAR_WEBHOOK_SECRET REWARD_SIGNER_ACCOUNT SENTRY_CRON_CHECKIN_MARGIN_MINUTES SENTRY_CRON_MAX_RUNTIME_MINUTES SENTRY_CRON_MONITOR_SLUG SENTRY_CRON_TIMEZONE SENTRY_DSN SSH_GATEWAY_HOST SSH_GATEWAY_NODE_HOSTS SSH_GATEWAY_PORT SSH_GATEWAY_REPOSITORIES SSH_GATEWAY_TOKEN TREASURY_SOLANA_ADDRESS WORKERS_OBSERVABILITY_ACCOUNT_ID WORKERS_OBSERVABILITY_API_TOKEN WORKERS_OBSERVABILITY_SERVICE X_API_BEARER_TOKEN "

    if [ "$action" = "validate" ]; then
        echo "Validating production secrets in: $(cd "$(dirname "$ENV_FILE")" && pwd)/$(basename "$ENV_FILE")"
    elif [ "$action" = "publish-preserve-router" ]; then
        echo "Pushing non-router secrets from: $(cd "$(dirname "$ENV_FILE")" && pwd)/$(basename "$ENV_FILE")"
    else
        echo "Pushing secrets from: $(cd "$(dirname "$ENV_FILE")" && pwd)/$(basename "$ENV_FILE")"
    fi
    local count=0
    local pushed=()
    local secret_values=()
    local empties=()
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac
        case "$line" in *=*) ;; *) continue ;; esac
        local key value
        key="$(trim "${line%%=*}")"
        value="$(trim "${line#*=}")"
        [ -z "$key" ] && continue
        case "$key" in
            CLOUDFLARE_ACCOUNT_ID)
                key="WORKERS_OBSERVABILITY_ACCOUNT_ID"
                ;;
            CLOUDFLARE_OBSERVABILITY_API_TOKEN)
                key="WORKERS_OBSERVABILITY_API_TOKEN"
                ;;
            CLOUDFLARE_*)
                continue
                ;;
            VULTR_API_KEY|VULTR_API_TOKEN|VULTR_TOKEN|VULTR_KEY)
                continue
                ;;
        esac
        case "$allowed" in
            *" $key "*) ;;
            *)
                echo "  skip (not an App runtime secret): $key" >&2
                continue
                ;;
        esac
        if [ -z "$value" ]; then
            echo "  skip (empty): $key" >&2
            empties+=("$key")
            continue
        fi
        case "$value" in
            *CHANGE-ME*|change-me*)
                echo "  warning: $key still has a placeholder value" >&2 ;;
        esac
        echo "  secret: $key"
        pushed+=("$key")
        secret_values+=("$value")
        count=$((count + 1))
    done < "$ENV_FILE"

    local remote_secrets=""
    if ! remote_secrets="$(pywrangler secret list --env "" ${SPLIT_SECRET_WORKER:+--name "$SPLIT_SECRET_WORKER"} 2>/dev/null)"; then
        if [ "${FORKMESH_FIRST_DEPLOY:-0}" != "1" ]; then
            echo "ERROR: the existing Worker secret names could not be read." >&2
            echo "       Refusing to risk replacing DATA_KEY. Use FORKMESH_FIRST_DEPLOY=1 only when no Worker exists yet." >&2
            return 1
        fi
        echo "  first deployment: no existing Worker secret set to preserve"
        remote_secrets="[]"
    fi

    local req missing_req=() remote_only=()
    for req in $required; do
        case " ${pushed[*]-} " in *" $req "*) ;; *) missing_req+=("$req") ;; esac
    done
    if [ "${#missing_req[@]}" -gt 0 ]; then
        local still_missing=()
        for req in "${missing_req[@]}"; do
            case "$remote_secrets" in
                *"\"$req\""*) remote_only+=("$req") ;;
                *) still_missing+=("$req") ;;
            esac
        done
        if [ "${#still_missing[@]}" -gt 0 ]; then
            echo "ERROR: required secret(s) are missing both locally and on the Worker: ${still_missing[*]}" >&2
            echo "       Set them (real values, not blank) and re-run './deploy.sh secrets'." >&2
            return 1
        fi
        echo "  preserving existing remote secret(s): ${remote_only[*]}"
    fi
    if [ "$action" = "publish-preserve-router" ]; then
        local router_secret
        for router_secret in MIRROR_ROUTER_PUBLIC_KEY MIRROR_ROUTER_SIGNING_SEED; do
            if [[ "$remote_secrets" != *"\"$router_secret\""* ]]; then
                echo "ERROR: $router_secret is not present on the existing App Worker." >&2
                echo "       Generic secret updates cannot create or rotate the mirror-router identity." >&2
                echo "       Use './deploy.sh rotate-mirror-router' with a matched local key pair." >&2
                return 1
            fi
        done
        local router_filtered_names=() router_filtered_values=()
        local router_index
        for router_index in "${!pushed[@]}"; do
            case "${pushed[$router_index]}" in
                MIRROR_ROUTER_PUBLIC_KEY|MIRROR_ROUTER_SIGNING_SEED) ;;
                *)
                    router_filtered_names+=("${pushed[$router_index]}")
                    router_filtered_values+=("${secret_values[$router_index]}")
                    ;;
            esac
        done
        pushed=("${router_filtered_names[@]}")
        secret_values=("${router_filtered_values[@]}")
        count="${#pushed[@]}"
        for router_secret in MIRROR_ROUTER_PUBLIC_KEY MIRROR_ROUTER_SIGNING_SEED; do
            case " ${remote_only[*]-} " in
                *" $router_secret "*) ;;
                *) remote_only+=("$router_secret") ;;
            esac
        done
        echo "  preserving the coordinated remote mirror-router key pair"
    fi
    if [[ "$remote_secrets" == *'"DATA_KEY"'* ]]; then
        local data_key_value="" filtered_names=() filtered_values=()
        local data_index
        for data_index in "${!pushed[@]}"; do
            if [ "${pushed[$data_index]}" = "DATA_KEY" ]; then
                data_key_value="${secret_values[$data_index]}"
            else
                filtered_names+=("${pushed[$data_index]}")
                filtered_values+=("${secret_values[$data_index]}")
            fi
        done
        if [ -z "$data_key_value" ]; then
            echo "ERROR: DATA_KEY exists remotely but no local recovery value is available." >&2
            echo "       Restore DATA_KEY in $ENV_FILE before deploying; the live secret cannot be exported." >&2
            return 1
        fi
        local data_key_proof="authenticated"
        if verify_remote_data_key "$data_key_value" "$action"; then
            :
        else
            local proof_status=$?
            if [ "$action" = "validate" ] && [ "$proof_status" = "3" ]; then
                data_key_proof="preserved for first cutover"
            else
                return "$proof_status"
            fi
        fi
        pushed=("${filtered_names[@]}")
        secret_values=("${filtered_values[@]}")
        count=$((count - 1))
        case " ${remote_only[*]-} " in
            *" DATA_KEY "*) ;;
            *) remote_only+=("DATA_KEY") ;;
        esac
        echo "  preserving $data_key_proof remote secret: DATA_KEY"
    fi
    local discord_client_id=""
    local index
    for index in "${!pushed[@]}"; do
        if [ "${pushed[$index]}" = "DISCORD_CLIENT_ID" ]; then
            discord_client_id="${secret_values[$index]}"
            break
        fi
    done
    if [ -n "$discord_client_id" ] && ! [[ "$discord_client_id" =~ ^[0-9]{17,20}$ ]]; then
        echo "ERROR: DISCORD_CLIENT_ID must be a 17-20 digit Discord application ID." >&2
        return 1
    fi
    if [ "$action" = "validate" ]; then
        echo "Validated ${#pushed[@]} local and ${#remote_only[@]} preserved remote production secret(s)."
        return 0
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        echo "ERROR: python3 is required to encode the bulk secret payload." >&2
        return 1
    fi
    if ! (
        umask 077
        secret_bulk_file="$(mktemp "${TMPDIR:-/tmp}/forkmesh-worker-secrets.XXXXXX.json")" \
            || exit 1
        trap 'rm -f -- "$secret_bulk_file"' EXIT
        trap 'exit 129' HUP
        trap 'exit 130' INT
        trap 'exit 143' TERM
        chmod 0600 "$secret_bulk_file" || exit 1
        python3 - "$secret_bulk_file" 3< <(
            for index in "${!pushed[@]}"; do
                printf '%s\0%s\0' "${pushed[$index]}" "${secret_values[$index]}"
            done
        ) <<'PYEOF' || exit 1
import json
import os
import sys

raw = os.fdopen(3, "rb").read()
parts = raw.split(b"\0")
if not parts or parts[-1] != b"" or len(parts) % 2 != 1:
    raise SystemExit("invalid internal secret record stream")

payload = {}
for index in range(0, len(parts) - 1, 2):
    key = parts[index].decode("utf-8")
    value = parts[index + 1].decode("utf-8")
    payload[key] = value

with open(sys.argv[1], "w", encoding="utf-8") as stream:
    json.dump(payload, stream, ensure_ascii=False, separators=(",", ":"))
PYEOF
        [ -s "$secret_bulk_file" ] || exit 1
        attempts="${FORKMESH_SECRET_BULK_ATTEMPTS:-4}"
        backoff="${FORKMESH_SECRET_BULK_BACKOFF:-5}"
        attempt=1
        while :; do
            if bulk_output="$(pywrangler secret bulk --env "" ${SPLIT_SECRET_WORKER:+--name "$SPLIT_SECRET_WORKER"} "$secret_bulk_file" 2>&1)"; then
                if [ -n "$bulk_output" ]; then
                    printf '%s\n' "$bulk_output"
                fi
                if [ "$attempt" -gt 1 ]; then
                    echo "Bulk secret update succeeded on attempt $attempt/$attempts."
                fi
                exit 0
            fi
            if [ -n "$bulk_output" ]; then
                printf '%s\n' "$bulk_output" >&2
            fi
            if ! _secret_bulk_error_is_transient "$bulk_output"; then
                exit 1
            fi
            if [ "$attempt" -ge "$attempts" ]; then
                echo "  hit a transient Cloudflare API error on all $attempts attempts." >&2
                exit 1
            fi
            echo "  attempt $attempt/$attempts hit a transient Cloudflare API error (the" >&2
            echo "  Worker settings endpoint 5xx'd); re-sending the same bulk update in ${backoff}s..." >&2
            sleep "$backoff"
            attempt=$((attempt + 1))
            backoff=$((backoff * 3))
        done
    ); then
        echo "ERROR: bulk secret update failed; no per-secret retry was attempted." >&2
        echo "       (Per-secret 'secret put' is deliberately never used: it publishes one" >&2
        echo "       Worker version per secret, restarting Durable Objects mid-deploy.)" >&2
        echo "       If the error above is Cloudflare's '[code: 10013] An unknown error has" >&2
        echo "       occurred', the settings endpoint is having a bad minute and every" >&2
        echo "       secret still holds its previous value — re-run './deploy.sh secrets'." >&2
        return 1
    fi
    echo "Pushed $count secret(s) from $ENV_FILE in one bulk update."

    local listed
    if listed="$(pywrangler secret list --env "" ${SPLIT_SECRET_WORKER:+--name "$SPLIT_SECRET_WORKER"} 2>/dev/null)"; then
        local missing=()
        local k
        for k in $required; do
            case "$listed" in *"\"$k\""*) ;; *) missing+=("$k") ;; esac
        done
        if [ "${#missing[@]}" -gt 0 ]; then
            echo "ERROR: these secrets did not register: ${missing[*]}" >&2
            echo "       re-run './deploy.sh secrets' or check 'pywrangler secret list'." >&2
            return 1
        fi
        echo "Verified every required secret is present on the Worker."
    else
        echo "ERROR: could not list Worker secrets after the bulk update." >&2
        echo "       Refusing to continue without proving preserved remote secrets still exist." >&2
        return 1
    fi
}

publish_release_binary() {
    local force="${1:-0}"
    if ! command -v cmake >/dev/null 2>&1; then
        if [ "$force" = "1" ]; then
            echo "ERROR: cmake is required to force-republish a release binary." >&2
            return 1
        fi
        echo "note: cmake not found — skipping release binary build." >&2
        return 0
    fi
    if ! command -v go >/dev/null 2>&1; then
        echo "ERROR: Go is required to publish the native mirror-node companion." >&2
        return 1
    fi

    local os arch
    os="$(uname -s)"
    arch="$(uname -m)"
    case "$os" in
        Linux)                            os="linux" ;;
        Darwin)                           os="macos" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT)  os="windows" ;;
        *) os="$(printf '%s' "$os" | tr '[:upper:]' '[:lower:]')" ;;
    esac
    case "$arch" in
        x86_64|amd64|x64) arch="x86_64" ;;
        aarch64|arm64)    arch="arm64" ;;
    esac
    local asset="forkmesh-${os}-${arch}"
    [ "$os" = "windows" ] && asset="${asset}.exe"
    local mirror_asset="forkmesh-mirror-node-${os}-${arch}"
    [ "$os" = "windows" ] && mirror_asset="${mirror_asset}.exe"

    if [ -f ../.forkmesh/releases/latest/SHASUMS256.txt ] &&
       grep -q "  $asset" ../.forkmesh/releases/latest/SHASUMS256.txt 2>/dev/null &&
       grep -q "  $mirror_asset" ../.forkmesh/releases/latest/SHASUMS256.txt 2>/dev/null; then
        if [ "$force" != "1" ]; then
            echo "Release binary for $asset is already published."
            echo "To rebuild the same version from the current clean commit, run:" >&2
            echo "  FORKMESH_RELEASE_CAS=/path/to/served/cas ./deploy.sh republish-release-binary" >&2
            return 0
        fi
        echo "Force-republishing existing release asset $asset."
    fi

    local release_version release_tag tag_commit build_commit
    release_version="$(app_version)"
    if ! printf '%s' "$release_version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
        echo "ERROR: could not resolve a clean app version from desktop/CMakeLists.txt." >&2
        return 1
    fi
    release_tag="${FORKMESH_TAG:-v${release_version}}"
    build_commit="$(git -C .. log -1 --format=%H -- . \
        ':(exclude).forkmesh/releases/**' 2>/dev/null || true)"
    if ! printf '%s' "$build_commit" |
         grep -Eq '^([0-9a-f]{40}|[0-9a-f]{64})$'; then
        echo "ERROR: could not resolve the exact source commit for the release binary." >&2
        return 1
    fi
    if [ "$release_tag" != "v${release_version}" ]; then
        echo "ERROR: FORKMESH_TAG=$release_tag does not match app version v${release_version}." >&2
        return 1
    fi
    tag_commit="$(git -C .. rev-list -n1 "$release_tag" 2>/dev/null || true)"
    if ! printf '%s' "$tag_commit" |
         grep -Eq '^([0-9a-f]{40}|[0-9a-f]{64})$'; then
        echo "ERROR: $release_tag does not resolve to an exact Git commit." >&2
        return 1
    fi

    if [ "$force" = "1" ]; then
        if [ -z "${FORKMESH_RELEASE_CAS:-}" ]; then
            echo "ERROR: forced republish requires FORKMESH_RELEASE_CAS to name the served release CAS." >&2
            echo "       Refusing to publish metadata for bytes stored only in a throwaway default directory." >&2
            return 1
        fi
    fi
    local worktree_status
    if ! worktree_status="$(git -C .. status --porcelain=v1 \
            --untracked-files=all -- . \
            ':(exclude).forkmesh/releases/**' 2>&1)"; then
        echo "ERROR: could not verify that the release source worktree is clean." >&2
        printf '%s\n' "$worktree_status" >&2
        return 1
    fi
    if [ -n "$worktree_status" ]; then
        echo "ERROR: release binary build requires a clean source worktree." >&2
        echo "       Commit the exact code to publish, then retry." >&2
        return 1
    fi

    echo "Building and publishing ForkMesh v${release_version} for ${os}/${arch}…"

    local jobs
    jobs="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )"
    local configure_output
    if ! configure_output="$(cmake -S ../desktop -B ../desktop/build-release \
        -DCMAKE_BUILD_TYPE=Release -DFORKMESH_BUILD_TESTS=OFF \
        -DFORKMESH_BUILD_COMMIT_OVERRIDE="$build_commit" \
        -DFORKMESH_VERSION_OVERRIDE="$release_version" 2>&1)"; then
        printf '%s\n' "$configure_output" >&2
        echo "ERROR: failed to configure the release binary." >&2
        return 1
    fi
    printf '%s\n' "$configure_output" | grep -v "^--" || true
    if ! cmake --build ../desktop/build-release -j"$jobs" 2>&1 | tail -5; then
        echo "ERROR: failed to build release binary." >&2
        return 1
    fi
    local mirror_built="../server/forkmesh-mirror-node"
    [ "$os" = "windows" ] && mirror_built="${mirror_built}.exe"
    if ! (cd ../server && CGO_ENABLED=0 go build -trimpath \
        -ldflags='-s -w' -o "$(basename "$mirror_built")" \
        ./cmd/forkmesh-mirror-node); then
        echo "ERROR: failed to build the Go mirror-node companion." >&2
        return 1
    fi

    local built=""
    for cand in \
        "../desktop/build-release/forkmesh" \
        "../desktop/build-release/forkmesh.exe" \
        "../desktop/build-release/ForkMesh.app/Contents/MacOS/ForkMesh"; do
        if [ -x "$cand" ]; then built="$cand"; break; fi
    done
    if [ -z "$built" ]; then
        echo "ERROR: build did not produce an executable." >&2
        return 1
    fi

    local reported_version reported_commit
    if ! reported_version="$("$built" --version 2>&1)" || \
       [ "$reported_version" != "ForkMesh ${release_version}" ]; then
        echo "ERROR: built release reports '${reported_version:-<no version>}' (expected 'ForkMesh ${release_version}')." >&2
        return 1
    fi
    if ! reported_commit="$("$built" --build-commit 2>&1)" || \
       [ "$reported_commit" != "$build_commit" ]; then
        echo "ERROR: built release reports source commit '${reported_commit:-<unknown>}' (expected '$build_commit')." >&2
        return 1
    fi

    cp "$built" "$asset"
    chmod 0755 "$asset" || true
    cp "$mirror_built" "$mirror_asset"
    chmod 0755 "$mirror_asset" || true
    local cas_dir="${FORKMESH_RELEASE_CAS:-../.forkmesh/release-blobs}"
    case "$cas_dir" in
        /*) ;;
        *) cas_dir="$PWD/$cas_dir" ;;
    esac
    mkdir -p "$cas_dir"
    local publish_output
    local publish_args=(
        --channel latest
        --tag "$release_tag"
        --tag-commit "$tag_commit"
        --build-commit "$build_commit"
        --cas-dir "$cas_dir"
    )
    if [ -n "${FORKMESH_REPO:-}" ]; then
        publish_args+=(--repo "$FORKMESH_REPO")
    fi
    publish_args+=("app/$asset")
    publish_args+=("app/$mirror_asset")
    if ! publish_output="$(cd .. && tools/forkmesh-release-publish.sh \
        "${publish_args[@]}" 2>&1)"; then
        rm -f "$asset" "$mirror_asset" "$mirror_built"
        echo "ERROR: failed to publish release binary." >&2
        return 1
    fi
    printf '%s\n' "$publish_output" | tail -3

    local asset_hash mirror_asset_hash
    if command -v sha256sum >/dev/null 2>&1; then
        asset_hash="$(sha256sum "$asset" | awk '{print $1}')"
        mirror_asset_hash="$(sha256sum "$mirror_asset" | awk '{print $1}')"
    else
        asset_hash="$(shasum -a 256 "$asset" | awk '{print $1}')"
        mirror_asset_hash="$(shasum -a 256 "$mirror_asset" | awk '{print $1}')"
    fi
    if ! grep -Fqx "$asset_hash  $asset" \
            ../.forkmesh/releases/latest/SHASUMS256.txt ||
       ! grep -Fq "\"tag\": \"$release_tag\"" \
            ../.forkmesh/releases/latest/release.json ||
       ! grep -Fq "\"tag_commit\": \"$tag_commit\"" \
            ../.forkmesh/releases/latest/release.json ||
       ! grep -Fq "\"build_commit\": \"$build_commit\"" \
            ../.forkmesh/releases/latest/release.json ||
       ! grep -Fq "\"name\":\"$asset\",\"blob_sha256\":\"$asset_hash\"" \
            ../.forkmesh/releases/latest/release.json ||
       ! grep -Fqx "$mirror_asset_hash  $mirror_asset" \
            ../.forkmesh/releases/latest/SHASUMS256.txt ||
       ! grep -Fq "\"name\":\"$mirror_asset\",\"blob_sha256\":\"$mirror_asset_hash\"" \
            ../.forkmesh/releases/latest/release.json ||
       [ ! -f ../.forkmesh/releases/latest/release.json.sig ] ||
       [ "$(wc -c < ../.forkmesh/releases/latest/release.json.sig | tr -d ' ')" != "64" ] ||
       [ ! -f "$cas_dir/sha256/${asset_hash:0:2}/$asset_hash/data" ]; then
        rm -f "$asset" "$mirror_asset" "$mirror_built"
        echo "ERROR: release metadata/CAS verification did not match the freshly built commit and binary." >&2
        return 1
    fi
    echo "Verified release metadata: v${release_version}, tag ${tag_commit:0:12}, build ${build_commit:0:12}, sha256:${asset_hash:0:12}."
    rm -f "$asset" "$mirror_asset" "$mirror_built"

    git add ../.forkmesh/releases/latest/SHASUMS256.txt \
        ../.forkmesh/releases/latest/release.json \
        ../.forkmesh/releases/latest/release.json.sig || return 1
}

commit_release_metadata() {
    if git diff --quiet --cached ../.forkmesh/releases/latest/ 2>/dev/null; then
        return 0
    fi
    echo "Committing release metadata…"
    git config user.email "deploy@forkmesh.local" >/dev/null 2>&1 || true
    git config user.name  "ForkMesh Deploy"        >/dev/null 2>&1 || true
    git commit -m "release: publish prebuilt binary for $(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)" \
        2>&1 | tail -2
    echo "Release metadata staged. Push this commit alongside the Worker deploy."
}

deploy_target_is_changed() {
    local target="$1"
    local current remote="" base headers edge_marker=""
    current="$(python3 tools/deploy_targets.py fingerprint "$target")" || return 1
    if command -v curl >/dev/null 2>&1; then
        case "$target" in
            app)
                base="${DEPLOY_VERIFY_URL:-https://app.forkmesh.com}"
                local edge_base edge_body="" backend_body="" identity_body=""
                local edge_fingerprint="" edge_rev="" edge_worker=""
                local backend_fingerprint="" backend_rev="" backend_worker=""
                local backend_router_public_key="" backend_router_ready=""
                local router_public_key=""
                edge_base="$(edge_control_verify_url)"
                edge_body="$(_http_json_get "${edge_base%/}/api/version")" || edge_body=""
                backend_body="$(_http_json_get "${base%/}/api/mainnode")" || backend_body=""
                identity_body="$(_http_json_get "${edge_base%/}/api/mirrors/https")" || identity_body=""
                if [ -n "$edge_body" ]; then
                    edge_fingerprint="$(printf '%s' "$edge_body" | _json_string_field deployFingerprint 2>/dev/null || true)"
                    edge_rev="$(printf '%s' "$edge_body" | _json_string_field rev 2>/dev/null || true)"
                    edge_worker="$(printf '%s' "$edge_body" | _json_string_field worker 2>/dev/null || true)"
                fi
                if [ -n "$backend_body" ]; then
                    backend_fingerprint="$(printf '%s' "$backend_body" | _json_string_field deployFingerprint 2>/dev/null || true)"
                    backend_rev="$(printf '%s' "$backend_body" | _json_string_field rev 2>/dev/null || true)"
                    backend_worker="$(printf '%s' "$backend_body" | _json_string_field worker 2>/dev/null || true)"
                    backend_router_public_key="$(printf '%s' "$backend_body" | _json_string_field routerPublicKey 2>/dev/null || true)"
                    backend_router_ready="$(printf '%s' "$backend_body" | _json_boolean_field routerIdentityReady 2>/dev/null || true)"
                fi
                if [ -n "$identity_body" ]; then
                    router_public_key="$(printf '%s' "$identity_body" | _json_string_field routerPublicKey 2>/dev/null || true)"
                fi
                headers="$(curl -sSI --max-time 15 "${edge_base%/}/api/version" 2>/dev/null || true)"
                edge_marker="$(printf '%s\n' "$headers" | awk -F': *' 'tolower($1) == "x-forkmesh-edge-control" { value=$2 } END { sub(/\r$/, "", value); print value }')"
                if [ "$edge_fingerprint" = "$current" ] && \
                   [ "$backend_fingerprint" = "$current" ] && \
                   [ "$edge_worker" = "app" ] && \
                   [ "$backend_worker" = "app" ] && \
                   [ -n "$edge_rev" ] && [ "$edge_rev" != "dev" ] && \
                   [ "$backend_rev" = "$edge_rev" ] && \
                   [ "$backend_router_public_key" = "$EDGE_ROUTER_PUBLIC_KEY" ] && \
                   [ "$backend_router_ready" = "true" ] && \
                   [ "$edge_marker" = "active" ] && \
                   [ "$router_public_key" = "$EDGE_ROUTER_PUBLIC_KEY" ]; then
                    return 3
                fi
                return 0
                ;;
            world)
                base="${DEPLOY_VERIFY_WORLD_URL:-https://world.forkmesh.com}"
                ;;
        esac
        if [ "$target" != "app" ]; then
            headers="$(curl -sSI --max-time 15 "${base%/}/" 2>/dev/null || true)"
            remote="$(printf '%s\n' "$headers" | awk -F': *' 'tolower($1) == "x-forkmesh-deploy-fingerprint" { value=$2 } END { sub(/\r$/, "", value); print value }')"
        fi
        if [ -n "$remote" ] && [ "$remote" = "$current" ] && \
           { [ "$target" != "app" ] || [ "$edge_marker" = "active" ]; }; then
            return 3
        fi
        if [ -n "$remote" ]; then
            return 0
        fi
    fi
    return 0
}

mark_deploy_target() {
    python3 tools/deploy_targets.py mark "$1" "${BUILD_REV:-}" >/dev/null
}

prepare_target_deploy() {
    local target="$1"
    require_cloudflare_account
    require_cloudflare_auth
    BUILD_REV="$(build_rev)"
    APP_VERSION="$(app_version)"
    DEPLOYED_AT_MS="$(date -u +%s)000"
    DEPLOY_TARGET_FINGERPRINT="$(python3 tools/deploy_targets.py fingerprint "$target")"
}

deploy_static_target() {
    local target="$1"
    if [ "${FORKMESH_FORCE_DEPLOY:-0}" != "1" ]; then
        if deploy_target_is_changed "$target"; then
            :
        else
            local changed_status=$?
            if [ "$changed_status" = "3" ]; then
                echo "Skipping $target: its Worker inputs have not changed since the last successful deploy."
                return 0
            fi
            return "$changed_status"
        fi
    fi
    prepare_target_deploy "$target"
    if ! command -v npm >/dev/null 2>&1; then
        echo "ERROR: npm is required to deploy $target." >&2
        return 1
    fi
    echo "Deploying $target Worker (build $BUILD_REV)..."
    if [ "$target" = "world" ]; then
        signal_deploy_status deploying "$BUILD_REV"
        DEPLOY_SIGNAL_ACTIVE=1
        trap mark_interrupted_deploy EXIT
    fi
    (
        cd "../$target"
        npm exec --yes --package "${WRANGLER_NPM_SPEC:-wrangler@4.120.0}" -- \
            wrangler deploy --config wrangler.toml \
            --var "DEPLOY_FINGERPRINT:${DEPLOY_TARGET_FINGERPRINT}"
    )
    if command -v curl >/dev/null 2>&1; then
        case "$target" in
            world)
                local base="${DEPLOY_VERIFY_WORLD_URL:-https://world.forkmesh.com}"
                _split_check "${base%/}/" world 200
                _split_check "${base%/}/world/world.js" world 200
                ;;
        esac
        local fingerprint_headers fingerprint_live
        fingerprint_headers="$(curl -sSI --max-time 20 "${base%/}/" 2>/dev/null || true)"
        fingerprint_live="$(printf '%s\n' "$fingerprint_headers" | awk -F': *' 'tolower($1) == "x-forkmesh-deploy-fingerprint" { value=$2 } END { sub(/\r$/, "", value); print value }')"
        if [ "$fingerprint_live" != "$DEPLOY_TARGET_FINGERPRINT" ]; then
            echo "ERROR: $target reported deployment fingerprint ${fingerprint_live:-<none>}; expected $DEPLOY_TARGET_FINGERPRINT." >&2
            return 1
        fi
    fi
    if [ "$target" = "world" ]; then
        signal_deploy_status ready "$BUILD_REV"
        DEPLOY_SIGNAL_ACTIVE=0
        trap - EXIT
    fi
    mark_deploy_target "$target"
    echo "Done: $target Worker is live."
}

deploy_app_version() {
    pywrangler deploy --env "" \
        --var "BUILD_REV:${BUILD_REV}" \
        --var "APP_VERSION:${APP_VERSION}" \
        --var "DEPLOYED_AT_MS:${DEPLOYED_AT_MS}" \
        --var "DEPLOY_FINGERPRINT:${DEPLOY_TARGET_FINGERPRINT}"
}

deploy_edge_control_version() {
    if ! command -v npm >/dev/null 2>&1; then
        echo "ERROR: npm is required to deploy the App edge-control Worker." >&2
        return 1
    fi
    local router_public_key="${1-$EDGE_ROUTER_PUBLIC_KEY}"
    local edge_args=(
        --var "BUILD_REV:${BUILD_REV}"
        --var "APP_VERSION:${APP_VERSION}"
        --var "DEPLOYED_AT_MS:${DEPLOYED_AT_MS}"
        --var "DEPLOY_FINGERPRINT:${DEPLOY_TARGET_FINGERPRINT}"
        --var "MIRROR_ROUTER_PUBLIC_KEY:${router_public_key}"
    )
    (
        cd edge-control
        npm exec --yes --package "${WRANGLER_NPM_SPEC:-wrangler@4.120.0}" -- \
            wrangler deploy --config wrangler.toml "${edge_args[@]}"
    )
}

require_edge_router_public_key() {
    if [[ "$EDGE_ROUTER_PUBLIC_KEY" =~ ^[A-Za-z0-9_-]{43}$ ]] && \
       [[ "$EDGE_ROUTER_SIGNING_SEED" =~ ^[A-Za-z0-9_-]{43}$ ]] && \
       _router_key_pair_matches; then
        return 0
    fi
    echo "ERROR: the local mirror-router public key and signing seed must be a matched" >&2
    echo "       pair of 32-byte base64url Ed25519 values." >&2
    echo "       Restore both values in $ENV_FILE before deploying; no Worker was changed." >&2
    return 1
}

ROUTER_ROTATION_ACTIVE=0
authorize_router_rotation() {
    ROUTER_ROTATION_ACTIVE=0
    local live_key=""
    if ! live_key="$(live_router_public_key "$(edge_control_verify_url)")"; then
        if [ "${FORKMESH_FIRST_DEPLOY:-0}" = "1" ]; then
            ROUTER_ROTATION_ACTIVE=1
            echo "  first deployment: the edge identity will be established through a fail-closed state"
            return 0
        fi
        if [ "${FORKMESH_ALLOW_ROUTER_KEY_ROTATION:-0}" = "1" ]; then
            ROUTER_ROTATION_ACTIVE=1
            echo "Mirror-router recovery authorized; the edge identity will be forced fail-closed before secrets change."
            return 0
        fi
        echo "ERROR: the live mirror-router identity could not be read safely." >&2
        echo "       Refusing to publish secrets or routes without knowing the active public key." >&2
        echo "       Use FORKMESH_FIRST_DEPLOY=1 only when no App Worker exists yet." >&2
        return 1
    fi
    if [ "$live_key" = "$EDGE_ROUTER_PUBLIC_KEY" ]; then
        return 0
    fi
    if [ "${FORKMESH_ALLOW_ROUTER_KEY_ROTATION:-0}" != "1" ]; then
        echo "ERROR: the local mirror-router public key differs from the live identity." >&2
        echo "       Generic App deploys never rotate this trust root. Review the local pair," >&2
        echo "       then run './deploy.sh rotate-mirror-router' for the fail-closed rotation." >&2
        return 1
    fi
    ROUTER_ROTATION_ACTIVE=1
    echo "Mirror-router rotation authorized; the public identity will fail closed during the secret transition."
}

edge_control_verify_url() {
    if [ -n "${DEPLOY_VERIFY_EDGE_URL:-}" ]; then
        printf '%s' "${DEPLOY_VERIFY_EDGE_URL%/}"
    elif official_multi_host_enabled; then
        printf '%s' "https://app.forkmesh.com"
    else
        printf '%s' "${DEPLOY_VERIFY_URL:-https://app.forkmesh.com}"
    fi
}

verify_edge_control() {
    if ! command -v curl >/dev/null 2>&1; then
        echo "ERROR: curl is required to verify the App edge-control route marker." >&2
        return 1
    fi
    local base
    base="$(edge_control_verify_url)"
    local headers marker
    headers="$(curl -sSI --max-time 25 "${base%/}/api/version" 2>/dev/null || true)"
    marker="$(printf '%s\n' "$headers" | awk -F': *' 'tolower($1) == "x-forkmesh-edge-control" { value=$2 } END { sub(/\r$/, "", value); print value }')"
    if [ "$marker" != "active" ]; then
        echo "ERROR: ${base%/}/api/version did not traverse the App edge-control Worker." >&2
        return 1
    fi
    verify_live_router_public_key "$EDGE_ROUTER_PUBLIC_KEY" "$base"
}

verify_router_identity_disabled() (
    if ! command -v curl >/dev/null 2>&1; then
        echo "ERROR: curl is required to verify the fail-closed edge identity." >&2
        return 1
    fi
    local base
    base="$(edge_control_verify_url)"
    local attempts="${FORKMESH_ROUTER_VERIFY_ATTEMPTS:-10}"
    local sleep_s="${FORKMESH_ROUTER_VERIFY_BACKOFF:-3}"
    local attempt response status body marker
    local header_file
    umask 077
    header_file="$(mktemp "${TMPDIR:-/tmp}/forkmesh-router-headers.XXXXXX")" || return 1
    trap 'rm -f -- "$header_file"' EXIT
    for attempt in $(seq 1 "$attempts"); do
        : >"$header_file"
        response="$(curl -sS --max-time 25 -D "$header_file" \
            -w $'\n%{http_code}' -H "Accept: application/json" \
            "${base%/}/api/mirrors/https" 2>/dev/null)" || response=""
        status="${response##*$'\n'}"
        body="${response%$'\n'*}"
        marker="$(awk -F': *' 'tolower($1) == "x-forkmesh-edge-control" { value=$2 } END { sub(/\r$/, "", value); print value }' "$header_file")"
        if [ "$status" = "503" ] && [ "$marker" = "active" ] && \
           printf '%s' "$body" | _router_identity_is_disabled; then
            echo "Verified the mirror-router identity is fail-closed for rotation."
            return 0
        fi
        if [ "$attempt" -lt "$attempts" ]; then
            echo "  attempt $attempt/$attempts: waiting for the fail-closed router identity; retrying in ${sleep_s}s..." >&2
            sleep "$sleep_s"
        fi
    done
    echo "ERROR: the edge identity did not fail closed; the router secrets were not changed." >&2
    return 1
)

verify_legacy_api_alias() {
    command -v curl >/dev/null 2>&1 || {
        echo "ERROR: curl is required to verify the api.forkmesh.com cutover." >&2
        return 1
    }
    local base="${DEPLOY_VERIFY_LEGACY_API_URL:-https://api.forkmesh.com}"
    local expected_rev="${BUILD_REV:-}"
    local expected_fingerprint="${DEPLOY_TARGET_FINGERPRINT:-}"
    if [ -z "$expected_rev" ] || [ -z "$expected_fingerprint" ]; then
        local canonical_version
        canonical_version="$(_http_json_get "$(edge_control_verify_url)/api/version")" || {
            echo "ERROR: the canonical App identity is unavailable for alias comparison." >&2
            return 1
        }
        expected_rev="$(printf '%s' "$canonical_version" | _json_string_field rev)" || return 1
        expected_fingerprint="$(printf '%s' "$canonical_version" | _json_string_field deployFingerprint)" || return 1
    fi
    [ -n "$expected_rev" ] && \
        [[ "$expected_fingerprint" =~ ^[0-9a-f]{64}$ ]] || {
        echo "ERROR: the canonical App deploy identity is incomplete." >&2
        return 1
    }
    verify_deploy "$expected_rev" "/api/version" "$base" \
        "$expected_fingerprint"
    local headers marker status location
    headers="$(curl -sSI --max-time 25 "${base%/}/api/version" 2>/dev/null || true)"
    marker="$(printf '%s\n' "$headers" | awk -F': *' 'tolower($1) == "x-forkmesh-edge-control" { value=$2 } END { sub(/\r$/, "", value); print value }')"
    if [ "$marker" != "active" ]; then
        echo "ERROR: ${base%/}/api/version did not traverse the App edge-control Worker." >&2
        return 1
    fi
    verify_live_router_public_key "$EDGE_ROUTER_PUBLIC_KEY" "$base"
    headers="$(curl -sSI --max-time 25 "${base%/}/")" || return 1
    status="$(printf '%s\n' "$headers" | awk 'toupper($1) ~ /^HTTP\// { code=$2 } END { print code }')"
    location="$(printf '%s\n' "$headers" | awk 'tolower($1) == "location:" { value=$0; sub(/^[^:]*:[[:space:]]*/, "", value) } END { sub(/\r$/, "", value); print value }')"
    if [ "$status" != "308" ] || [ "$location" != "https://app.forkmesh.com/" ]; then
        echo "ERROR: ${base%/}/ returned HTTP ${status:-<none>} -> ${location:-<none>}; expected the canonical App redirect." >&2
        return 1
    fi
    echo "Verified api.forkmesh.com compatibility on forkmesh-relay."
}

deploy_app_target() {
    require_edge_router_public_key
    if [ "${FORKMESH_FORCE_DEPLOY:-0}" != "1" ]; then
        if deploy_target_is_changed app; then
            :
        else
            local changed_status=$?
            if [ "$changed_status" = "3" ]; then
                echo "Skipping app: its Worker inputs have not changed since the last successful deploy."
                return 0
            fi
            return "$changed_status"
        fi
    fi
    prepare_target_deploy app
    authorize_router_rotation
    push_secrets validate
    if signal_deploy_status deploying "$BUILD_REV"; then
        DEPLOY_SIGNAL_ACTIVE=1
        trap mark_interrupted_deploy EXIT
    else
        echo "note: deploy semaphore is not available before migrations; retrying after schema setup." >&2
    fi
    ./migrate.sh
    if [ "$DEPLOY_SIGNAL_ACTIVE" != "1" ]; then
        if ! signal_deploy_status deploying "$BUILD_REV"; then
            echo "ERROR: deploy semaphore is unavailable after migrations." >&2
            return 1
        fi
        DEPLOY_SIGNAL_ACTIVE=1
        trap mark_interrupted_deploy EXIT
    fi
    echo "Deploying forkmesh-relay as the App, API, Git, and Durable Object owner (build $BUILD_REV)..."
    deploy_app_version
    if [ "$ROUTER_ROTATION_ACTIVE" = "1" ]; then
        echo "Disabling the public mirror-router identity before publishing the new signing pair..."
        deploy_edge_control_version ""
        verify_router_identity_disabled
        push_secrets
    else
        verify_live_router_public_key "$EDGE_ROUTER_PUBLIC_KEY" \
            "$(edge_control_verify_url)"
        push_secrets publish-preserve-router
    fi
    verify_deploy "$BUILD_REV" "/api/mainnode" \
        "${DEPLOY_VERIFY_URL:-https://app.forkmesh.com}" \
        "$DEPLOY_TARGET_FINGERPRINT" "$EDGE_ROUTER_PUBLIC_KEY"
    echo "Deploying the runtime-independent App health and identity edge routes..."
    deploy_edge_control_version
    verify_deploy "$BUILD_REV" "/api/version" "$(edge_control_verify_url)" \
        "$DEPLOY_TARGET_FINGERPRINT"
    verify_edge_control
    if command -v curl >/dev/null 2>&1; then
        local base="${DEPLOY_VERIFY_URL:-https://app.forkmesh.com}"
        local version landing_status
        version="$(curl -sS --max-time 25 "${base%/}/api/version" 2>/dev/null || true)"
        if ! grep -Eq '"worker"[[:space:]]*:[[:space:]]*"app"' <<<"$version"; then
            echo "ERROR: ${base%/} is not reporting the app worker role." >&2
            return 1
        fi
        if ! grep -Eq '"deployFingerprint"[[:space:]]*:[[:space:]]*"'"$DEPLOY_TARGET_FINGERPRINT"'"' <<<"$version"; then
            echo "ERROR: ${base%/} is not reporting the deployed App fingerprint." >&2
            return 1
        fi
        landing_status="$(curl -sS -o /dev/null -w '%{http_code}' --max-time 25 "${base%/}/api" 2>/dev/null || true)"
        if [ "$landing_status" != "200" ]; then
            echo "ERROR: ${base%/}/api returned HTTP ${landing_status:-<none>}." >&2
            return 1
        fi
    fi
    if official_multi_host_enabled; then
        verify_legacy_api_alias
    fi
    signal_deploy_status ready "$BUILD_REV"
    DEPLOY_SIGNAL_ACTIVE=0
    trap - EXIT
    mark_deploy_target app
    echo "Done: forkmesh-relay owns the App, API, Git protocols, and Durable Objects."
}

retire_legacy_api_worker() {
    if [ "${FORKMESH_RETIRE_LEGACY_API:-0}" != "1" ]; then
        echo "Keeping forkmesh-api until explicit post-cutover retirement is requested."
        return 0
    fi
    verify_legacy_api_alias
    local output status=0
    output="$(pywrangler delete --name forkmesh-api --force 2>&1)" || status=$?
    if [ "$status" = "0" ]; then
        printf '%s\n' "$output"
        echo "Retired legacy Worker: forkmesh-api."
        return 0
    fi
    if grep -Eiq 'not found|does not exist|could not find|10007' <<<"$output"; then
        return 0
    fi
    printf '%s\n' "$output" >&2
    return "$status"
}

publish_preserving_router_identity() {
    if ! command -v curl >/dev/null 2>&1; then
        echo "ERROR: curl is required to preserve and verify the live edge router identity." >&2
        return 1
    fi
    local base headers marker preserved_key
    base="$(edge_control_verify_url)"
    headers="$(curl -sSI --max-time 25 "${base%/}/api/version" 2>/dev/null || true)"
    marker="$(printf '%s\n' "$headers" | awk -F': *' 'tolower($1) == "x-forkmesh-edge-control" { value=$2 } END { sub(/\r$/, "", value); print value }')"
    if [ "$marker" != "active" ]; then
        echo "ERROR: the App edge-control route is not active; generic secret publication is unsafe." >&2
        echo "       Deploy the complete App before updating non-router secrets." >&2
        return 1
    fi
    if ! preserved_key="$(live_router_public_key "$base")"; then
        echo "ERROR: the live mirror-router public identity could not be preserved safely." >&2
        return 1
    fi
    push_secrets publish-preserve-router
    verify_live_router_public_key "$preserved_key" "$base"
}

deploy_changed_workers() {
    if ! official_multi_host_enabled; then
        "$0" app
        if [ "${FORKMESH_DEPLOY_WORLD:-0}" = "1" ]; then
            "$0" world
        fi
        return
    fi
    "$0" app
    "$0" world
    retire_legacy_api_worker
}

case "${1:-deploy}" in
    deploy)
        deploy_changed_workers
        echo "Done: app and world were checked in migration-safe order."
        ;;
    changed)
        deploy_changed_workers
        echo "Done: every changed Worker has been deployed; unchanged Workers were skipped."
        ;;
    app)
        deploy_app_target
        ;;
    rotate-mirror-router)
        FORKMESH_ALLOW_ROUTER_KEY_ROTATION=1 FORKMESH_FORCE_DEPLOY=1 "$0" app
        ;;
    world)
        deploy_static_target "$1"
        ;;
    status)
        shift
        python3 tools/deploy_targets.py status "$@"
        ;;
    post-deploy-verify)
        python3 tools/verify_split_deployment.py
        ;;
    republish-release-binary)
        source_rev="$(git rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
        if publish_release_binary 1; then
            commit_release_metadata
            echo "Release binary republished from source commit $source_rev."
            echo "Push the release metadata commit, then refresh mirror catalogs before using the fleet install button."
        else
            exit 1
        fi
        ;;
    secrets)
        require_cloudflare_account
        require_cloudflare_auth
        publish_preserving_router_identity
        ;;
    validate-secrets)
        require_cloudflare_account
        require_cloudflare_auth
        require_edge_router_public_key
        push_secrets validate
        ;;
    dev)
        build_dashboard_assets
        pywrangler dev --env "" ${VAR_ARGS[@]+"${VAR_ARGS[@]}"}
        ;;
    dry-run)
        target="${2:-app}"
        case "$target" in
            app)
                build_dashboard_assets
                python3 tools/build_site_assets.py app
                pywrangler deploy --env "" --dry-run
                (
                    cd edge-control
                    npm exec --yes --package "${WRANGLER_NPM_SPEC:-wrangler@4.120.0}" -- \
                        wrangler deploy --config wrangler.toml --dry-run
                )
                ;;
            world)
                (
                    cd "../$target"
                    npm exec --yes --package "${WRANGLER_NPM_SPEC:-wrangler@4.120.0}" -- \
                        wrangler deploy --config wrangler.toml --dry-run
                )
                ;;
            *)
                echo "Unknown dry-run target: $target" >&2
                exit 2
                ;;
        esac
        ;;
    *)
        echo "Usage: $0 [deploy|changed|app|rotate-mirror-router|world|status|post-deploy-verify|republish-release-binary|secrets|validate-secrets|dev|dry-run [target]]" >&2
        exit 2
        ;;
esac
