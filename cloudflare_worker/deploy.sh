#!/usr/bin/env bash


















set -euo pipefail
cd "$(dirname "$0")"

. ./pywrangler.sh










ENV_FILE=".env.production"




trim() {
    local s="$1"
    s="${s%$'\r'}"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}




VAR_ARGS=()
CF_ACCOUNT_ID_SET=0
if [ -f "$ENV_FILE" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in ''|'#'*) continue ;; esac
        case "$line" in *=*) ;; *) continue ;; esac
        key="$(trim "${line%%=*}")"
        value="$(trim "${line#*=}")"
        [ -z "$key" ] && continue





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
        VAR_ARGS+=(--var "${key}:${value}")
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
    adopt_cloudflare_token_alias || true
    [ -n "${CLOUDFLARE_API_TOKEN:-}" ] && return 0
    if [ -t 0 ] && [ -t 1 ]; then
        return 0
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
    local cmake="../qt_client/CMakeLists.txt"
    [ -f "$cmake" ] || return 0
    sed -n 's/^project(ForkMesh VERSION \([0-9][0-9.]*\).*/\1/p' "$cmake" | head -n1
}



signal_world_deploy() {
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
mark_interrupted_world_deploy() {
    local exit_code=$?
    if [ "$DEPLOY_SIGNAL_ACTIVE" = "1" ] && [ -n "${BUILD_REV:-}" ]; then
        signal_world_deploy failed "$BUILD_REV" >/dev/null 2>&1 || true
    fi
    return "$exit_code"
}

build_dashboard_assets() {
    python3 tools/build_dashboard_assets.py
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



_py_http_head() {
    command -v python3 >/dev/null 2>&1 || return 1
    python3 - "$1" <<'PYEOF'
import sys, urllib.request
req = urllib.request.Request(sys.argv[1], method="HEAD", headers={
    "User-Agent": "forkmesh-deploy-verify/1.0"})
try:
    with urllib.request.urlopen(req, timeout=15) as r:
        print(r.status, (r.headers.get("content-type") or "").lower())
except Exception as e:
    code = getattr(e, "code", None)
    if code is None:
        sys.exit(1)
    print(code, (getattr(e, "headers", None) or {}).get("content-type", "").lower())
PYEOF
}







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








    local attempts=30 max_time=25 sleep_s=6
    local attempt body got http_code curl_rc via
    for attempt in $(seq 1 "$attempts"); do
        curl_rc=0
        via=curl
        body="$(curl -sS --max-time "$max_time" -w $'\n%{http_code}' "$url" 2>/dev/null)" || curl_rc=$?
        if [ "$curl_rc" != 0 ] && body="$(_py_http_get "$url")"; then
            curl_rc=0
            via=python3
        fi
        http_code="${body##*$'\n'}"
        body="${body%$'\n'*}"

        got="$(printf '%s' "$body" | sed -n 's/.*"rev"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p')"
        if [ "$curl_rc" = 0 ] && [ "$http_code" = "200" ] && [ "$got" = "$expected" ]; then
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
            echo "  attempt $attempt/$attempts: HTTP $http_code, live rev='${got:-<none>}' (want '$expected', via $via); retrying in ${sleep_s}s..." >&2
        fi
        sleep "$sleep_s"
    done
    echo "ERROR: $base never reported BUILD_REV=$expected after the deploy." >&2
    if [ "$curl_rc" != 0 ]; then
        echo "       Last attempt failed to connect (curl exit $curl_rc) — check network egress" >&2
        echo "       from the deploy runner and that forkmesh.com resolves and is reachable." >&2
    elif [ "$http_code" != "200" ]; then
        echo "       Last attempt got HTTP $http_code from $url (expected 200). That's a" >&2
        echo "       server-side/routing error, not a stale-code mismatch — check the Worker's" >&2
        echo "       error log (admin dashboard) for what's failing on that origin." >&2
    else
        echo "       Last live rev was '${got:-<none>}'. The upload did NOT take effect on" >&2
        echo "       this origin (most likely it hit the wrong Cloudflare account, or the" >&2
        echo "       custom domain still routes to an old Worker). Check that" >&2
        echo "       CLOUDFLARE_ACCOUNT_ID in $ENV_FILE matches the account that owns" >&2
        echo "       forkmesh.com, then redeploy." >&2
    fi
    return 1
}






verify_public_assets() {
    local base="${DEPLOY_VERIFY_URL:-https://forkmesh.com}"
    base="${base%/}"
    if ! command -v curl >/dev/null 2>&1; then
        echo "note: curl not found — skipping public asset verification." >&2
        return 0
    fi



    local checks=(
        "/assets/logo.png image/png"
        "/favicon/favicon-32x32.png image/png"
        "/favicon/site.webmanifest application/manifest+json"
        "/assets/video/network.jpg image/jpeg"
        "/assets/video/network.mp4 video/mp4"
        "/assets/blog/features/forkmesh-forever.webp image/webp"
        "/assets/video/forkmesh-forever.mp4 video/mp4"
        "/assets/video/forkmesh-forever.en.vtt text/vtt"
        "/assets/music/cosmic-waves.ogg audio/ogg"
        "/dashboard/tailwind.css text/css"
    )

    echo "Verifying public static assets on $base ..."
    local check path expected url headers status content_type ok failed=0
    for check in "${checks[@]}"; do
        path="${check%% *}"
        expected="${check#* }"
        url="$base$path"
        headers="$(curl -sSI --max-time 15 "$url" 2>/dev/null || true)"
        status="$(
            printf '%s\n' "$headers" |
                awk 'toupper($1) ~ /^HTTP\// { code=$2 } END { print code }'
        )"
        content_type="$(
            printf '%s\n' "$headers" |
                awk -F': *' 'tolower($1) == "content-type" { value=tolower($2) } END { sub(/\r$/, "", value); print value }'
        )"


        if [ -z "$status" ]; then
            read -r status content_type <<<"$(_py_http_head "$url" || true)"
        fi

        if [ "$status" != "200" ]; then
            echo "ERROR: $url returned HTTP ${status:-<none>} (expected 200)." >&2
            failed=1
            continue
        fi
        ok=0
        case "$content_type" in
            "$expected"*) ok=1 ;;
        esac
        if [ "$ok" = 0 ] && [ "$path" = "/favicon/site.webmanifest" ]; then
            case "$content_type" in
                application/json*|application/manifest+json*) ok=1 ;;
            esac
        fi
        if [ "$ok" = 0 ]; then
            echo "ERROR: $url returned content-type '${content_type:-<none>}' (expected $expected)." >&2
            failed=1
        fi
    done

    if [ "$failed" != "0" ]; then
        echo "       Static assets did not publish correctly. Check the Worker assets" >&2
        echo "       upload (and not_found_handling routing) and redeploy." >&2
        return 1
    fi










    local world_checks=(
        "/world/world.js|public/world/world.js"
        "/world/world-data.js|public/world/world-data.js"
        "/world/world-scene.js|public/world/world-scene.js"
        "/world/world-mirror-nodes.js|public/world/world-mirror-nodes.js"
        "/world/world-mastodon.js|public/world/world-mastodon.js"
        "/world/world-pull-review.js|public/world/world-pull-review.js"
        "/world/world-repository-graph.js|public/world/world-repository-graph.js"
        "/world/world.css|public/world/world.css"
    )
    local local_asset local_hash remote_hash cache_control
    local asset_attempt asset_attempts=15 asset_retry_s=2
    for check in "${world_checks[@]}"; do
        path="${check%%|*}"
        local_asset="${check#*|}"
        url="$base$path?deploy-rev=$BUILD_REV"
        if command -v sha256sum >/dev/null 2>&1; then
            local_hash="$(sha256sum "$local_asset" | awk '{print $1}')"
        else
            local_hash="$(shasum -a 256 "$local_asset" | awk '{print $1}')"
        fi
        remote_hash=""




        for ((asset_attempt = 1; asset_attempt <= asset_attempts; asset_attempt++)); do
            if command -v sha256sum >/dev/null 2>&1; then
                remote_hash="$(
                    curl -fsS --max-time 30 \
                        "$url&verify-attempt=$asset_attempt" |
                        sha256sum |
                        awk '{print $1}'
                )"
            else
                remote_hash="$(
                    curl -fsS --max-time 30 \
                        "$url&verify-attempt=$asset_attempt" |
                        shasum -a 256 |
                        awk '{print $1}'
                )"
            fi
            if [ "$remote_hash" = "$local_hash" ]; then
                break
            fi
            if [ "$asset_attempt" -lt "$asset_attempts" ]; then
                echo "  $path is still converging at the edge (attempt $asset_attempt/$asset_attempts); retrying in ${asset_retry_s}s..." >&2
                sleep "$asset_retry_s"
            fi
        done
        if [ "$remote_hash" != "$local_hash" ]; then
            echo "ERROR: $url is not the World asset from BUILD_REV=$BUILD_REV." >&2
            echo "       Local sha256=$local_hash; live sha256=$remote_hash." >&2
            failed=1
        fi
        headers="$(curl -sSI --max-time 15 "$url" 2>/dev/null || true)"
        cache_control="$(
            printf '%s\n' "$headers" |
                awk -F': *' 'tolower($1) == "cache-control" { value=tolower($2) } END { sub(/\r$/, "", value); print value }'
        )"
        case "$cache_control" in
            *no-store*) ;;
            *)
                echo "ERROR: $url permits stale browser reuse ('$cache_control')." >&2
                failed=1
                ;;
        esac
    done
    headers="$(curl -sSI --max-time 15 "$base/world?deploy-rev=$BUILD_REV" 2>/dev/null || true)"
    cache_control="$(
        printf '%s\n' "$headers" |
            awk -F': *' 'tolower($1) == "cache-control" { value=tolower($2) } END { sub(/\r$/, "", value); print value }'
    )"
    case "$cache_control" in
        *no-store*) ;;
        *)
            echo "ERROR: $base/world permits stale browser shell reuse ('$cache_control')." >&2
            failed=1
            ;;
    esac
    if [ "$failed" != "0" ]; then
        echo "       World refresh freshness verification failed; deployment is incomplete." >&2
        return 1
    fi
    echo "Verified: public static assets are serving expected content types."
    echo "Verified: World runtime assets exactly match $BUILD_REV and are browser no-store."
}






