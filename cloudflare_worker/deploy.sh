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

# ForkMesh injects every Settings -> Secrets & Coves variable into this run's
# environment under the exact name you gave it, and wrangler only authenticates
# with CLOUDFLARE_API_TOKEN. So if the token was saved under a common near-miss
# name (CF_API_TOKEN / CLOUDFLARE_TOKEN / CF_TOKEN), promote it to
# CLOUDFLARE_API_TOKEN here rather than failing the deploy over a naming
# mismatch. Only runs when CLOUDFLARE_API_TOKEN is empty, and announces the
# rename so the source is never a mystery.
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

# Wrangler needs Cloudflare credentials to touch the API. An explicit
# CLOUDFLARE_API_TOKEN (exported from $ENV_FILE above) always works; without one
# wrangler falls back to an interactive OAuth login, which only works at a TTY.
# In a non-interactive environment (CI, a headless box, an agent) that fallback
# fails — and only AFTER the [build] command has already run migrate.sh — with a
# terse "Failed to fetch auth token: 400 Bad Request / set a CLOUDFLARE_API_TOKEN".
# Catch it up front with actionable guidance instead. The TTY test mirrors
# wrangler's own interactive detection (stdin && stdout), so this errors exactly
# when wrangler would have, never sooner.
require_cloudflare_auth() {
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
    # The token must be named EXACTLY CLOUDFLARE_API_TOKEN (or one of the aliases
    # adopted above). A token saved under any other name is invisible to wrangler,
    # so surface the Cloudflare-ish variable names we CAN see — this is what turns
    # "but I added the token!" into "oh, I named it the wrong thing."
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

# The human-readable release version, read from the desktop app's authoritative
# source (qt_client/CMakeLists.txt: `project(ForkMesh VERSION x.y.z ...)`) so the
# website header shows the SAME version as the app and picks up a new number
# automatically whenever a release bumps that line and redeploys. Passed to the
# Worker as the APP_VERSION var (echoed back by /api/version). Empty if it can't
# be parsed, in which case the header simply omits the version chip.
app_version() {
    local cmake="../qt_client/CMakeLists.txt"
    [ -f "$cmake" ] || return 0
    sed -n 's/^project(ForkMesh VERSION \([0-9][0-9.]*\).*/\1/p' "$cmake" | head -n1
}

# Fallback HTTP GET for when curl itself is broken. Seen live (adhoc #136): a
# host application-firewall rule that singles out the curl binary (an OpenSnitch
# "deny process.path /usr/bin/curl" answered on a popup) blackholes every curl
# request — including its DNS — so verification dies with curl exit 28 while the
# network, the deploy, and the site are all fine. python3's sockets don't match
# a per-binary curl rule, so retry the same GET through it before treating a
# connection-level curl failure as "the origin is down". Output mirrors
# curl -w '\n%{http_code}': body, newline, status code. The explicit User-Agent
# matters: the edge 403s python's default UA as a bot.
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

# HEAD-style fallback for the asset checks, same rationale as _py_http_get.
# Prints "<status> <content-type>".
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
    # 30 attempts * (up to 25s request + 6s sleep) gives a long runway before
    # calling this a real failure. This Worker is a ~10k-line Python
    # (Pyodide) module, and Cloudflare Python Workers are known to have much
    # slower cold starts than JS Workers while the isolate compiles/loads the
    # runtime on the FIRST hit of a freshly-deployed version at each colo — a
    # too-tight per-request timeout here previously made the loop time out
    # (curl exit 28) before the Worker ever got a chance to answer, reporting
    # a false "never went live" even though the deploy had, in fact, landed.
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
        # Pull "rev":"<value>" out of the JSON without needing jq.
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

# After verify_deploy confirms the Worker is live, prove the public static assets
# are actually served (correct HTTP status + content-type). A broken assets upload
# or a misrouted path silently serves the 404 page (text/html) in place of an
# image, so we check a representative set and FAIL LOUDLY rather than ship a
# landing page with missing logo/video. Override the origin with DEPLOY_VERIFY_URL.
verify_public_assets() {
    local base="${DEPLOY_VERIFY_URL:-https://forkmesh.com}"
    base="${base%/}"
    if ! command -v curl >/dev/null 2>&1; then
        echo "note: curl not found — skipping public asset verification." >&2
        return 0
    fi

    # "<path> <expected-content-type-prefix>". The .webmanifest entry is matched
    # leniently below because Cloudflare may serve it as application/json.
    local checks=(
        "/assets/logo.png image/png"
        "/favicon/favicon-32x32.png image/png"
        "/favicon/site.webmanifest application/manifest+json"
        "/assets/video/network.jpg image/jpeg"
        "/assets/video/network.mp4 video/mp4"
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
        # curl got no response at all (vs an HTTP error): same per-binary
        # firewall blind spot as in verify_deploy — retry through python3.
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
    echo "Verified: public static assets are serving expected content types."
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
    # MAILTRAP_API_TOKEN sends signup confirmation emails - required here so a
    # production deploy is guaranteed to push and register it (the Worker still
    # no-ops gracefully if it's ever unset). A fork that doesn't send email can
    # drop it from this list.
    local required=" ADMIN_PATH TREASURY_SOLANA_ADDRESS MAILTRAP_API_TOKEN "

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
        printf '%s\n' "$value" | pywrangler secret put --env "" "$key"
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

# Build and publish a prebuilt release binary for the current platform. This
# makes install.sh downloads fast (no recompile) instead of falling back to a
# full source build. Silently skips if already published for this platform.
publish_release_binary() {
    if ! command -v cmake >/dev/null 2>&1; then
        echo "note: cmake not found — skipping release binary build." >&2
        return 0
    fi

    # Detect current platform (same logic as release.yml).
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

    # Check if this asset is already published.
    if [ -f ../releases/latest/SHASUMS256.txt ] && grep -q "  $asset" ../releases/latest/SHASUMS256.txt 2>/dev/null; then
        echo "Release binary for $asset is already published."
        return 0
    fi

    echo "Building and publishing release binary for ${os}/${arch}…"

    # Build the Qt client in Release mode (same as release.yml). Note: we are
    # in cloudflare_worker/ so qt_client is at ../qt_client.
    local jobs
    jobs="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )"
    cmake -S ../qt_client -B ../qt_client/build-release \
        -DCMAKE_BUILD_TYPE=Release -DFORKMESH_BUILD_TESTS=OFF 2>&1 | grep -v "^--" || true
    if ! cmake --build ../qt_client/build-release -j"$jobs" 2>&1 | tail -5; then
        echo "ERROR: failed to build release binary." >&2
        return 1
    fi

    # Locate the built executable.
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

    # Publish via content-addressed store and write release metadata.
    cp "$built" "$asset"
    chmod 0755 "$asset" || true
    mkdir -p "${FORKMESH_RELEASE_CAS:-.forkmesh/release-blobs}"
    local publish_output
    if ! publish_output="$(../tools/forkmesh-release-publish.sh \
        --channel latest \
        --tag "${FORKMESH_TAG:-}" \
        --cas-dir "${FORKMESH_RELEASE_CAS:-.forkmesh/release-blobs}" \
        ${FORKMESH_REPO:+--repo "$FORKMESH_REPO"} \
        "$asset" 2>&1)"; then
        echo "ERROR: failed to publish release binary." >&2
        return 1
    fi
    printf '%s\n' "$publish_output" | tail -3
    rm -f "$asset"

    # Stage the release metadata for commit.
    git add ../releases/latest/SHASUMS256.txt ../releases/latest/release.json || return 1
}

# Commit release metadata changes if any were staged.
commit_release_metadata() {
    if git diff --quiet --cached ../releases/latest/ 2>/dev/null; then
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
        BUILD_REV="$(build_rev)"
        APP_VERSION="$(app_version)"
        echo "Deploying ForkMesh website + relay to Cloudflare (build $BUILD_REV, version ${APP_VERSION:-unknown})..."
        # wrangler.toml defines [env.dev] alongside the top-level (production)
        # config, so wrangler warns "no target environment specified" unless we
        # pass --env explicitly. Every pywrangler call below that touches the
        # live Worker passes --env "" (the documented way to target the
        # top-level environment) so the warning goes away and every command
        # (deploy, secret put/list, dev, dry-run) consistently hits the same
        # script instead of drifting between an implicit and explicit target.
        # Stamp the build into the Worker as a plaintext var so /api/version can
        # report it. --var is MERGED with wrangler.toml [vars] (it does not wipe
        # them) and we re-pass it every deploy, so it persists; secrets are
        # untouched. This is the marker verify_deploy checks below.
        pywrangler deploy --env "" --var "BUILD_REV:${BUILD_REV}" --var "APP_VERSION:${APP_VERSION}"
        # Secrets are set after the Worker exists; unlike plaintext vars they
        # survive this and future deploys, so the admin dashboard keeps working.
        # push_secrets
        # Prove the public origin is actually serving what we just uploaded. A
        # failed/no-op/wrong-account deploy now aborts here instead of printing a
        # phantom success.
        verify_deploy "$BUILD_REV"
        verify_public_assets

        # Build and publish a prebuilt release binary for install.sh to find.
        # This is optional: if it fails, the deploy still succeeds (users can build
        # from source), but install.sh will be much faster with prebuilts.
        echo
        if publish_release_binary; then
            commit_release_metadata
        else
            echo "note: prebuilt binary build failed, but deploy succeeded." >&2
            echo "      install.sh will fall back to building from source." >&2
        fi
        echo "Done. Live at https://forkmesh.com (and any custom domain)."
        ;;
    secrets)
        require_cloudflare_account
        require_cloudflare_auth
        # Re-push just the .env.production secrets, no full redeploy.
        push_secrets
        ;;
    dev)
        pywrangler dev --env "" ${VAR_ARGS[@]+"${VAR_ARGS[@]}"}
        ;;
    dry-run)
        require_cloudflare_account
        # --dry-run still runs the [build] command (migrate.sh → remote D1), which
        # needs Cloudflare auth, so the same non-interactive guard applies.
        require_cloudflare_auth
        pywrangler deploy --env "" --dry-run
        ;;
    *)
        echo "Usage: $0 [deploy|secrets|dev|dry-run]" >&2
        exit 2
        ;;
esac
