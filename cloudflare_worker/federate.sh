#!/usr/bin/env bash




















set -u
cd "$(dirname "$0")"

ORIGIN="https://forkmesh.com"
USER_HANDLE=""
REPO_PATH=""
FAILS=0
CHECKS=0
BODY=""
HTTP_STATUS="000"

say()  { printf '%s\n' "$*"; }
pass() { CHECKS=$((CHECKS + 1)); say "  PASS  $1"; }
fail() { CHECKS=$((CHECKS + 1)); FAILS=$((FAILS + 1)); say "  FAIL  $1${2:+ — $2}"; }



fetch() {
    local url="$1" accept="${2:-}" tmp
    tmp="$(mktemp)"
    if [ -n "$accept" ]; then
        HTTP_STATUS="$(curl -sS -o "$tmp" -w '%{http_code}' -H "Accept: $accept" "$url" 2>/dev/null || echo 000)"
    else
        HTTP_STATUS="$(curl -sS -o "$tmp" -w '%{http_code}' "$url" 2>/dev/null || echo 000)"
    fi
    BODY="$(cat "$tmp")"
    rm -f "$tmp"
}



jget() {
    printf '%s' "$BODY" | python3 -c '
import json, sys
try:
    d = json.load(sys.stdin)
    v = eval(sys.argv[1])
    print("" if v is None else v)
except Exception:
    pass' "$1" 2>/dev/null
}

pick_sample() {


    fetch "$ORIGIN/api/repositories"
    if [ -z "$REPO_PATH" ]; then
        REPO_PATH="$(jget 'd["repositories"][0]["owner"] + "/" + d["repositories"][0]["name"]')"
    fi
    if [ -z "$USER_HANDLE" ]; then


        local domain="${ORIGIN#*://}" owner
        for owner in $(jget '" ".join(dict.fromkeys(r["owner"] for r in d["repositories"][:25]))'); do
            fetch "$ORIGIN/.well-known/webfinger?resource=acct:$owner@$domain"
            if [ "$HTTP_STATUS" = 200 ]; then USER_HANDLE="$owner"; break; fi
        done
    fi
}