retire_legacy_marketing_worker() {
    local output rc=0
    echo "Retiring legacy forkmesh-marketing Worker (if present) ..."
    output="$(pywrangler delete --name forkmesh-marketing --force 2>&1)" || rc=$?
    if [ "$rc" = 0 ]; then
        printf '%s\n' "$output"
        echo "Retired: forkmesh-marketing routes now resolve through forkmesh-relay."
        return 0
    fi
    if grep -Eiq \
        'not found|does not exist|could not find|workers script.*10007|code: 10007' \
        <<<"$output"; then
        echo "Already retired: forkmesh-marketing does not exist."
        return 0
    fi
    printf '%s\n' "$output" >&2
    echo "ERROR: could not retire forkmesh-marketing; refusing a split deployment." >&2
    return "$rc"
}





verify_marketing_routes() {
    local base="${DEPLOY_VERIFY_URL:-https://forkmesh.com}"
    base="${base%/}"
    if ! command -v curl >/dev/null 2>&1; then
        echo "note: curl not found — skipping marketing route verification." >&2
        return 0
    fi

    local checks=(
        "/|ForkMesh - Local-first source code preservation"
        "/pricing|ForkMesh Pricing - Coding Reimagined for Teams"
        "/blog|Blog · ForkMesh"
        "/blog/introducing-forkmesh/|Introducing ForkMesh"
    )
    local check path marker body status failed=0
    echo "Verifying consolidated World and marketing routes on $base ..."
    for check in "${checks[@]}"; do
        path="${check%%|*}"
        marker="${check#*|}"
        body="$(curl -sS --max-time 25 -w $'\n%{http_code}' "$base$path" 2>/dev/null || true)"
        status="${body##*$'\n'}"
        body="${body%$'\n'*}"
        if [ "$status" != "200" ] || ! grep -Fq "$marker" <<<"$body"; then
            echo "ERROR: $base$path failed consolidated route verification" >&2
            echo "       (HTTP ${status:-<none>}; expected marker: $marker)." >&2
            failed=1
        fi
    done
    if [ "$failed" != "0" ]; then
        echo "       The main Worker must own /, /pricing, /blog and posts before" >&2
        echo "       the legacy marketing Worker/routes are retired." >&2
        return 1
    fi
    echo "Verified: one Worker serves the World, pricing, blog index and posts."
}







push_secrets() {
    local action="${1:-publish}"
    case "$action" in
        validate|publish) ;;
        *)
            echo "ERROR: internal push_secrets action must be validate or publish." >&2
            return 2
            ;;
    esac
    if [ ! -f "$ENV_FILE" ]; then
        echo "ERROR: $ENV_FILE not found; production secrets cannot be validated or published." >&2
        echo "       Copy .env.production.example, supply real values, and keep the file untracked." >&2
        return 1
    fi







    local required=" ADMIN_PATH MAILTRAP_API_TOKEN DATA_KEY TREASURY_SOLANA_ADDRESS MIRROR_ROUTER_PUBLIC_KEY MIRROR_ROUTER_SIGNING_SEED DISCORD_CLIENT_ID DISCORD_CLIENT_SECRET DISCORD_BOT_TOKEN "

    if [ "$action" = "validate" ]; then
        echo "Validating production secrets in: $(cd "$(dirname "$ENV_FILE")" && pwd)/$(basename "$ENV_FILE")"
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




    local req missing_req=()
    for req in $required; do
        case " ${pushed[*]-} " in *" $req "*) ;; *) missing_req+=("$req") ;; esac
    done
    if [ "${#missing_req[@]}" -gt 0 ]; then
        echo "ERROR: required secret(s) empty or missing in $ENV_FILE: ${missing_req[*]}" >&2
        echo "       Set them (real values, not blank) and re-run './deploy.sh secrets'." >&2
        return 1
    fi


    local discord_client_id=""
    local index
    for index in "${!pushed[@]}"; do
        if [ "${pushed[$index]}" = "DISCORD_CLIENT_ID" ]; then
            discord_client_id="${secret_values[$index]}"
            break
        fi
    done
    if ! [[ "$discord_client_id" =~ ^[0-9]{17,20}$ ]]; then
        echo "ERROR: DISCORD_CLIENT_ID must be a 17-20 digit Discord application ID." >&2
        return 1
    fi
    if [ "$action" = "validate" ]; then
        echo "Validated ${#pushed[@]} production secret(s), including Discord bot and OAuth credentials."
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
        pywrangler secret bulk --env "" "$secret_bulk_file"
    ); then
        echo "ERROR: bulk secret update failed; no per-secret retry was attempted." >&2
        return 1
    fi
    echo "Pushed $count secret(s) from $ENV_FILE in one bulk update."



    local listed
    if listed="$(pywrangler secret list --env "" 2>/dev/null)"; then
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




    if [ -f ../.forkmesh/releases/latest/SHASUMS256.txt ] && grep -q "  $asset" ../.forkmesh/releases/latest/SHASUMS256.txt 2>/dev/null; then
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
        echo "ERROR: could not resolve a clean app version from qt_client/CMakeLists.txt." >&2
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
    if ! configure_output="$(cmake -S ../qt_client -B ../qt_client/build-release \
        -DCMAKE_BUILD_TYPE=Release -DFORKMESH_BUILD_TESTS=OFF \
        -DFORKMESH_BUILD_COMMIT_OVERRIDE="$build_commit" \
        -DFORKMESH_VERSION_OVERRIDE="$release_version" 2>&1)"; then
        printf '%s\n' "$configure_output" >&2
        echo "ERROR: failed to configure the release binary." >&2
        return 1
    fi
    printf '%s\n' "$configure_output" | grep -v "^--" || true
    if ! cmake --build ../qt_client/build-release -j"$jobs" 2>&1 | tail -5; then
        echo "ERROR: failed to build release binary." >&2
        return 1
    fi


    local built=""
    for cand in \
        "../qt_client/build-release/forkmesh" \
        "../qt_client/build-release/forkmesh.exe" \
        "../qt_client/build-release/ForkMesh.app/Contents/MacOS/ForkMesh"; do
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
    publish_args+=("cloudflare_worker/$asset")
    if ! publish_output="$(cd .. && tools/forkmesh-release-publish.sh \
        "${publish_args[@]}" 2>&1)"; then
        rm -f "$asset"
        echo "ERROR: failed to publish release binary." >&2
        return 1
    fi
    printf '%s\n' "$publish_output" | tail -3





    local asset_hash
    if command -v sha256sum >/dev/null 2>&1; then
        asset_hash="$(sha256sum "$asset" | awk '{print $1}')"
    else
        asset_hash="$(shasum -a 256 "$asset" | awk '{print $1}')"
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
       [ ! -f ../.forkmesh/releases/latest/release.json.sig ] ||
       [ "$(wc -c < ../.forkmesh/releases/latest/release.json.sig | tr -d ' ')" != "64" ] ||
       [ ! -f "$cas_dir/sha256/${asset_hash:0:2}/$asset_hash/data" ]; then
        rm -f "$asset"
        echo "ERROR: release metadata/CAS verification did not match the freshly built commit and binary." >&2
        return 1
    fi
    echo "Verified release metadata: v${release_version}, tag ${tag_commit:0:12}, build ${build_commit:0:12}, sha256:${asset_hash:0:12}."
    rm -f "$asset"


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