verify() {
    local domain="${ORIGIN#*://}"
    say "Verifying fediverse surface of $ORIGIN"


    fetch "$ORIGIN/api/version"
    if [ "$HTTP_STATUS" = 200 ]; then pass "/api/version (rev $(jget 'd["rev"][:12]'))"
    else fail "/api/version" "HTTP $HTTP_STATUS — is the Worker deployed?"; return; fi

    pick_sample
    [ -n "$REPO_PATH" ] || say "  note: no public repos in catalog; repo-actor checks will be skipped"
    [ -n "$USER_HANDLE" ] || say "  note: no user-kind account found among catalog owners; user-actor checks skipped (node accounts correctly have no Person actor)"
    local owner="${REPO_PATH%%/*}" repo="${REPO_PATH#*/}"


    fetch "$ORIGIN/.well-known/nodeinfo"
    case "$(jget 'd["links"][0]["href"]')" in
        "$ORIGIN"/nodeinfo/2.1) pass "/.well-known/nodeinfo";;
        *) fail "/.well-known/nodeinfo" "HTTP $HTTP_STATUS";;
    esac
    fetch "$ORIGIN/nodeinfo/2.1"
    if [ "$(jget 'd["software"]["name"]')" = "forkmesh" ] \
       && [ "$(jget 'd["protocols"][0]')" = "activitypub" ]; then
        pass "/nodeinfo/2.1 (activitypub advertised)"
    else fail "/nodeinfo/2.1" "HTTP $HTTP_STATUS"; fi


    if [ -n "$USER_HANDLE" ]; then
        fetch "$ORIGIN/.well-known/webfinger?resource=acct:$USER_HANDLE@$domain"
        if [ "$(jget 'd["subject"]')" = "acct:$USER_HANDLE@$domain" ]; then
            pass "webfinger @$USER_HANDLE@$domain"
        else fail "webfinger @$USER_HANDLE@$domain" "HTTP $HTTP_STATUS"; fi

        fetch "$ORIGIN/ap/users/$USER_HANDLE" application/activity+json
        if [ "$(jget 'd["preferredUsername"]')" = "$USER_HANDLE" ] \
           && [ -n "$(jget 'd["publicKey"]["publicKeyPem"]')" ]; then
            pass "actor /ap/users/$USER_HANDLE (RSA key published)"
        else fail "actor /ap/users/$USER_HANDLE" "HTTP $HTTP_STATUS"; fi


        fetch "$ORIGIN/@$USER_HANDLE" application/activity+json
        if [ "$(jget 'd["id"]')" = "$ORIGIN/ap/users/$USER_HANDLE" ]; then
            pass "content negotiation on /@$USER_HANDLE"
        else fail "content negotiation on /@$USER_HANDLE" "HTTP $HTTP_STATUS"; fi
    fi

    if [ -n "$REPO_PATH" ]; then
        fetch "$ORIGIN/.well-known/webfinger?resource=acct:$owner.$repo@$domain"
        if [ "$(jget 'd["subject"]')" = "acct:$owner.$repo@$domain" ]; then
            pass "webfinger @$owner.$repo@$domain"
        else fail "webfinger @$owner.$repo@$domain" "HTTP $HTTP_STATUS"; fi

        fetch "$ORIGIN/ap/repos/$owner/$repo" application/activity+json
        if [ "$(jget 'd["type"]')" = "Group" ] \
           && [ -n "$(jget 'd["publicKey"]["publicKeyPem"]')" ]; then
            pass "actor /ap/repos/$owner/$repo (Group, RSA key published)"
        else fail "actor /ap/repos/$owner/$repo" "HTTP $HTTP_STATUS"; fi

        fetch "$ORIGIN/ap/repos/$owner/$repo/followers" application/activity+json
        if [ "$(jget 'd["type"]')" = "OrderedCollection" ]; then
            pass "followers collection ($(jget 'd["totalItems"]') followers)"
        else fail "followers collection" "HTTP $HTTP_STATUS"; fi

        fetch "$ORIGIN/api/repo/$owner/$repo/fedi-comments?kind=issue&number=1"
        if [ "$(jget 'd["ok"]')" = "True" ]; then
            pass "fedi-comments API"
        else fail "fedi-comments API" "HTTP $HTTP_STATUS"; fi
    fi


    HTTP_STATUS="$(curl -sS -o /dev/null -w '%{http_code}' -X POST \
        -H 'Content-Type: application/activity+json' \
        -d '{"type":"Follow","actor":"https://example.com/u/x"}' \
        "$ORIGIN/ap/inbox" 2>/dev/null || echo 000)"
    if [ "$HTTP_STATUS" = 401 ]; then pass "shared inbox rejects unsigned POSTs (401)"
    else fail "shared inbox signature enforcement" "expected 401, got $HTTP_STATUS"; fi

    say ""
    if [ "$FAILS" = 0 ]; then
        say "All $CHECKS checks passed — $domain is live on the fediverse."
        [ -n "$REPO_PATH" ] && say "From any Mastodon account, search:  @$owner.$repo@$domain   and follow it."
    else
        say "$FAILS of $CHECKS checks FAILED."
    fi
}

cmd="${1:-verify}"
case "$cmd" in
    verify|deploy) shift || true;;
    comments)
        shift
        rp="${1:?usage: federate.sh comments owner/repo kind number [origin]}"
        kind="${2:?kind (issue|pull|discussion|commit|release)}"
        num="${3:?number/ref}"
        ORIGIN="${4:-$ORIGIN}"
        fetch "$ORIGIN/api/repo/${rp%%/*}/${rp#*/}/fedi-comments?kind=$kind&number=$num"
        printf '%s\n' "$BODY"
        exit 0;;
    -*|http*) cmd=verify;;
    *) say "unknown command: $cmd"; exit 2;;
esac
while [ $# -gt 0 ]; do
    case "$1" in
        --user) USER_HANDLE="$2"; shift 2;;
        --repo) REPO_PATH="$2"; shift 2;;
        http*) ORIGIN="${1%/}"; shift;;
        *) say "unknown arg: $1"; exit 2;;
    esac
done

if [ "$cmd" = deploy ]; then
    ./deploy.sh || exit $?
fi
verify
exit "$FAILS"