case "${1:-deploy}" in
    deploy)
        require_cloudflare_account
        require_cloudflare_auth



        push_secrets validate
        build_dashboard_assets
        BUILD_REV="$(build_rev)"
        APP_VERSION="$(app_version)"
        DEPLOYED_AT_MS="$(date -u +%s)000"



        if signal_world_deploy deploying "$BUILD_REV"; then
            DEPLOY_SIGNAL_ACTIVE=1
            trap mark_interrupted_world_deploy EXIT
        else
            echo "note: deploy semaphore unavailable before migrations; retrying after schema setup." >&2
        fi
        ./migrate.sh
        if [ "$DEPLOY_SIGNAL_ACTIVE" != "1" ]; then
            if signal_world_deploy deploying "$BUILD_REV"; then
                DEPLOY_SIGNAL_ACTIVE=1
                trap mark_interrupted_world_deploy EXIT
            else
                echo "WARNING: deploy alert semaphore is unavailable; deployment alerts will not be suppressed." >&2
            fi
        fi
        echo "Deploying ForkMesh website + relay to Cloudflare (build $BUILD_REV, version ${APP_VERSION:-unknown})..."











        pywrangler deploy --env "" \
            --var "BUILD_REV:${BUILD_REV}" \
            --var "APP_VERSION:${APP_VERSION}" \
            --var "DEPLOYED_AT_MS:${DEPLOYED_AT_MS}"


        push_secrets



        verify_deploy "$BUILD_REV"
        verify_public_assets
        retire_legacy_marketing_worker
        verify_marketing_routes
        if [ "$DEPLOY_SIGNAL_ACTIVE" = "1" ]; then
            signal_world_deploy ready "$BUILD_REV"
            DEPLOY_SIGNAL_ACTIVE=0
            trap - EXIT
        fi




        echo
        if publish_release_binary 0; then
            commit_release_metadata
        else
            echo "note: prebuilt binary build failed, but deploy succeeded." >&2
            echo "      install.sh will fall back to building from source." >&2
        fi
        echo "Done. Live at https://forkmesh.com (and any custom domain)."
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

        push_secrets
        ;;
    dev)
        build_dashboard_assets
        pywrangler dev --env "" ${VAR_ARGS[@]+"${VAR_ARGS[@]}"}
        ;;
    dry-run)
        require_cloudflare_account


        require_cloudflare_auth
        build_dashboard_assets
        pywrangler deploy --env "" --dry-run
        ;;
    *)
        echo "Usage: $0 [deploy|republish-release-binary|secrets|dev|dry-run]" >&2
        exit 2
        ;;
esac
