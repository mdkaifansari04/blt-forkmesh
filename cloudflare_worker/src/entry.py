import asyncio
import base64
import gzip
import hmac
import io
import json
import re
import struct
import traceback
from urllib.parse import parse_qs, quote, unquote, urlparse

from js import Date
from js import Object
from js import Request as JsRequest
from js import Response as JsResponse
from js import TransformStream
from js import Uint8Array
from js import WebSocketPair
from js import caches as js_caches
from js import crypto as js_crypto
from pyodide.ffi import to_js as _to_js
from workers import DurableObject, Response, WorkerEntrypoint

# Solana custody plumbing (base58/base64url codecs, JSON-RPC client, transfer
# signing, Pyth price read) lives in its own module — see solana.py (adhoc #215).
from solana import (
    _base58_encode,
    _b64url_encode,
    _shortvec,
    _sol_usd_from_http,
    _sol_usd_from_pyth,
    _solana_latest_blockhash,
    _solana_rpc,
    _solana_send_transaction,
    _solana_sign_message,
    _solana_transfer_message,
)

MAX_ROOM_NAME = 80
MAX_CONNECTIONS = 128
# Room frame cap. Kept small (4 MB) to limit relay amplification/abuse; chat plus
# small inline images/files fit, larger attachments are rejected. MUST match the
# client's RoomCrypto kMaxPlainBytes so both ends agree.
MAX_TEXT_BYTES = 4 * 1024 * 1024
# Per-socket room message rate limit: at most this many frames per window.
ROOM_MSG_WINDOW_MS = 10 * 1000
ROOM_MSG_MAX_PER_WINDOW = 20
# Catalog write throttle per owner, and a hard cap on records an owner may hold.
CATALOG_WRITE_COOLDOWN_MS = 5 * 1000
CATALOG_MAX_RECORDS_PER_OWNER = 50
# Per-repo host tunnel/git request rate limit (generous: real clones make a
# handful of requests; this only blunts floods).
HOST_RATE_WINDOW_MS = 10 * 1000
HOST_RATE_MAX_PER_WINDOW = 200
# Retained chat history (encrypted) so late-joining nodes see some backlog.
CHAT_HISTORY_RETAIN_MS = 7 * 24 * 60 * 60 * 1000  # keep the last 7 days
CHAT_HISTORY_MAX_PER_ROOM = 500  # hard cap on retained messages per room
CHAT_HISTORY_MAX_BODY = 48 * 1024  # don't retain very large frames (e.g. files)
MAX_CATALOG_REPOS = 200
MAX_ERROR_LOG = 500
# Anonymous installer diagnostics: one row per reported install step. Bounded the
# same way as the error log so the unauthenticated POST endpoint can't grow D1.
MAX_INSTALL_DIAG = 5000
INSTALL_DIAG_RETAIN_MS = 30 * 24 * 60 * 60 * 1000  # surface a 30-day window
# Opt-in crash/stall telemetry from desktop nodes (issue #354). Bounded like the
# install diagnostics so the unauthenticated POST endpoint can't grow D1, with a
# hard body cap and a per-request event cap so a single POST can never blow up
# the (small) isolate — this must never become an outage vector.
MAX_TELEMETRY = 5000
TELEMETRY_RETAIN_MS = 30 * 24 * 60 * 60 * 1000  # surface a 30-day window
TELEMETRY_MAX_BODY = 32 * 1024  # reject anything larger than this outright
TELEMETRY_MAX_EVENTS = 4        # crash + stall (+ headroom) per node per startup
TELEMETRY_MAX_SUMMARY = 8000    # per-event scrubbed report text
TELEMETRY_KINDS = frozenset({"crash", "stall"})
# Private vulnerability reports: bounded so the open endpoint can't grow D1.
MAX_SECURITY_REPORTS = 1000
MAX_FILES = 5000
# Issue inbox: a single signed event body is small text; cap it and the number
# of un-merged submissions a repo's inbox will hold.
MAX_ISSUE_BYTES = 64 * 1024
MAX_PENDING_ISSUES = 500
# Per-author cap across the issue/pull/commit/discussion inboxes, so one signing
# key can't fill a repo's whole inbox to the global cap and block everyone else.
MAX_PENDING_PER_AUTHOR = 50
# Pull-request inbox: a PR carries a unified diff (text), capped larger than an
# issue body but still bounded. Raised to 100 MB so a PR with a large diff (e.g.
# generated files or vendored code) isn't rejected at submission.
MAX_PULL_BYTES = 100 * 1024 * 1024
MAX_PENDING_PULLS = 200
# Commit-comment inbox: small signed text comments keyed by commit hash.
MAX_COMMIT_COMMENT_BYTES = 64 * 1024
MAX_PENDING_COMMIT_COMMENTS = 500
# Discussion inbox: signed open/comment events for read-only contributors.
MAX_DISCUSSION_BYTES = 64 * 1024
MAX_PENDING_DISCUSSIONS = 500
# Agent-session sync (adhoc #182): desktop -> website push of Claude Code agent
# sessions for a repo, and website -> desktop queued prompts for a running one.
MAX_AGENT_SESSIONS = 300
MAX_AGENT_STRING = 300
MAX_AGENT_TITLE = 240
MAX_AGENT_PROMPT_TEXT = 8000
MAX_PENDING_AGENT_PROMPTS = 50
# Bounded tail of an agent session's run log the desktop pushes for the website
# detail page's live transcript (adhoc #259). Kept modest so the full-replace
# sessions push stays small even with several sessions per repo.
MAX_AGENT_TRANSCRIPT = 16000
# Notification inbox: Worker indexes public-safe notification state while the
# canonical issue/PR/discussion/release records remain in signed repo files or
# pending inboxes. Stored rows are encrypted and bounded per recipient.
MAX_NOTIFICATIONS_PER_RECIPIENT = 500
MAX_NOTIFICATIONS_FETCH = 100
NOTIFICATION_RETAIN_MS = 90 * 24 * 60 * 60 * 1000
NOTIFICATION_KINDS = frozenset({
    "mention",
    "subscribed",
    "pull_submitted",
    "issue_assigned",
    "repo_shared",
    "bounty_funded",
    "bounty_paid",
    "release_published",
    "host_online",
    "host_offline",
    "credits_refilled",
    "pending_inbox",
})
# Email digest bridge (issue #361): the cron rolls a recipient's unread
# notifications into one email so a reply reaches people who don't have the app
# open. Only recipients with a *verified* email get one (issue #320 fixed the
# verification flow, a hard dependency). A notification is only digested once it
# is a couple of minutes old, so a user actively reading in-app clears it before
# any mail goes out; and at most one digest per recipient per interval. Any
# notification kind rides this rail — including issue #346's credits_refilled.
NOTIFICATION_DIGEST_INTERVAL_MS = 60 * 60 * 1000
NOTIFICATION_DIGEST_MIN_AGE_MS = 3 * 60 * 1000
NOTIFICATION_DIGEST_MAX_ITEMS = 20
NOTIFICATION_DIGEST_MAX_RECIPIENTS = 200
# HTTP route patterns (git smart-HTTP, repo APIs, accounts) live in urls.py so the
# router's match table is one small, scannable module instead of buried in this
# 11k-line file. The Worker runtime bundles sibling modules in src/, so this
# import resolves both on Cloudflare and in the test suite (which parses urls.py
# the same way it parses this file).
from urls import (  # noqa: E402
    ROOM_RE,
    REPO_ROOM_RE,
    REPO_ISSUES_RE,
    REPO_PULLS_RE,
    REPO_COMMITS_RE,
    REPO_DISCUSSIONS_RE,
    REPO_SUBSCRIBE_RE,
    REPO_BOUNTY_RE,
    REPO_SHARES_RE,
    REPO_MIRRORS_RE,
    REPO_AGENTS_RE,
    REPO_AGENTS_LIST_RE,
    REPO_AGENTS_PROMPT_RE,
    REPO_AGENTS_TRANSCRIPT_RE,
    REPO_HOST_RE,
    RELEASE_BLOB_RE,
    REPO_RELEASE_DOWNLOADS_RE,
    GIT_INFO_RE,
    GIT_PACK_RE,
    GIT_RECEIVE_RE,
    ACCOUNTS_RE,
)

# The dashboard SPA shell is split into HTML partials (public/dashboard/partials/)
# stitched back together at request time — see dashboard_shell.py. Like urls.py,
# this is a js-free sibling module the runtime bundles and the test suite imports
# directly.
from dashboard_shell import (  # noqa: E402
    assemble_shell,
    included_partials,
    partial_path,
)

# The dashboard behaviour script is likewise split into ordered JS fragments
# (public/dashboard/js/) concatenated back into one /dashboard.js at request
# time — see dashboard_bundle.py. Same js-free sibling-module pattern.
from dashboard_bundle import (  # noqa: E402
    FRAGMENTS as DASHBOARD_JS_FRAGMENTS,
    assemble_bundle,
    fragment_path,
)

# Release manifest + content-addressed blob helpers (tag/asset validation, the
# CAS blob path layout, the canonical signable manifest body, semver ordering)
# live in releases.py — another pure, js-free sibling module the runtime bundles
# and the test suite imports directly. Only verify_release_manifest stays below,
# since it reaches into this file's Ed25519/sha256 crypto.
from releases import (  # noqa: E402
    RELEASE_TAG_RE,
    RELEASE_SEMVER_RE,
    SHA256_HEX_RE,
    valid_release_tag,
    valid_asset_name,
    valid_sha256_hex,
    cas_blob_relpath,
    release_asset_line,
    release_manifest_content,
    release_signing_message,
    generate_shasums,
    release_semver_key,
    resolve_latest_release,
    asset_upload_decision,
)

# Git smart-HTTP wire helpers (pkt-line, ref-advertisement canonicalization,
# request-body decoding) and the repo-blob content-type/filename mapping live
# in their own stdlib-only module — see git_http.py.
from git_http import (  # noqa: E402
    REPO_BLOB_CONTENT_TYPES,
    advertised_refs_canonical,
    decode_git_request_body,
    pkt_line,
    repo_blob_content_type,
    repo_blob_filename,
)

# Mirror grouping, clone-fallback selection, and state-pin helpers are a
# self-contained, builtin-only cluster — see mirrors.py.
from mirrors import (  # noqa: E402
    STATE_PIN_HISTORY,
    _mirror_ms,
    browse_mirror_candidates,
    build_repo_mirrors_payload,
    clone_state_pins,
    mirroring_owner_set,
    repo_clone_online,
    repo_mirror_group_key,
    repo_mirror_same_group,
    select_clone_fallback,
    served_mirror_groups,
)

# Catalog-record sanitization (string cleaning, path-segment validation, the
# public catalog-record builder) lives in catalog.py -- another pure, js-free
# sibling module the runtime bundles and the test suite parses directly.
from catalog import (  # noqa: E402
    MAX_REPO_SEGMENT,
    clean_string,
    safe_catalog_record,
    safe_segment,
)

# The D1 table/index DDL list ensure_schema() runs lives in schema.py.
from schema import SCHEMA_STATEMENTS  # noqa: E402

# Signed-event crypto + canonicalization -- the Ed25519/SHA-256 verify
# primitives and the per-event canonical-content builders (which must
# byte-match the desktop client's *Store contentForSigning/canonicalString)
# live in events.py, another one-directional sibling module (adhoc #279).
from events import (  # noqa: E402
    DISCUSSION_CATEGORIES,
    DISCUSSION_CATEGORY_MAP,
    b64url_decode,
    discussion_event_content,
    ed25519_verify,
    issue_event_content,
    normalized_discussion_category,
    pull_comment_content,
    sha256_hex,
    verify_commit_comment_event,
    verify_discussion_event,
    verify_issue_event,
    verify_pull_comment_event,
    verify_pull_event,
    verify_release_manifest,
)

# Largest git-req-chunk (push pack fragment) forwarded to the host in one WS
# message; matches the host's 256 KiB git-chunk ceiling so neither side trips
# the relay's ~1 MiB message cap.
GIT_REQ_CHUNK = 256 * 1024
TUNNEL_TIMEOUT_MS = 20000
GIT_TIMEOUT_MS = 60000
# Most files one /blobs batch may read. One batched request replaces the
# website's per-record /blob fan-out (50+ parallel HTTP calls per page view,
# which tripped the per-repo rate limit); the DO spreads the reads over the
# live tunnel concurrently instead.
MAX_BLOB_BATCH = 60
# Aggregate homepage/network stats. Cached at the edge so a burst of visitors
# costs one computation per colo per TTL instead of a Durable Object fan-out per
# visit. The flagship room whose live client count the homepage shows.
NETWORK_STATS_TTL = 20  # seconds the /api/network/stats response is cached
FLAGSHIP_ROOM_KEY = "repo:mainnode/forkmesh:room:general"
# A host counts as "online" if it has been active within this window. The window
# self-heals presence rows orphaned by a host that vanished without a clean close;
# active hosts refresh their row at most once per HOST_PRESENCE_REFRESH_MS.
HOST_PRESENCE_STALE_MS = 10 * 60 * 1000
HOST_PRESENCE_REFRESH_MS = 60 * 1000
# How long a downed repo's clone traffic stays pinned to one chosen mirror (see
# clone_sticky). Long enough that a clone's info/refs and upload-pack POST land
# on the same node; short enough that the load still rotates across mirrors.
CLONE_STICKY_MS = 5 * 60 * 1000


# Public node name = username = a single DNS-like label: lowercase letters,
# digits, and hyphens; must start with a letter and end with a letter or digit;
# no underscores, no spaces, <= 63 chars. This is the user's public handle and
# their repo namespace, so it is validated identically in the web and desktop
# clients.
NODE_NAME_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
MENTION_RE = re.compile(r"(?<![A-Za-z0-9._%+-])@([a-z](?:[a-z0-9-]{0,61}[a-z0-9])?)\b")
MAX_NODE_NAME = 63

# A node's Ed25519 public key (raw 32 bytes, base64url, unpadded) — the value
# the desktop app's profile card itself labels "Node ID". The claim-node flow
# (issue #351) accepts this as an alternative to the account's chosen name,
# since that's what users copy when the app tells them their "node ID".
NODE_PUBKEY_RE = re.compile(r"^[A-Za-z0-9_-]{43}$")
# ACCOUNTS_RE is imported from urls.py with the rest of the route table.
LOGIN_MAX_SKEW_MS = 5 * 60 * 1000
# Login brute-force throttle: after LOGIN_MAX_FAILS failures (counted within a
# rolling window) the identifier is locked out for LOGIN_LOCKOUT_MS.
LOGIN_MAX_FAILS = 10
LOGIN_FAIL_WINDOW_MS = 15 * 60 * 1000
LOGIN_LOCKOUT_MS = 15 * 60 * 1000
# How long a password-reset link stays valid after it is emailed.
PASSWORD_RESET_TTL_MS = 60 * 60 * 1000
# Users-vs-nodes linking (adhoc #53). A website claim's confirmation code (shown
# on the node, typed into the site) lives this long; an installer link code
# (shown by install.sh, typed into the installing desktop app) lives longer
# because the fresh node still has to build/launch/register before it can
# present the code.
CLAIM_CODE_TTL_MS = 10 * 60 * 1000
CLAIM_CODE_MAX_ATTEMPTS = 5
# An admin-initiated ownership takeover (adhoc #141) rides back to the target
# node on its own heartbeat, same as a claim code, but the node may be offline
# for a while before it's seen and approved/denied, so it gets a long window.
OWNERSHIP_TRANSFER_TTL_MS = 24 * 60 * 60 * 1000
LINK_CODE_TTL_MS = 30 * 60 * 1000
LINK_CODE_RE = re.compile(r"^[0-9]{6}$")
MAX_AVATAR_BYTES = 256 * 1024
MAX_AVATAR_B64 = 4 * ((MAX_AVATAR_BYTES + 2) // 3)
PNG_HEADER = b"\x89PNG\r\n\x1a\n"


def valid_node_name(value):
    value = (value or "").strip()
    return (bool(value) and len(value) <= MAX_NODE_NAME and
            bool(NODE_NAME_RE.match(value)))


def valid_node_pubkey(value):
    return bool(NODE_PUBKEY_RE.match((value or "").strip()))


def notification_mentions(*parts):
    found = set()
    for part in parts:
        if not part:
            continue
        for match in MENTION_RE.finditer(str(part).lower()):
            name = match.group(1)
            if name and len(name) <= MAX_NODE_NAME and NODE_NAME_RE.match(name):
                found.add(name)
    return sorted(found)


def notification_payload(kind, title, body="", repo="", href="", actor="",
                         source="", ts=0, meta=None):
    kind = kind if kind in NOTIFICATION_KINDS else "pending_inbox"
    return {
        "kind": kind,
        "title": clean_string(title, 160),
        "body": clean_string(body, 500),
        "repo": clean_string(repo, 180),
        "href": clean_string(href, 512),
        "actor": clean_string(actor, 120),
        "source": clean_string(source, 80),
        "ts": int(ts or 0),
        "readAt": 0,
        "meta": meta if isinstance(meta, dict) else {},
    }


def repo_web_href(owner, repo):
    # Clean, shareable repo link (/owner/repo) for notification "Open context"
    # buttons. 404.html bounces this to the dashboard's repo view and dashboard.js
    # keeps the clean path in the address bar, so links read forkmesh.com/owner/repo
    # rather than the old forkmesh.com/dashboard?repo=owner/repo form.
    return "/" + quote(owner) + "/" + quote(repo)


# The signed-event crypto + canonical-content helpers (b64url_decode,
# ed25519_verify, sha256_hex, the verify_* verifiers and *_content
# canonicalizers) were extracted into events.py (adhoc #279); imported above.


def git_bytes_response(data, content_type):
    return JsResponse.new(
        _to_js(bytes(data)),
        to_js(
            {
                "status": 200,
                "headers": {
                    "content-type": content_type,
                    "cache-control": "no-cache, max-age=0, must-revalidate",
                },
            }
        ),
    )


def to_js(value):
    return _to_js(value, dict_converter=Object.fromEntries)


def new_socket_id():
    # Stable per-socket id stored in the hibernation attachment, so we can skip
    # the sender on broadcast without relying on object identity (which does not
    # survive a Durable Object eviction).
    rnd = js_crypto.getRandomValues(Uint8Array.new(4))
    return "%d-%d" % (int(Date.now()),
                      (rnd[0] << 24) | (rnd[1] << 16) | (rnd[2] << 8) | rnd[3])


def _ws_attachment(ws):
    try:
        return ws.deserializeAttachment()
    except Exception:
        return None


def _ws_attr(ws, name, default=None):
    att = _ws_attachment(ws)
    if att is None:
        return default
    value = getattr(att, name, default)
    return default if value is None else value


def json_response(data, status=200, cache_seconds=None, cache_control=None):
    headers = {"content-type": "application/json; charset=utf-8"}
    if cache_control is not None:
        headers["cache-control"] = cache_control
    elif cache_seconds is not None:
        # Lets both the Cloudflare edge cache (via the Cache API) and the browser
        # reuse this response for cache_seconds, collapsing repeated polls.
        headers["cache-control"] = "public, max-age=%d" % cache_seconds
    return Response(json.dumps(data, indent=2), status=status, headers=headers)


# --- Edge cache (Cache API) helpers -----------------------------------------
# Dynamic Worker responses are not edge-cached automatically; we cache the few
# read-heavy aggregate endpoints explicitly so a burst of visitors collapses to
# one origin computation per colo per TTL. Keys are synthetic absolute URLs.

async def edge_cache_match(cache_key):
    try:
        hit = await js_caches.default.match(cache_key)
    except Exception:
        hit = None
    return Response(hit) if hit is not None else None


async def edge_cache_put(cache_key, response):
    # cache.put consumes the body it is handed, so store a clone and return the
    # original to the caller. Best-effort: a cache failure must not fail the read.
    try:
        await js_caches.default.put(cache_key, response.js_object.clone())
    except Exception:
        pass


async def edge_cache_delete(cache_key):
    try:
        await js_caches.default.delete(cache_key)
    except Exception:
        pass


CATALOG_CACHE_KEY = "https://forkmesh.internal/api/repositories"
NETWORK_STATS_CACHE_KEY = "https://forkmesh.internal/api/network/stats"
NETWORK_LEADERBOARDS_CACHE_KEY = "https://forkmesh.internal/api/network/leaderboards"
CATALOG_TTL = 10  # seconds the repositories list is cached at the edge


async def touch_host_presence(env, repo_bi):
    # Mark a repo's tunnel as live (or refresh its timestamp). Called when a host
    # connects and, throttled, while it serves traffic.
    await ensure_schema(env)
    now = int(Date.now())
    notify_online = False
    try:
        existing = await d1_first(env, "SELECT ts FROM host_presence WHERE repo_bi=?", repo_bi)
        previous = int((existing or {}).get("ts") or 0)
        notify_online = not previous or now - previous > HOST_PRESENCE_STALE_MS
    except Exception:
        notify_online = False
    await d1_run(
        env,
        "INSERT INTO host_presence (repo_bi, ts) VALUES (?, ?) "
        "ON CONFLICT(repo_bi) DO UPDATE SET ts=excluded.ts",
        repo_bi, now,
    )
    if notify_online:
        await notify_host_status(env, repo_bi, "host_online")
    # Stamp the first time this repo was ever hosted (for the "longest hosted"
    # leaderboard). INSERT OR IGNORE keeps the earliest timestamp forever.
    try:
        await d1_run(
            env,
            "INSERT OR IGNORE INTO repo_first_hosted (repo_bi, ts) VALUES (?, ?)",
            repo_bi, now,
        )
    except Exception:
        pass


async def next_clone_rotation(env, repo_bi):
    # Advance and read back this repo's round-robin cursor so consecutive clone
    # fallbacks rotate across its mirrors instead of all hitting the freshest one.
    # Best-effort: any failure yields 0 (the freshest-first pick), so a flaky
    # counter never blocks a redirect.
    try:
        await ensure_schema(env)
        await d1_run(
            env,
            "INSERT INTO clone_rr (repo_bi, n) VALUES (?, 1) "
            "ON CONFLICT(repo_bi) DO UPDATE SET n = n + 1",
            repo_bi,
        )
        row = await d1_first(env, "SELECT n FROM clone_rr WHERE repo_bi=?", repo_bi)
        return int((row or {}).get("n") or 0)
    except Exception:
        return 0


async def _flagship_client_count(env):
    # One internal request to the flagship room's count endpoint (not a WebSocket).
    try:
        room_id = env.FORKMESH_MAINNODE_ROOM.idFromName(FLAGSHIP_ROOM_KEY)
        room = env.FORKMESH_MAINNODE_ROOM.get(room_id)
        resp = await room.fetch(
            "https://forkmesh.internal/api/repo/mainnode/forkmesh/rooms/general/clients"
        )
        data = await resp.json()
        # workers.Response.json() may cross the Python/JS boundary as either a
        # native dict or a JsProxy depending on where the response was created;
        # accept both shapes (see the host-count read in select_install_source).
        clients = (data.get("clients", 0) if isinstance(data, dict)
                   else getattr(data, "clients", 0))
        return int(clients or 0)
    except Exception:
        return 0


async def network_stats(env, include_payouts=False):
    # Aggregate homepage stats: repo count (D1), live host count (presence table),
    # and flagship-room client count (one internal request). Cached at the edge.
    cache_key = NETWORK_STATS_CACHE_KEY + ("?payouts=1" if include_payouts else "")
    cached = await edge_cache_match(cache_key)
    if cached is not None:
        return cached

    await ensure_schema(env)
    now = int(Date.now())
    repo_row = await d1_first(env, "SELECT COUNT(*) AS n FROM repositories")
    repos = int((repo_row or {}).get("n", 0) or 0)

    cutoff = now - HOST_PRESENCE_STALE_MS
    try:
        await notify_stale_hosts_offline(env, cutoff)
        await d1_run(env, "DELETE FROM host_presence WHERE ts < ?", cutoff)
    except Exception:
        pass

    # Named list of the nodes that are actually online right now (same signal the
    # uptime cron samples), so the dashboard rail can show *who* is live instead of
    # painting the 48h uptime leaderboard green — a node offline now must not look
    # active, and a node that just came up must appear even with no accrued minutes.
    #
    # The headline "Nodes" count is the size of THIS set — the distinct, publicly
    # known nodes (a catalog record or a registered account) that are live — rather
    # than a raw COUNT of host_presence rows. A plain row count over-reports: one
    # node hosting several repos counts many times, and an ad-hoc/private tunnel
    # with no public catalog record inflates the total with a node we never name.
    # Deriving the count from the named set is what makes the Network panel agree
    # with the Mirror nodes list on who is online (adhoc #93).
    try:
        online_nodes = sorted(
            {label for label in (await _live_online_nodes(env, now)).values() if label}
        )
        hosts = len(online_nodes)
    except Exception:
        online_nodes = []
        host_row = await d1_first(
            env, "SELECT COUNT(*) AS n FROM host_presence WHERE ts >= ?", cutoff
        )
        hosts = int((host_row or {}).get("n", 0) or 0)

    clients = await _flagship_client_count(env)
    payout_nodes = await _network_payout_nodes(env) if include_payouts else None

    # Live minimum join donation (~$1, from the on-chain price) so the signup
    # page can show the real figure before requesting a deposit address.
    min_lamports = await _min_join_lamports(env)
    price = await _sol_usd_price(env)
    resp = json_response(
        {"ok": True, "repos": repos, "hosts": hosts, "clients": clients,
         "onlineNodes": online_nodes,
         "minLamports": min_lamports, "minSol": _amount_sol(min_lamports),
         "minUsd": round((min_lamports / LAMPORTS_PER_SOL) * price, 2) if price else 0,
         "solUsd": price or 0,
         **({"payoutNodes": payout_nodes} if include_payouts else {})},
        cache_seconds=NETWORK_STATS_TTL,
    )
    await edge_cache_put(cache_key, resp)
    return resp


async def _network_payout_nodes(env):
    cutoff = int(Date.now()) - ACCOUNT_PRESENCE_STALE_MS
    try:
        await d1_run(env, "DELETE FROM account_presence WHERE ts < ?", cutoff)
    except Exception:
        pass
    rows = await d1_all(
        env,
        """SELECT a.name_bi, a.name, a.data, ap.ts AS online_ts
             FROM accounts a
             LEFT JOIN account_presence ap
               ON ap.name_bi = a.name_bi AND ap.ts >= ?
             ORDER BY COALESCE(ap.ts, 0) DESC, a.name ASC""",
        cutoff,
    )
    mirroring = await _mirroring_owners(env)
    nodes = []
    seen_wallets = set()
    for row in rows:
        rec = await decrypt_row(env, row.get("data"))
        if not rec or rec.get("status") != "active":
            continue
        name = clean_string(row.get("name") or rec.get("name", ""), MAX_NODE_NAME)
        wallet = (rec.get("solana") or "").strip()
        has_wallet = bool(wallet and SOLANA_RE.match(wallet))
        online = bool(row.get("online_ts"))
        first_wallet = has_wallet and wallet not in seen_wallets
        balance = await _solana_balance_lamports(env, wallet) if has_wallet else None
        verified_wallet = bool(balance is not None and balance >= MIN_ACTIVE_LAMPORTS)
        # A node only shares the split if it mirrors a repo for another node, so
        # its eligibility on /network/ must reflect that too (issue #94).
        mirrors_repo = mirroring is None or bool(name and name.lower() in mirroring)
        eligible = bool(online and first_wallet and verified_wallet and mirrors_repo)
        if has_wallet:
            seen_wallets.add(wallet)
        reason = "eligible"
        if not online:
            reason = "offline"
        elif not has_wallet:
            reason = "missing_wallet"
        elif not first_wallet:
            reason = "duplicate_wallet"
        elif not verified_wallet:
            reason = "wallet_unverified"
        elif not mirrors_repo:
            reason = "no_mirrors"
        nodes.append({
            "name": name or _short_presence_label("node", row.get("name_bi")),
            "wallet": wallet if has_wallet else "",
            "balanceLamports": balance,
            "balanceSol": _amount_sol(balance) if balance is not None else "",
            "online": online,
            "payoutEligible": eligible,
            "eligibilityReason": reason,
        })
    # Main relay: surface online nodes federated in from approved relays too, so
    # /network/ reflects everyone sharing the disbursement.
    if _is_main_relay(env):
        try:
            fed = await d1_all(
                env,
                "SELECT fp.wallet AS wallet, fp.name AS name, r.label AS label "
                "FROM federated_presence fp "
                "JOIN relays r ON r.relay_bi = fp.relay_bi "
                "WHERE fp.ts >= ? AND r.status = 'approved' ORDER BY fp.ts DESC",
                cutoff)
            for r in (fed or []):
                wallet = (r.get("wallet") or "").strip()
                if not wallet or not SOLANA_RE.match(wallet):
                    continue
                eligible = wallet not in seen_wallets
                seen_wallets.add(wallet)
                relay_label = clean_string(r.get("label", ""), 80) or "relay"
                nodes.append({
                    "name": clean_string(r.get("name", ""), MAX_NODE_NAME) or "node",
                    "wallet": wallet,
                    "balanceLamports": None, "balanceSol": "",
                    "online": True, "payoutEligible": eligible,
                    "eligibilityReason": "eligible" if eligible else "duplicate_wallet",
                    "relay": relay_label,
                })
        except Exception:
            pass
    return nodes


# --- Online-activity history (24h graph on /network/) -----------------------
# A per-minute cron records how many nodes are online into the current hour's
# bucket, so each hour's value is "node-minutes online" that hour.

ONLINE_SAMPLE_WINDOW_MS = 2 * 60 * 1000  # treat a node seen in the last 2 min as online
ONLINE_HISTORY_RETAIN_MS = 48 * 60 * 60 * 1000
ONLINE_HISTORY_MAX_NODES = 50


def _short_presence_label(prefix, key):
    return prefix + " " + str(key or "")[:8]


async def _live_online_nodes(env, now):
    nodes = {}
    host_rows = await d1_all(
        env,
        """SELECT hp.repo_bi, r.owner_bi, r.is_private, r.data
             FROM host_presence hp
             LEFT JOIN repositories r ON r.key_bi = hp.repo_bi
             WHERE hp.ts >= ?""",
        now - HOST_PRESENCE_STALE_MS,
    )
    for row in host_rows:
        # A host_presence row is written for ANY owner/repo URL that gets served
        # (touch_host_presence keys on a blind index of the path), so it includes
        # repos that were never published to the catalog — ad-hoc hosts. Those
        # rows don't join to a repository, and the public /network/ graph used to
        # surface them as anonymous "repo <hash>" phantom nodes. Only count hosts
        # serving a published, public, non-blocked repo, keyed by owner — the same
        # filtering the catalog and leaderboards apply.
        owner_bi = row.get("owner_bi")
        if not owner_bi or not row.get("data") or int(row.get("is_private") or 0):
            continue
        rec = await decrypt_row(env, row["data"])
        if not rec:
            continue
        if _is_blocked_catalog_identity(env, rec.get("owner"), rec.get("name")):
            continue
        label = clean_string(rec.get("owner", ""), MAX_NODE_NAME)
        if not label:
            continue
        nodes[owner_bi] = label

    acct_rows = await d1_all(
        env,
        """SELECT ap.name_bi, a.name, a.data
             FROM account_presence ap
             LEFT JOIN accounts a ON a.name_bi = ap.name_bi
             WHERE ap.ts >= ?""",
        now - ONLINE_SAMPLE_WINDOW_MS,
    )
    for row in acct_rows:
        node_key = row.get("name_bi")
        if not node_key:
            continue
        label = clean_string(row.get("name", ""), MAX_NODE_NAME)
        if not label and row.get("data"):
            rec = await decrypt_row(env, row["data"])
            if rec:
                label = clean_string(rec.get("name", ""), MAX_NODE_NAME)
        nodes[node_key] = label or nodes.get(node_key) or _short_presence_label("node", node_key)
    return nodes


async def record_online_sample(env):
    # Called once a minute by the scheduled (cron) handler. "Online nodes" is the
    # number of live host tunnels (host_presence) — the signal that actually
    # reflects desktop nodes being up — taken together with any account
    # heartbeats. The account funnel is currently disabled, so sampling only
    # account_presence left the activity graph permanently empty even while a
    # host was online; counting hosts fixes that.
    await ensure_schema(env)
    now = int(Date.now())
    nodes = await _live_online_nodes(env, now)
    online = len(nodes)
    hour_ts = (now // 3600000) * 3600000
    await d1_run(
        env,
        "INSERT INTO online_hourly (hour_ts, node_minutes) VALUES (?, ?) "
        "ON CONFLICT(hour_ts) DO UPDATE SET node_minutes = node_minutes + ?",
        hour_ts, online, online,
    )
    for node_key, label in nodes.items():
        await d1_run(
            env,
            """INSERT INTO online_hourly_nodes
                 (hour_ts, node_key, label, node_minutes) VALUES (?, ?, ?, 1)
               ON CONFLICT(hour_ts, node_key) DO UPDATE SET
                 node_minutes = node_minutes + 1,
                 label = excluded.label""",
            hour_ts, node_key, label,
        )
    await d1_run(
        env, "DELETE FROM online_hourly WHERE hour_ts < ?",
        hour_ts - ONLINE_HISTORY_RETAIN_MS,
    )
    await d1_run(
        env, "DELETE FROM online_hourly_nodes WHERE hour_ts < ?",
        hour_ts - ONLINE_HISTORY_RETAIN_MS,
    )


async def online_history(env):
    await ensure_schema(env)
    now = int(Date.now())
    cur_hour = (now // 3600000) * 3600000
    start = cur_hour - 23 * 3600000  # last 24 hourly buckets, oldest first
    rows = await d1_all(
        env,
        "SELECT hour_ts, node_minutes FROM online_hourly WHERE hour_ts >= ? "
        "ORDER BY hour_ts",
        start,
    )
    by_hour = {int(r["hour_ts"]): int(r["node_minutes"]) for r in rows}
    series = [{"hourTs": start + i * 3600000,
               "nodeMinutes": by_hour.get(start + i * 3600000, 0)}
              for i in range(24)]
    node_rows = await d1_all(
        env,
        """SELECT node_key, label, hour_ts, node_minutes
             FROM online_hourly_nodes
             WHERE hour_ts >= ?
             ORDER BY node_key, hour_ts""",
        start,
    )
    grouped = {}
    for row in node_rows:
        node_key = str(row.get("node_key") or "")
        if not node_key:
            continue
        item = grouped.setdefault(node_key, {
            "label": clean_string(row.get("label", ""), MAX_NODE_NAME) or
                     _short_presence_label("node", node_key),
            "hours": {},
            "total": 0,
        })
        minutes = int(row.get("node_minutes") or 0)
        hour_ts = int(row.get("hour_ts") or 0)
        item["hours"][hour_ts] = minutes
        item["total"] += minutes
        if row.get("label"):
            item["label"] = clean_string(row.get("label", ""), MAX_NODE_NAME)
    nodes = []
    for node_key, item in grouped.items():
        nodes.append({
            "id": node_key[:12],
            "label": item["label"],
            "totalMinutes": item["total"],
            "hours": [
                {"hourTs": start + i * 3600000,
                 "minutes": item["hours"].get(start + i * 3600000, 0)}
                for i in range(24)
            ],
        })
    nodes.sort(key=lambda n: (-int(n["totalMinutes"]), str(n["label"])))
    return json_response(
        {"ok": True, "hours": series, "nodes": nodes[:ONLINE_HISTORY_MAX_NODES]},
        cache_seconds=30,
    )


# --- System status page (/status) -------------------------------------------
# A per-minute cron folds one health check per system into today's UTC-day
# bucket, so the public /status page can show a 30-day history per system
# (statuspage.io style) without any extra always-on monitoring infra. Every
# system reuses a signal the relay already tracks:
#   - database: a trivial D1 round trip.
#   - git_hosting: at least one desktop host tunnel is live (host_presence).
#   - website / api / realtime: unhandled/5xx errors logged this minute
#     (log_error, see Default.fetch) bucketed by the path they hit, so an
#     incident in one area doesn't paint the whole site down.
STATUS_SYSTEMS = [
    ("website", "Website"),
    ("api", "API"),
    ("database", "Database"),
    ("git_hosting", "Git hosting network"),
    ("realtime", "Realtime sync (chat & tunnels)"),
]
STATUS_HISTORY_DAYS = 30
STATUS_HISTORY_RETAIN_MS = STATUS_HISTORY_DAYS * 24 * 60 * 60 * 1000
STATUS_SAMPLE_WINDOW_MS = 60 * 1000  # one cron tick


async def record_status_sample(env):
    # Called once a minute by the scheduled (cron) handler. Best-effort per
    # system so one failing check can't blank the rest of the page.
    await ensure_schema(env)
    now = int(Date.now())
    day_ts = (now // 86400000) * 86400000
    hour_ts = (now // 3600000) * 3600000
    ok = {}
    reason = {}

    try:
        await d1_first(env, "SELECT 1 AS ok")
        ok["database"] = True
    except Exception as exc:
        ok["database"] = False
        reason["database"] = "Database query failed: " + str(exc)[:160]

    try:
        cutoff = now - HOST_PRESENCE_STALE_MS
        row = await d1_first(
            env, "SELECT COUNT(*) AS n FROM host_presence WHERE ts >= ?", cutoff,
        )
        ok["git_hosting"] = int((row or {}).get("n", 0) or 0) > 0
        if not ok["git_hosting"]:
            reason["git_hosting"] = "No desktop hosts have checked in within the last 10 minutes"
    except Exception as exc:
        ok["git_hosting"] = False
        reason["git_hosting"] = "Host presence query failed: " + str(exc)[:160]

    try:
        rows = await d1_all(
            env, "SELECT path, status, message FROM error_log WHERE ts >= ?",
            now - STATUS_SAMPLE_WINDOW_MS,
        )
        failed = {"website": False, "api": False, "realtime": False}
        first_hit = {"website": None, "api": None, "realtime": None}
        hit_count = {"website": 0, "api": 0, "realtime": 0}
        for row in rows:
            path = str(row.get("path") or "")
            if (ROOM_RE.match(path) or REPO_ROOM_RE.match(path) or
                    GIT_INFO_RE.match(path) or GIT_PACK_RE.match(path)):
                bucket = "realtime"
            elif path.startswith("/api/"):
                bucket = "api"
            else:
                bucket = "website"
            failed[bucket] = True
            hit_count[bucket] += 1
            if first_hit[bucket] is None:
                first_hit[bucket] = (row.get("status"), path, str(row.get("message") or "").strip())
        ok["website"] = not failed["website"]
        ok["api"] = not failed["api"]
        ok["realtime"] = not failed["realtime"]
        for bucket in ("website", "api", "realtime"):
            if failed[bucket] and first_hit[bucket]:
                status_code, path, message = first_hit[bucket]
                text = (str(status_code) + " on " + path) if status_code else path
                if message:
                    text += ": " + message[:120]
                if hit_count[bucket] > 1:
                    text += " (+%d more)" % (hit_count[bucket] - 1)
                reason[bucket] = text
    except Exception:
        # A query hiccup here is not itself evidence of an outage — don't
        # fabricate a false incident from it.
        ok["website"] = ok["api"] = ok["realtime"] = True

    for system_id, _label in STATUS_SYSTEMS:
        failure = 0 if ok.get(system_id, True) else 1
        await d1_run(
            env,
            "INSERT INTO system_status_daily (day_ts, system, checks, failures) "
            "VALUES (?, ?, 1, ?) "
            "ON CONFLICT(day_ts, system) DO UPDATE SET "
            "checks = checks + 1, failures = failures + ?",
            day_ts, system_id, failure, failure,
        )
        # reason is only set when this sample failed; on success it's left NULL
        # so the COALESCE below keeps whatever failure reason was last recorded
        # this hour, rather than blanking it out.
        await d1_run(
            env,
            "INSERT INTO system_status_hourly (hour_ts, system, checks, failures, reason) "
            "VALUES (?, ?, 1, ?, ?) "
            "ON CONFLICT(hour_ts, system) DO UPDATE SET "
            "checks = checks + 1, failures = failures + ?, "
            "reason = COALESCE(excluded.reason, system_status_hourly.reason)",
            hour_ts, system_id, failure, reason.get(system_id), failure,
        )
    await d1_run(
        env, "DELETE FROM system_status_daily WHERE day_ts < ?",
        day_ts - STATUS_HISTORY_RETAIN_MS,
    )
    await d1_run(
        env, "DELETE FROM system_status_hourly WHERE hour_ts < ?",
        day_ts - STATUS_HISTORY_RETAIN_MS,
    )


async def status_history(env):
    await ensure_schema(env)
    now = int(Date.now())
    cur_day = (now // 86400000) * 86400000
    start = cur_day - (STATUS_HISTORY_DAYS - 1) * 86400000
    rows = await d1_all(
        env,
        "SELECT day_ts, system, checks, failures FROM system_status_daily "
        "WHERE day_ts >= ?",
        start,
    )
    by_system = {}
    for row in rows:
        system_id = str(row.get("system") or "")
        by_system.setdefault(system_id, {})[int(row["day_ts"])] = (
            int(row.get("checks") or 0), int(row.get("failures") or 0),
        )

    hour_rows = await d1_all(
        env,
        "SELECT hour_ts, system, checks, failures, reason FROM system_status_hourly "
        "WHERE hour_ts >= ?",
        start,
    )
    by_system_hour = {}
    for row in hour_rows:
        system_id = str(row.get("system") or "")
        by_system_hour.setdefault(system_id, {})[int(row["hour_ts"])] = (
            int(row.get("checks") or 0), int(row.get("failures") or 0),
            row.get("reason") or None,
        )

    systems = []
    for system_id, label in STATUS_SYSTEMS:
        days = []
        total_checks = total_failures = 0
        # Walked oldest-to-newest, so the last non-operational hour we see is
        # also the most recent one — that becomes the headline "why" shown
        # without requiring a hover, next to the system's current badge.
        latest_reason = None
        latest_reason_ts = None
        for i in range(STATUS_HISTORY_DAYS):
            this_day = start + i * 86400000
            checks, failures = by_system.get(system_id, {}).get(this_day, (0, 0))
            total_checks += checks
            total_failures += failures
            uptime = round(((checks - failures) / checks) * 100, 2) if checks else None
            hours = []
            for h in range(24):
                hour_ts = this_day + h * 3600000
                if hour_ts > now:
                    break
                h_checks, h_failures, h_reason = by_system_hour.get(
                    system_id, {}).get(hour_ts, (0, 0, None))
                if not h_checks:
                    h_status = "unknown"
                elif h_failures == 0:
                    h_status = "operational"
                elif h_failures >= h_checks:
                    h_status = "down"
                else:
                    h_status = "degraded"
                hour_reason = h_reason if h_status != "operational" else None
                hours.append({
                    "hourTs": hour_ts, "status": h_status,
                    "checks": h_checks, "failures": h_failures,
                    "reason": hour_reason,
                })
                if hour_reason:
                    latest_reason = hour_reason
                    latest_reason_ts = hour_ts
            days.append({
                "dayTs": this_day, "checks": checks, "failures": failures,
                "uptimePct": uptime, "hours": hours,
                "hoursElapsed": len(hours),
            })
        # Current status comes from the most recent HOUR with any data, not
        # the whole current day's aggregate — otherwise an incident that was
        # resolved an hour ago keeps the badge red/yellow for the rest of the
        # day even once every recent check has gone back to green.
        latest_hour = None
        for d in reversed(days):
            for h in reversed(d["hours"]):
                if h["checks"]:
                    latest_hour = h
                    break
            if latest_hour:
                break
        if latest_hour is not None:
            status = latest_hour["status"]
        else:
            # No hourly rows at all (e.g. pre-migration data) — fall back to
            # the most recent day's aggregate so the badge isn't stuck unknown.
            latest_day = next((d for d in reversed(days) if d["checks"]), None)
            if latest_day is None:
                status = "unknown"
            elif latest_day["failures"] == 0:
                status = "operational"
            elif latest_day["failures"] >= latest_day["checks"]:
                status = "down"
            else:
                status = "degraded"
        overall_uptime = (
            round(((total_checks - total_failures) / total_checks) * 100, 2)
            if total_checks else None
        )
        systems.append({
            "id": system_id, "label": label, "status": status,
            "uptimePct": overall_uptime, "days": days,
            "reason": latest_reason if status != "operational" else None,
            "reasonTs": latest_reason_ts if status != "operational" else None,
        })

    # Current-state snapshot (issue #356): the headline health metrics rendered
    # at the top of the page — mainnode host reachable, the distinct online node
    # count (same signal as the /network/ headline, NOT raw host_presence rows,
    # which over-count), catalog size, and errors logged in the last 24h. Each
    # read is best-effort so one failing query can't blank the summary, and it
    # all rides on the single /api/status fetch a page view already makes.
    current = {}
    try:
        repo_row = await d1_first(env, "SELECT COUNT(*) AS n FROM repositories")
        current["catalogRepos"] = int((repo_row or {}).get("n", 0) or 0)
    except Exception:
        current["catalogRepos"] = None
    try:
        online = {
            label for label in (await _live_online_nodes(env, now)).values() if label
        }
        current["onlineNodes"] = len(online)
    except Exception:
        current["onlineNodes"] = None
    try:
        err_row = await d1_first(
            env, "SELECT COUNT(*) AS n FROM error_log WHERE ts >= ?",
            now - 24 * 60 * 60 * 1000,
        )
        current["errors24h"] = int((err_row or {}).get("n", 0) or 0)
    except Exception:
        current["errors24h"] = None
    try:
        # host_presence is keyed one-row-per-repo (upserted on each heartbeat),
        # so reading it with no staleness filter gives the mainnode's true last
        # heartbeat even while offline — lets the banner say "last seen 42m
        # ago" instead of just a bare "Offline" with no technical detail.
        mainnode_bi = await blind_index(env, "mainnode/forkmesh")
        host_row = await d1_first(
            env, "SELECT ts FROM host_presence WHERE repo_bi = ?", mainnode_bi,
        )
        last_ts = int(host_row["ts"]) if host_row else None

        # Regression (adhoc #189): there is no reserved "mainnode" owner
        # account — "mainnode/forkmesh" is just the fixed path the flagship
        # chat room happens to use (FLAGSHIP_ROOM_KEY), not a real repo
        # identity any desktop host ever registers under, so the check above
        # never sees a heartbeat even with a live self-hosted instance. Fold
        # in the real signal too: repositories.key_bi is computed the same
        # way as host_presence.repo_bi (blind_index of "owner/name"), so join
        # the two directly to find whichever real owner is actually hosting a
        # repo named "forkmesh".
        repo_rows = await d1_all(env, "SELECT key_bi, data FROM repositories")
        presence_rows = await d1_all(
            env, "SELECT repo_bi, ts FROM host_presence")
        presence = {r["repo_bi"]: int(r["ts"]) for r in presence_rows}
        for row in repo_rows:
            rec = await decrypt_row(env, row.get("data"))
            if not rec or safe_segment(rec.get("name", "")) != "forkmesh":
                continue
            if _is_blocked_catalog_identity(
                    env, rec.get("owner"), rec.get("name")):
                continue
            ts = presence.get(row.get("key_bi"))
            if ts is not None and (last_ts is None or ts > last_ts):
                last_ts = ts
        current["mainnodeLastSeenTs"] = last_ts
        current["mainnodeOnline"] = (
            last_ts is not None and now - last_ts < HOST_PRESENCE_STALE_MS
        )
        current["mainnodeStaleMs"] = HOST_PRESENCE_STALE_MS
    except Exception:
        current["mainnodeOnline"] = None
        current["mainnodeLastSeenTs"] = None
        current["mainnodeStaleMs"] = HOST_PRESENCE_STALE_MS

    return json_response(
        {"ok": True, "now": now, "systems": systems, "current": current},
        cache_seconds=60,
    )


# --- Leaderboards (/network/) ----------------------------------------------
# Public ranking boards backing issue #11. Each board has a persisted data
# source: node uptime (online_hourly_nodes), public repos per owner + most-
# mirrored project + longest-hosted repo (repositories / repo_first_hosted),
# contributor activity (contributor_activity, tallied as issues/PRs/commits are
# submitted), and funds received (funds_received, accumulated as donations are
# swept and bounties paid out).

LEADERBOARD_LIMIT = 10  # rows returned per board


async def _record_contributor(env, author, kind):
    # Bump a contributor's running activity tally. kind is one of
    # "issues"/"pulls"/"commits". Called as signed issue/PR/commit events are
    # accepted into the inbox; best-effort so a tally failure never blocks the
    # submission. author is the public contributor name.
    name = clean_string(author or "", MAX_NODE_NAME)
    if not name or kind not in ("issues", "pulls", "commits"):
        return
    try:
        author_bi = await blind_index(env, name.lower())
        await d1_run(
            env,
            f"""INSERT INTO contributor_activity
                  (author_bi, name, {kind}, total, last_ts)
                VALUES (?, ?, 1, 1, ?)
                ON CONFLICT(author_bi) DO UPDATE SET
                  {kind} = {kind} + 1, total = total + 1,
                  name = excluded.name, last_ts = excluded.last_ts""",
            author_bi, name, int(Date.now()),
        )
    except Exception:
        pass


async def _record_funds_received(env, scope, key, name, lamports):
    # Accumulate disbursed lamports per recipient for the funds-received boards.
    # Best-effort: never let a bookkeeping failure abort a money transfer.
    if not key or int(lamports or 0) <= 0:
        return
    try:
        await d1_run(
            env,
            """INSERT INTO funds_received (scope, key, name, lamports, last_ts)
               VALUES (?, ?, ?, ?, ?)
               ON CONFLICT(scope, key) DO UPDATE SET
                 lamports = lamports + excluded.lamports,
                 name = COALESCE(excluded.name, funds_received.name),
                 last_ts = excluded.last_ts""",
            scope, key, name or None, int(lamports), int(Date.now()),
        )
    except Exception:
        pass


async def _record_bounty_payout(env, rec, transfers):
    # After a bounty escrow is split, credit the payee (contributor) and the
    # owner/repo the bounty belonged to (project) with the payee's share. The
    # treasury cut is intentionally not recorded.
    treasury = _treasury_address(env)
    payee = rec.get("payee", "")
    owner = clean_string(rec.get("owner", ""), MAX_NODE_NAME)
    repo = clean_string(rec.get("repo", ""), 120)
    for addr, lamports in transfers:
        if addr == treasury or addr != payee:
            continue
        await _record_funds_received(env, "contributor", addr, "", lamports)
        if owner and repo:
            await _record_funds_received(
                env, "project", owner + "/" + repo, owner + "/" + repo, lamports)


def _catalog_updated_ms(rec):
    # Best-effort parse of a catalog record's free-form updatedAt string, so we
    # can tell which of a node's several repo mirrors last reported in (used to
    # pick the "latest" commit/platform/version for the node as a whole). Any
    # unparseable value sorts last rather than raising.
    try:
        ms = float(Date.parse(str(rec.get("updatedAt") or "")))
        return ms if ms == ms else -1  # NaN check (NaN != NaN)
    except Exception:
        return -1


async def network_leaderboards(env):
    cached = await edge_cache_match(NETWORK_LEADERBOARDS_CACHE_KEY)
    if cached is not None:
        return cached
    await ensure_schema(env)
    now = int(Date.now())

    # --- Uptime: rank nodes by total minutes online over the retained window.
    # online_hourly_nodes is pruned to ONLINE_HISTORY_RETAIN_MS, so this is
    # inherently a "last 48h" board. label is plaintext (the node's own name).
    window_start = now - ONLINE_HISTORY_RETAIN_MS
    uptime_rows = await d1_all(
        env,
        "SELECT node_key, label, node_minutes FROM online_hourly_nodes "
        "WHERE hour_ts >= ?",
        window_start,
    )
    uptime = {}
    for row in uptime_rows:
        key = str(row.get("node_key") or "")
        if not key:
            continue
        item = uptime.setdefault(key, {"label": "", "minutes": 0})
        item["minutes"] += int(row.get("node_minutes") or 0)
        if row.get("label"):
            item["label"] = clean_string(row.get("label", ""), MAX_NODE_NAME)
    uptime_board = [
        {"name": v["label"] or _short_presence_label("node", k),
         "minutes": v["minutes"]}
        for k, v in uptime.items() if v["minutes"] > 0
    ]
    uptime_board.sort(key=lambda n: (-n["minutes"], n["name"]))

    # --- Repos / mirrors / longest-hosted: one pass over the public catalog
    # (public only, blocked identities excluded). counts -> repos-per-owner;
    # mirror_owners -> distinct owners hosting a repo of a given name (a repo
    # mirrored by many nodes shows up under many owners); first-hosted timestamps
    # come from repo_first_hosted joined on key_bi.
    first_rows = await d1_all(env, "SELECT repo_bi, ts FROM repo_first_hosted")
    first_hosted = {str(r.get("repo_bi")): int(r.get("ts") or 0)
                    for r in first_rows if r.get("repo_bi")}
    repo_rows = await d1_all(
        env, "SELECT key_bi, data FROM repositories WHERE is_private = 0")
    counts = {}
    mirror_owners = {}
    hosted_board = []
    largest_board = []          # per owner/repo, by reported mirror size
    bytes_by_owner = {}         # owner -> total bytes hosted across their repos
    # Per-node detail card for the Network page's "Connected nodes" list: sums
    # the per-repo counters a node reports (adhoc #56's commit/issues/platform/
    # version/id fields) across every repo it mirrors, and keeps the commit/
    # branch/platform/version/sync-time from whichever of its repos reported in
    # most recently — so a multi-repo node shows one coherent "latest" state.
    node_details = {}
    for row in repo_rows:
        rec = await decrypt_row(env, row.get("data"))
        if not rec:
            continue
        if _is_blocked_catalog_identity(env, rec.get("owner"), rec.get("name")):
            continue
        owner = clean_string(rec.get("owner", ""), MAX_NODE_NAME)
        name = clean_string(rec.get("name", ""), 120)
        if not owner:
            continue
        counts[owner] = counts.get(owner, 0) + 1
        try:
            size_bytes = max(0, int(rec.get("sizeBytes", 0) or 0))
        except (TypeError, ValueError):
            size_bytes = 0
        if size_bytes > 0:
            bytes_by_owner[owner] = bytes_by_owner.get(owner, 0) + size_bytes
        detail = node_details.setdefault(owner.lower(), {
            "name": owner, "sizeBytes": 0,
            "issueCount": 0, "commitCount": 0, "branchCount": 0,
            "pullCount": 0, "discussionCount": 0, "artifactCount": 0,
            "clonesServed": 0, "websiteServed": 0,
            "commit": "", "branch": "", "lastSync": "",
            "platform": "", "version": "", "_updatedMs": -1,
        })
        detail["sizeBytes"] += size_bytes
        for field in ("issueCount", "commitCount", "branchCount", "pullCount",
                      "discussionCount", "artifactCount", "clonesServed",
                      "websiteServed"):
            try:
                detail[field] += max(0, int(rec.get(field, 0) or 0))
            except (TypeError, ValueError):
                pass
        updated_ms = _catalog_updated_ms(rec)
        if updated_ms > detail["_updatedMs"]:
            detail["_updatedMs"] = updated_ms
            detail["commit"] = clean_string(rec.get("commit", ""), 64)
            detail["branch"] = clean_string(rec.get("branch", ""), 120)
            detail["lastSync"] = clean_string(rec.get("lastSync", ""), 32)
            detail["platform"] = clean_string(rec.get("platform", ""), 16)
            detail["version"] = clean_string(rec.get("version", ""), 32)
        if name:
            mirror_owners.setdefault(name, set()).add(owner.lower())
            if size_bytes > 0:
                largest_board.append(
                    {"name": owner + "/" + name, "bytes": size_bytes})
            ts = first_hosted.get(str(row.get("key_bi")))
            if ts:
                hosted_board.append(
                    {"name": owner + "/" + name, "since": ts,
                     "ageMs": max(0, now - ts)})
    node_board = [
        {k: v for k, v in detail.items() if k != "_updatedMs"}
        for detail in node_details.values()
    ]
    node_board.sort(key=lambda n: (-n["sizeBytes"], n["name"]))

    repo_board = [{"name": o, "repos": c} for o, c in counts.items()]
    repo_board.sort(key=lambda n: (-n["repos"], n["name"]))

    # --- Most mirrored: repo names hosted under more than one owner.
    mirror_board = [{"name": nm, "mirrors": len(owners)}
                    for nm, owners in mirror_owners.items() if len(owners) > 1]
    mirror_board.sort(key=lambda n: (-n["mirrors"], n["name"]))

    # --- Longest hosted: oldest first-hosted timestamp wins.
    hosted_board.sort(key=lambda n: (-n["ageMs"], n["name"]))

    # --- Largest repos / most data hosted: from the size each node reports for
    # its mirror. largest_board ranks individual repos; data_board sums per owner.
    largest_board.sort(key=lambda n: (-n["bytes"], n["name"]))
    data_board = [{"name": o, "bytes": b} for o, b in bytes_by_owner.items()]
    data_board.sort(key=lambda n: (-n["bytes"], n["name"]))

    # --- Contributor activity: cumulative issues + PRs + commits per author.
    contrib_rows = await d1_all(
        env,
        "SELECT name, issues, pulls, commits, total FROM contributor_activity "
        "WHERE total > 0 ORDER BY total DESC LIMIT ?",
        LEADERBOARD_LIMIT,
    )
    contrib_board = [
        {"name": clean_string(r.get("name", ""), MAX_NODE_NAME) or "contributor",
         "total": int(r.get("total") or 0),
         "issues": int(r.get("issues") or 0),
         "pulls": int(r.get("pulls") or 0),
         "commits": int(r.get("commits") or 0)}
        for r in contrib_rows if int(r.get("total") or 0) > 0
    ]

    # --- Funds received: separate boards for mainnodes, contributors, projects.
    # Recipients of node/contributor payouts are stored by wallet; resolve a
    # friendly node name where we can, else show a shortened address.
    funds = await _funds_received_boards(env)

    resp = json_response(
        {"ok": True,
         "windowHours": ONLINE_HISTORY_RETAIN_MS // 3600000,
         "uptime": uptime_board[:LEADERBOARD_LIMIT],
         "nodes": node_board,
         "repos": repo_board[:LEADERBOARD_LIMIT],
         "mirrors": mirror_board[:LEADERBOARD_LIMIT],
         "hosted": hosted_board[:LEADERBOARD_LIMIT],
         "largest": largest_board[:LEADERBOARD_LIMIT],
         "dataHosted": data_board[:LEADERBOARD_LIMIT],
         "contributors": contrib_board,
         "fundsMainnodes": funds["mainnode"][:LEADERBOARD_LIMIT],
         "fundsContributors": funds["contributor"][:LEADERBOARD_LIMIT],
         "fundsProjects": funds["project"][:LEADERBOARD_LIMIT]},
        cache_seconds=NETWORK_STATS_TTL,
    )
    await edge_cache_put(NETWORK_LEADERBOARDS_CACHE_KEY, resp)
    return resp


def _short_wallet(addr):
    addr = (addr or "").strip()
    if len(addr) <= 10:
        return addr or "node"
    return addr[:4] + "…" + addr[-4:]


async def _wallet_name_map(env):
    # Build wallet -> friendly node name from local accounts and federated
    # presence, so funds-received boards can label payout wallets. Best-effort.
    mapping = {}
    try:
        rows = await d1_all(env, "SELECT name, data FROM accounts")
        for row in rows:
            rec = await decrypt_row(env, row.get("data"))
            if not rec:
                continue
            wallet = (rec.get("solana") or "").strip()
            if not wallet:
                continue
            name = clean_string(row.get("name") or rec.get("name", ""), MAX_NODE_NAME)
            if name:
                mapping.setdefault(wallet, name)
    except Exception:
        pass
    try:
        fed = await d1_all(env, "SELECT wallet, name FROM federated_presence")
        for row in fed:
            wallet = (row.get("wallet") or "").strip()
            name = clean_string(row.get("name", ""), MAX_NODE_NAME)
            if wallet and name:
                mapping.setdefault(wallet, name)
    except Exception:
        pass
    return mapping


async def _funds_received_boards(env):
    out = {"mainnode": [], "contributor": [], "project": []}
    try:
        rows = await d1_all(
            env, "SELECT scope, key, name, lamports FROM funds_received "
                 "WHERE lamports > 0")
    except Exception:
        return out
    wallet_names = None
    for row in rows:
        scope = row.get("scope")
        if scope not in out:
            continue
        lamports = int(row.get("lamports") or 0)
        if lamports <= 0:
            continue
        key = row.get("key") or ""
        name = clean_string(row.get("name") or "", 120)
        if scope in ("mainnode", "contributor") and not name:
            if wallet_names is None:
                wallet_names = await _wallet_name_map(env)
            name = wallet_names.get(key) or _short_wallet(key)
        out[scope].append({"name": name or _short_wallet(key),
                           "lamports": lamports, "sol": _amount_sol(lamports)})
    for scope in out:
        out[scope].sort(key=lambda n: (-n["lamports"], n["name"]))
    return out


async def install_source(env):
    await ensure_schema(env)
    now = int(Date.now())
    # The installer needs an exact answer: a host WebSocket can still be live
    # after an older desktop client's D1 presence row ages out. Decode the small
    # set of catalog entries named "forkmesh", then ask each repository's
    # Durable Object for its real connected-host count. This also avoids ever
    # selecting a stale presence row left by an unclean disconnect.
    rows = await d1_all(
        env,
        "SELECT owner_bi, data FROM repositories",
    )

    candidates = {}
    for row in rows:
        rec = await decrypt_row(env, row.get("data"))
        if not rec or safe_segment(rec.get("name", "")) != "forkmesh":
            continue
        owner = safe_segment(rec.get("owner", ""))
        owner_bi = row.get("owner_bi")
        if not owner or not owner_bi:
            continue
        try:
            host_id = env.FORKMESH_HOST.idFromName(f"host:{owner}/forkmesh")
            host_object = env.FORKMESH_HOST.get(host_id)
            response = await host_object.fetch(
                f"https://forkmesh.internal/api/repo/{owner}/forkmesh/host"
            )
            status = await response.json()
            # workers.Response.json() may cross the Python/JS boundary as either
            # a native dict or a JsProxy depending on where the response was
            # created; accept both shapes.
            hosts = (status.get("hosts", 0) if isinstance(status, dict)
                     else getattr(status, "hosts", 0))
            if int(hosts or 0) > 0:
                candidates[owner_bi] = owner
        except Exception:
            # A single unavailable DO must not stop another live mirror from
            # being selected.
            continue

    if not candidates:
        return json_response({"ok": False, "error": "no_online_install_source"},
                             status=503,
                             cache_control="no-store, max-age=0, must-revalidate")

    start = now - ONLINE_HISTORY_RETAIN_MS
    uptime_rows = await d1_all(
        env,
        """SELECT node_key, SUM(node_minutes) AS total_minutes
             FROM online_hourly_nodes
             WHERE hour_ts >= ?
             GROUP BY node_key""",
        start,
    )
    totals = {r.get("node_key"): int(r.get("total_minutes") or 0)
              for r in uptime_rows}
    ranked = sorted(
        (
            {"node": node, "totalMinutes": totals.get(node_key, 0)}
            for node_key, node in candidates.items()
        ),
        key=lambda item: (-item["totalMinutes"], item["node"]),
    )
    best = ranked[0]
    # Hand the installer the whole ranked list (capped), not just the top pick,
    # so it can fall back to the next online mirror when the best one's git
    # tunnel is unreachable. A node can hold a live host WebSocket — which is all
    # the "hosts > 0" check above proves, so it counts as online here — yet still
    # time out the clone proxy with a 504, which would otherwise dead-end the
    # install. "node" stays for older installers that read only the single best.
    node_list = [item["node"] for item in ranked[:8]]
    return json_response(
        {"ok": True, "node": best["node"], "nodes": node_list,
         "repo": "forkmesh", "totalMinutes": best["totalMinutes"]},
        cache_control="no-store, max-age=0, must-revalidate",
    )


def method_name(request):
    method = getattr(request, "method", "GET")
    return str(method).upper()



def room_key_from_path(pathname):
    match = REPO_ROOM_RE.match(pathname)
    if match:
        owner = safe_segment(match.group(1))
        repo = safe_segment(match.group(2))
        room = safe_segment(match.group(3), MAX_ROOM_NAME)
        if not owner or not repo or not room:
            return None
        return {
            "key": f"repo:{owner}/{repo}:room:{room}",
            "owner": owner,
            "repo": repo,
            "room": room,
            "compat": False,
        }

    match = ROOM_RE.match(pathname)
    if not match:
        return None

    room = safe_segment(match.group(1), MAX_ROOM_NAME)
    if not room:
        return None
    return {
        "key": f"legacy:room:{room}",
        "owner": "",
        "repo": "",
        "room": room,
        "compat": True,
    }


def clean_avatar_png(value):
    if not isinstance(value, str):
        return "", "bad_avatar"
    avatar = value.strip()
    if avatar.startswith("data:image/png;base64,"):
        avatar = avatar.split(",", 1)[1].strip()
    if not avatar:
        return "", ""
    if len(avatar) > MAX_AVATAR_B64:
        return "", "avatar_too_large"
    try:
        raw = base64.b64decode(avatar, validate=True)
    except Exception:
        return "", "bad_avatar"
    if len(raw) > MAX_AVATAR_BYTES:
        return "", "avatar_too_large"
    if not raw.startswith(PNG_HEADER):
        return "", "bad_avatar"
    return base64.b64encode(raw).decode(), ""


# Owners (or repo names) that must never appear in the public catalog / under
# /network/, and may never register host presence. Seeded with a known phantom
# account that kept re-publishing; extend without a code change via the
# BLOCKED_CATALOG_OWNERS env var (comma/space separated, case-insensitive).
_BLOCKED_CATALOG_DEFAULT = {"7cbaf0dc"}


def _blocked_catalog_set(env):
    raw = (getattr(env, "BLOCKED_CATALOG_OWNERS", "") or "").replace(",", " ")
    extra = {part.strip().lower() for part in raw.split() if part.strip()}
    return _BLOCKED_CATALOG_DEFAULT | extra


def _is_blocked_catalog_identity(env, *values):
    blocked = _blocked_catalog_set(env)
    for value in values:
        if value and str(value).strip().lower() in blocked:
            return True
    return False


async def purge_blocked_catalog(env):
    # Delete any catalog rows (and their host-presence rows) belonging to a
    # blocked owner, so phantom entries already in D1 disappear and stay gone.
    for owner in _blocked_catalog_set(env):
        owner_bi = await blind_index(env, owner)
        rows = await d1_all(
            env, "SELECT key_bi FROM repositories WHERE owner_bi=?", owner_bi)
        for r in (rows or []):
            await d1_run(
                env, "DELETE FROM host_presence WHERE repo_bi=?", r["key_bi"])
        await d1_run(env, "DELETE FROM repositories WHERE owner_bi=?", owner_bi)


# --- D1 storage --------------------------------------------------------------
# Durable Objects are reserved for transient relaying (chat rooms + the live
# file/git tunnel). Everything that must persist lives in D1, and every row is
# encrypted at rest: each table stores HMAC "blind index" columns (for lookups
# and uniqueness) plus a single AES-GCM-encrypted JSON `data` blob. The worker
# holds DATA_KEY, so this protects data at rest but is not zero-knowledge.

PBKDF2_ITERS = 100000
MIN_ACTIVE_LAMPORTS = 1000000  # 0.001 SOL proves the wallet is active/funded
SOLANA_RE = re.compile(r"^[1-9A-HJ-NP-Za-km-z]{32,44}$")

# Donation funnel: each signup gets its OWN freshly generated Solana deposit
# address so we can watch that single address go from a zero balance to the
# required amount (low-level getBalance, no transaction indexer needed). The
# worker holds the deposit key (encrypted at rest) so funds can later be swept
# to the treasury.
LAMPORTS_PER_SOL = 1000000000
# The minimum join donation targets ~$1, derived from the live SOL/USD rate and
# rounded up (see _min_join_lamports). These bound that calculation so a bad
# price read can never set an absurd minimum, and act as the fallback when no
# price is available.
MIN_JOIN_USD = 1.0
MIN_JOIN_LAMPORTS_FLOOR = 2_000_000      # 0.002 SOL — never ask for less
MIN_JOIN_LAMPORTS_CEILING = 200_000_000  # 0.2 SOL — never ask for more
MIN_JOIN_LAMPORTS = 5_000_000            # 0.005 SOL — used only if pricing fails
# Sane bounds for a fetched SOL/USD price (USD per 1 SOL).
SOL_USD_MIN = 1.0
SOL_USD_MAX = 100_000.0
# Process-local price cache so we don't refetch on every signup poll.
_SOL_USD_CACHE = {"usd": 0.0, "ts": 0}
_SOL_USD_CACHE_TTL_MS = 5 * 60 * 1000
DONATION_ADDRESS_TTL_MS = 60 * 60 * 1000
# After the address expires (hidden, no longer usable) keep it parked for one
# more hour before deleting it outright, so a late payment can still be matched
# during the grace window.
DONATION_ADDRESS_DELETE_GRACE_MS = 60 * 60 * 1000
# Foundational reward split: half of each confirmed donation goes to the
# treasury, the other half is accounted to online nodes that have a payout
# Solana address on file. On-chain payout batching is intentionally separate
# from signup confirmation now that signup payments go directly to treasury.
TREASURY_SPLIT_NUMERATOR = 1
TREASURY_SPLIT_DENOMINATOR = 2
# The node half is split over every online node with a payout wallet that
# actually mirrors a repo for another node (issue #94) — single-repo-only nodes
# add no redundancy, so they don't share the pool. See _mirroring_owners.
SOLANA_SWEEP_FEE_RESERVE_LAMPORTS = 5000
# A node counts as online for payouts if it has sent a heartbeat within this
# window (reuses the host-presence staleness window).
ACCOUNT_PRESENCE_STALE_MS = 10 * 60 * 1000
# Central donation fund (issue #308): a single worker-custodied Solana wallet
# that anyone can donate to. A cron sweeps its whole balance out to the
# currently-online nodes once an hour, so one donation address fans out to every
# node keeping the network alive.
CENTRAL_FUND_DISTRIBUTION_INTERVAL_MS = 60 * 60 * 1000  # distribute hourly
# Don't distribute dust: wait until the fund holds at least this much over the
# fee reserve before sweeping, so a cycle doesn't burn a transaction fee to hand
# out a few lamports each.
CENTRAL_FUND_MIN_DISTRIBUTION_LAMPORTS = 100_000

_schema_ready = False

# Derived WebCrypto keys are pure functions of the DATA_KEY secret, which is
# constant for an isolate's lifetime. Deriving them (SHA-256 digest + importKey,
# two async WebCrypto round trips each) on every blind_index / encrypt_row /
# decrypt_row call was the dominant per-request CPU cost on hot polled endpoints
# (/api/notifications, /api/accounts/<name>). Cache the imported CryptoKeys,
# keyed by the current secret so a secret rotation still takes effect.
_data_key_cache = {"secret": None, "key": None}
_hmac_key_cache = {"secret": None, "key": None}


async def ensure_schema(env):
    global _schema_ready
    if _schema_ready:
        return
    for sql in SCHEMA_STATEMENTS:
        await env.DB.prepare(sql).run()
    # CREATE TABLE IF NOT EXISTS above won't add a column to a repositories table
    # that predates private repos, so add it separately. Idempotent: a second run
    # raises "duplicate column name", which we swallow.
    try:
        await env.DB.prepare(
            "ALTER TABLE repositories ADD COLUMN is_private INTEGER NOT NULL DEFAULT 0"
        ).run()
    except Exception:
        pass
    # Plaintext blind index of the (signature-verified) submitter on each inbox
    # row, so a per-author quota can be enforced with a COUNT instead of
    # decrypting every pending row. Added separately for tables predating it.
    for _tbl in ("issue_inbox", "pull_inbox", "commit_inbox", "discussion_inbox"):
        try:
            await env.DB.prepare(
                "ALTER TABLE " + _tbl + " ADD COLUMN submitter_bi TEXT").run()
        except Exception:
            pass
    # Blind index of the signup IP for duplicate-signup detection (migration 0015);
    # added separately for accounts tables that predate it. Idempotent — a second
    # run raises "duplicate column name", which we swallow.
    try:
        await env.DB.prepare(
            "ALTER TABLE accounts ADD COLUMN ip_bi TEXT").run()
    except Exception:
        pass
    _schema_ready = True


def js_nullish(value):
    return value is None or type(value).__name__ in ("JsNull", "JsUndefined")


def d1_row_to_dict(row):
    if js_nullish(row):
        return None
    if hasattr(row, "to_py"):
        row = row.to_py()
        if js_nullish(row):
            return None
    return dict(row)


async def d1_all(env, sql, *args):
    stmt = env.DB.prepare(sql)
    if args:
        stmt = stmt.bind(*args)
    result = await stmt.all()
    out = []
    results = getattr(result, "results", None)
    if js_nullish(results):
        return out
    for row in results:
        converted = d1_row_to_dict(row)
        if converted is not None:
            out.append(converted)
    return out


async def d1_first(env, sql, *args):
    stmt = env.DB.prepare(sql)
    if args:
        stmt = stmt.bind(*args)
    row = await stmt.first()
    return d1_row_to_dict(row)


async def d1_run(env, sql, *args):
    stmt = env.DB.prepare(sql)
    if args:
        stmt = stmt.bind(*args)
    await stmt.run()


# --- Encryption-at-rest + blind index ---------------------------------------

# Known placeholder values that must never reach production: encrypting custody
# key material under a publicly known key is equivalent to storing it in plaintext.
_INSECURE_DATA_KEYS = frozenset({
    "", "forkmesh-dev-data-key", "forkmesh-dev-data-key-change-me",
})


def _require_data_secret(env):
    # Fail closed: a missing or placeholder DATA_KEY would silently encrypt every
    # custody seed under a public key. Set a real secret (deploy.sh pushes it):
    #   uvx --from workers-py pywrangler secret put DATA_KEY
    secret = (getattr(env, "DATA_KEY", "") or "").strip()
    if secret in _INSECURE_DATA_KEYS:
        raise RuntimeError(
            "DATA_KEY is unset or a known placeholder; refusing to use a public "
            "key for at-rest encryption. Set DATA_KEY as a Worker secret.")
    return secret


async def _data_key(env):
    secret = _require_data_secret(env)
    if _data_key_cache["secret"] == secret and _data_key_cache["key"] is not None:
        return _data_key_cache["key"]
    digest = await js_crypto.subtle.digest("SHA-256", _to_js(secret.encode()))
    key = await js_crypto.subtle.importKey(
        "raw", digest, to_js({"name": "AES-GCM"}), False,
        _to_js(["encrypt", "decrypt"])
    )
    _data_key_cache["secret"] = secret
    _data_key_cache["key"] = key
    return key


async def encrypt_row(env, obj):
    key = await _data_key(env)
    iv = js_crypto.getRandomValues(Uint8Array.new(12))
    plaintext = json.dumps(obj).encode()
    cipher = await js_crypto.subtle.encrypt(
        to_js({"name": "AES-GCM", "iv": iv}), key, _to_js(plaintext)
    )
    blob = bytes(iv.to_py()) + bytes(Uint8Array.new(cipher).to_py())
    return base64.b64encode(blob).decode()


async def decrypt_row(env, stored, key=None):
    try:
        blob = base64.b64decode(stored)
        iv = _to_js(blob[:12])
        cipher = _to_js(blob[12:])
        if key is None:
            key = await _data_key(env)
        plain = await js_crypto.subtle.decrypt(
            to_js({"name": "AES-GCM", "iv": iv}), key, cipher
        )
        return json.loads(bytes(Uint8Array.new(plain).to_py()).decode())
    except Exception:
        return None


async def _hmac_key(env):
    # A distinct key context so the blind-index HMAC key isn't the AES key.
    secret = _require_data_secret(env) + ":blind-index"
    if _hmac_key_cache["secret"] == secret and _hmac_key_cache["key"] is not None:
        return _hmac_key_cache["key"]
    digest = await js_crypto.subtle.digest("SHA-256", _to_js(secret.encode()))
    key = await js_crypto.subtle.importKey(
        "raw", digest, to_js({"name": "HMAC", "hash": "SHA-256"}), False,
        _to_js(["sign"])
    )
    _hmac_key_cache["secret"] = secret
    _hmac_key_cache["key"] = key
    return key


async def blind_index(env, value):
    # Deterministic, searchable HMAC of a normalized identifier (never plaintext).
    key = await _hmac_key(env)
    norm = (value or "").strip().lower().encode()
    sig = await js_crypto.subtle.sign("HMAC", key, _to_js(norm))
    return bytes(Uint8Array.new(sig).to_py()).hex()


# --- Password (PBKDF2-SHA256) -----------------------------------------------

async def hash_password(password):
    salt = js_crypto.getRandomValues(Uint8Array.new(16))
    base_key = await js_crypto.subtle.importKey(
        "raw", _to_js(password.encode()), to_js({"name": "PBKDF2"}), False,
        _to_js(["deriveBits"])
    )
    bits = await js_crypto.subtle.deriveBits(
        to_js({"name": "PBKDF2", "salt": salt, "iterations": PBKDF2_ITERS,
               "hash": "SHA-256"}),
        base_key, 256
    )
    return (base64.b64encode(bytes(salt.to_py())).decode(),
            base64.b64encode(bytes(Uint8Array.new(bits).to_py())).decode())


async def verify_password(password, salt_b64, hash_b64):
    try:
        salt = base64.b64decode(salt_b64)
        expected = base64.b64decode(hash_b64)
        base_key = await js_crypto.subtle.importKey(
            "raw", _to_js(password.encode()), to_js({"name": "PBKDF2"}), False,
            _to_js(["deriveBits"])
        )
        bits = await js_crypto.subtle.deriveBits(
            to_js({"name": "PBKDF2", "salt": _to_js(salt),
                   "iterations": PBKDF2_ITERS, "hash": "SHA-256"}),
            base_key, 256
        )
        got = bytes(Uint8Array.new(bits).to_py())
        return hmac.compare_digest(got, expected)
    except Exception:
        return False


# --- TOTP (RFC 6238, HMAC-SHA1, 6 digits, 30s) ------------------------------

def _b32_decode(secret):
    s = (secret or "").strip().upper().replace(" ", "")
    return base64.b32decode(s + "=" * ((8 - len(s) % 8) % 8))


def gen_totp_secret():
    raw = bytes(js_crypto.getRandomValues(Uint8Array.new(20)).to_py())
    return base64.b32encode(raw).decode().rstrip("=")


async def _totp_at(secret_b32, counter):
    key = await js_crypto.subtle.importKey(
        "raw", _to_js(_b32_decode(secret_b32)),
        to_js({"name": "HMAC", "hash": "SHA-1"}), False, _to_js(["sign"])
    )
    msg = struct.pack(">Q", counter)
    mac = bytes(Uint8Array.new(await js_crypto.subtle.sign(
        "HMAC", key, _to_js(msg))).to_py())
    offset = mac[-1] & 0x0F
    code = ((mac[offset] & 0x7F) << 24 | (mac[offset + 1] & 0xFF) << 16 |
            (mac[offset + 2] & 0xFF) << 8 | (mac[offset + 3] & 0xFF)) % 1000000
    return "%06d" % code


async def totp_verify(secret_b32, code):
    code = (code or "").strip()
    if not code.isdigit() or len(code) != 6:
        return False
    counter = int(Date.now()) // 1000 // 30
    for delta in (-1, 0, 1):  # allow one step of clock skew either way
        if hmac.compare_digest(await _totp_at(secret_b32, counter + delta), code):
            return True
    return False


# --- Catalog (repositories table) -------------------------------------------

async def catalog_rate_check(env, owner_bi):
    # Throttle catalog writes per owner: at most one write per cooldown window.
    # Returns a 429 response when throttled, else None (and records this write).
    now = int(Date.now())
    row = await d1_first(
        env, "SELECT ts FROM catalog_rate WHERE owner_bi=?", owner_bi)
    if row:
        try:
            last = int(row["ts"])
        except (TypeError, ValueError):
            last = 0
        if now - last < CATALOG_WRITE_COOLDOWN_MS:
            return json_response({"error": "rate_limited"}, status=429)
    await d1_run(
        env,
        "INSERT INTO catalog_rate (owner_bi, ts) VALUES (?,?) "
        "ON CONFLICT(owner_bi) DO UPDATE SET ts=excluded.ts",
        owner_bi, now,
    )
    return None


async def catalog_handler(env, request):
    await ensure_schema(env)
    method = method_name(request)
    if method == "GET":
        # A logged-in owner may sign a short-lived forkmesh-catalog-view-v1 token to
        # additionally receive their OWN private repos (anonymous callers, and the
        # static website with no key, still get the public-only list). The result is
        # per-viewer, so an authenticated request must never read from or write to
        # the SHARED public edge cache — that would leak private repos to everyone.
        params = parse_qs(urlparse(request.url).query)
        viewer = safe_segment(params.get("viewer", [""])[0])
        view_ts = clean_string(params.get("ts", [""])[0], 20)
        view_sig = clean_string(params.get("sig", [""])[0], 200)
        authed_viewer = ""
        if viewer and view_sig:
            authed_viewer = await verify_catalog_view_token(
                env, viewer, view_ts, view_sig)
        if not authed_viewer:
            cached = await edge_cache_match(CATALOG_CACHE_KEY)
            if cached is not None:
                return cached
        # Drop any blocked phantom entries from D1 before listing (idempotent,
        # only runs on a cache miss).
        try:
            await purge_blocked_catalog(env)
        except Exception:
            pass
        # Public repos are listed for everyone; an authenticated viewer additionally
        # gets the private repos they own (matched by blind index) AND any private
        # repo another owner has shared with them (issue #9, via the repo_shares
        # ACL). No other owner's un-shared private repos are ever returned.
        if authed_viewer:
            viewer_bi = await blind_index(env, authed_viewer)
            rows = await d1_all(
                env,
                "SELECT key_bi, data FROM repositories "
                "WHERE is_private = 0 OR owner_bi = ? OR key_bi IN "
                "(SELECT repo_bi FROM repo_shares WHERE grantee_bi = ?)",
                viewer_bi, viewer_bi)
        else:
            rows = await d1_all(
                env, "SELECT key_bi, data FROM repositories WHERE is_private = 0")
        # Annotate each repo with whether a host is currently live, computed once
        # here from the presence table (keyed by the same blind index as the repo)
        # instead of the catalog page probing every repo's tunnel DO per visit.
        cutoff = int(Date.now()) - HOST_PRESENCE_STALE_MS
        live_rows = await d1_all(
            env, "SELECT repo_bi FROM host_presence WHERE ts >= ?", cutoff
        )
        live = {r["repo_bi"] for r in live_rows}
        repos = []
        for r in rows:
            rec = await decrypt_row(env, r["data"])
            if rec:
                # Defense in depth: never surface a blocked identity even if a
                # row slipped in before the purge ran.
                if _is_blocked_catalog_identity(
                        env, rec.get("owner"), rec.get("name")):
                    continue
                rec["liveHost"] = r["key_bi"] in live
                # Plaintext flag so clients can badge private repos without
                # re-deriving it from the visibility string.
                rec["isPrivate"] = rec.get("visibility") == "private"
                # A private repo surfaced to a viewer who is NOT its owner can only
                # be here because it was shared with them (issue #9). The client
                # badges it and clones it with the grantee view token, not the
                # owner one.
                rec["sharedWithMe"] = bool(
                    authed_viewer and rec["isPrivate"]
                    and rec.get("owner") != authed_viewer)
                repos.append(rec)
        # Second pass: mark each repo cloneable when its own host is offline but a
        # peer mirroring the same logical repo is online — the relay serves that
        # mirror in place through the repo's own URL (adhoc #61), so the website
        # shows the repo as available (and which nodes are live) instead of a
        # bare "host offline". served_mirror_groups sees the whole public list, so
        # this works even when the freshest live node is a different owner's mirror.
        served = served_mirror_groups(repos)
        for rec in repos:
            rec["cloneOnline"] = repo_clone_online(rec, served)
        repos.sort(key=lambda x: x.get("updatedAt", ""), reverse=True)
        payload = {"ok": True, "repositories": repos[:MAX_CATALOG_REPOS]}
        # Per-viewer responses (with private repos) must not be cached at the shared
        # edge; only the public-only list is cacheable.
        if authed_viewer:
            return json_response(payload)
        resp = json_response(payload, cache_seconds=CATALOG_TTL)
        await edge_cache_put(CATALOG_CACHE_KEY, resp)
        return resp

    if method == "POST":
        try:
            data = await request.json()
        except Exception:
            return json_response({"error": "invalid_json"}, status=400)
        record = safe_catalog_record(data)
        if not record:
            return json_response(
                {"error": "owner_name_and_maintainer_required"}, status=400
            )
        owner = record["owner"]
        # Blocked phantom identities can never (re)enter the catalog.
        if _is_blocked_catalog_identity(env, owner, record["name"]):
            return json_response({"error": "blocked"}, status=403)
        # Repos are namespaced under a registered account, and only that account's
        # key holder may write its namespace. This ties repo identity to the
        # account (fixes duplicate forks) and prevents impersonation. A registered
        # account is REQUIRED — there is no self-asserted-maintainer fallback.
        owner_pub = await _owner_pubkey(env, owner)
        if not owner_pub:
            return json_response({"error": "account_required"}, status=403)
        if record["maintainer"] != owner_pub:
            return json_response({"error": "maintainer_mismatch"}, status=403)
        catalog_sig = clean_string(data.get("catalogSig", ""), 200)
        canonical = ("forkmesh-catalog-v1\n" + owner + "\n" + record["name"] +
                     "\n" + record["updatedAt"]).encode()
        if not await ed25519_verify(owner_pub, catalog_sig, canonical):
            return json_response({"error": "bad_signature"}, status=401)

        # Repo-state attestation: the same owner key signs the fingerprint of the
        # refs it serves, so the relay can later reject a tampered/stale mirror.
        # Reject the whole write if a present attestation doesn't verify (a bad
        # one must never be pinned); absent is allowed for backward compatibility.
        state_hash = record.get("stateHash", "")
        state_sig = record.get("stateSig", "")
        if state_hash or state_sig:
            state_canonical = (
                "forkmesh-repostate-v1\n" + owner + "\n" + record["name"] +
                "\n" + state_hash + "\n" + record["updatedAt"]
            ).encode()
            if not (state_hash and state_sig and await ed25519_verify(
                    owner_pub, state_sig, state_canonical)):
                return json_response({"error": "bad_state_signature"}, status=401)

        owner_bi = await blind_index(env, owner)
        # Anti-spam: throttle writes per owner and cap how many repos one owner may
        # publish, so a single key can't flood the catalog.
        limited = await catalog_rate_check(env, owner_bi)
        if limited is not None:
            return limited

        key_bi = await blind_index(env, owner + "/" + record["name"])
        # Per-owner record cap (an update to an existing repo is always allowed).
        prior_row = await d1_first(
            env, "SELECT data FROM repositories WHERE key_bi=?", key_bi)
        exists = prior_row is not None
        # Reject rollbacks: a replayed older record must not be able to repin an
        # earlier (validly-signed) repo state and downgrade the served refs.
        if exists:
            prior = await decrypt_row(env, prior_row["data"]) or {}
            try:
                if int(record["updatedAt"]) < int(prior.get("updatedAt", 0) or 0):
                    return json_response({"error": "stale_update"}, status=409)
            except (TypeError, ValueError):
                pass
        if not exists:
            cnt = await d1_first(
                env, "SELECT COUNT(*) AS c FROM repositories WHERE owner_bi=?", owner_bi)
            if cnt and cnt.get("c", 0) >= CATALOG_MAX_RECORDS_PER_OWNER:
                return json_response({"error": "too_many_repos"}, status=429)
        enc = await encrypt_row(env, record)
        # is_private is a plaintext mirror of the (signed-write-gated) visibility
        # field so browse/clone gating can check it without decrypting the row.
        is_private = 1 if record["visibility"] == "private" else 0
        await d1_run(
            env,
            """INSERT INTO repositories (key_bi, owner_bi, data, is_private)
               VALUES (?,?,?,?)
               ON CONFLICT(key_bi) DO UPDATE SET
                 owner_bi=excluded.owner_bi, data=excluded.data,
                 is_private=excluded.is_private""",
            key_bi, owner_bi, enc, is_private,
        )
        # A working-copy holder's verified attestation also lands in the pin
        # history, which is what lets an honest mirror lag the source by a few
        # publishes without failing the clone integrity gate (clone_state_pins).
        # Mirrors' ("remote-clone") self-attestations are deliberately NOT
        # recorded — they must match a source pin. Best-effort: a failure here
        # must never fail the publish itself.
        if state_hash and record.get("source") == "local-node":
            try:
                await d1_run(
                    env,
                    "INSERT INTO repo_state_history (key_bi, state_hash, ts) "
                    "VALUES (?,?,?) ON CONFLICT(key_bi, state_hash) "
                    "DO UPDATE SET ts=excluded.ts",
                    key_bi, state_hash.strip().lower(), int(Date.now()),
                )
                await d1_run(
                    env,
                    "DELETE FROM repo_state_history WHERE key_bi=? "
                    "AND state_hash NOT IN (SELECT state_hash FROM "
                    "repo_state_history WHERE key_bi=? ORDER BY ts DESC LIMIT ?)",
                    key_bi, key_bi, STATE_PIN_HISTORY,
                )
            except Exception:
                pass
        # Cap: keep only the most-recent MAX_CATALOG_REPOS.
        rows = await d1_all(env, "SELECT key_bi, data FROM repositories")
        if len(rows) > MAX_CATALOG_REPOS:
            decoded = []
            for r in rows:
                rec2 = await decrypt_row(env, r["data"])
                decoded.append((r["key_bi"], rec2.get("updatedAt", "") if rec2 else ""))
            decoded.sort(key=lambda x: x[1], reverse=True)
            for stale_key, _ in decoded[MAX_CATALOG_REPOS:]:
                await d1_run(env, "DELETE FROM repositories WHERE key_bi=?", stale_key)
        await edge_cache_delete(CATALOG_CACHE_KEY)
        return json_response({"ok": True, "repository": record}, status=201)

    if method == "DELETE":
        params = parse_qs(urlparse(request.url).query)
        owner = safe_segment(params.get("owner", [""])[0])
        name = safe_segment(params.get("name", [""])[0])
        ts = clean_string(params.get("ts", [""])[0], 20)
        sig = clean_string(params.get("sig", [""])[0], 200)
        if not owner or not name or not ts or not sig:
            return json_response({"error": "owner_name_ts_sig_required"}, status=400)
        try:
            skew = abs(int(Date.now()) - int(ts))
        except (TypeError, ValueError):
            skew = LOGIN_MAX_SKEW_MS + 1
        if skew > LOGIN_MAX_SKEW_MS:
            return json_response({"error": "stale_request"}, status=401)

        key_bi = await blind_index(env, owner + "/" + name)
        row = await d1_first(env, "SELECT data FROM repositories WHERE key_bi=?", key_bi)
        if not row:
            return json_response({"ok": True, "deleted": False})
        existing = await decrypt_row(env, row["data"])
        if not existing:
            return json_response({"error": "catalog_record_unreadable"}, status=500)
        owner_pub = await _owner_pubkey(env, owner)
        if not owner_pub:
            return json_response({"error": "account_required"}, status=403)
        canonical = ("forkmesh-catalog-delete-v1\n" + owner + "\n" + name +
                     "\n" + ts).encode()
        if not await ed25519_verify(owner_pub, sig, canonical):
            return json_response({"error": "bad_signature"}, status=401)
        await d1_run(env, "DELETE FROM repositories WHERE key_bi=?", key_bi)
        await edge_cache_delete(CATALOG_CACHE_KEY)
        return json_response({"ok": True, "deleted": True})

    return json_response({"error": "method_not_allowed"}, status=405)


async def repo_mirrors_handler(env, request, owner, repo):
    if method_name(request) != "GET":
        return json_response({"error": "method_not_allowed"}, status=405)
    await ensure_schema(env)
    rows = await d1_all(
        env, "SELECT key_bi, data, is_private FROM repositories WHERE is_private = 0"
    )
    catalog_rows = []
    for row in rows:
        rec = await decrypt_row(env, row.get("data"))
        if not rec:
            continue
        if _is_blocked_catalog_identity(env, rec.get("owner"), rec.get("name")):
            continue
        catalog_rows.append({
            "key_bi": row.get("key_bi"),
            "is_private": int(row.get("is_private") or 0),
            "data": rec,
        })

    presence_rows = await d1_all(env, "SELECT repo_bi, ts FROM host_presence")
    presence = {
        str(r.get("repo_bi")): int(r.get("ts") or 0)
        for r in presence_rows
        if r.get("repo_bi")
    }
    first_rows = await d1_all(env, "SELECT repo_bi, ts FROM repo_first_hosted")
    first_hosted = {
        str(r.get("repo_bi")): int(r.get("ts") or 0)
        for r in first_rows
        if r.get("repo_bi")
    }
    # Recent owner-attested state pins, so the payload can mark which mirrors
    # the clone integrity gate is rejecting (same gathering as _state_pins).
    history = {}
    hist_rows = await d1_all(
        env, "SELECT key_bi, state_hash FROM repo_state_history")
    for r in hist_rows:
        history.setdefault(str(r.get("key_bi") or ""), []).append(
            r.get("state_hash"))
    payload = build_repo_mirrors_payload(
        owner,
        repo,
        catalog_rows,
        presence,
        first_hosted,
        int(Date.now()),
        HOST_PRESENCE_STALE_MS,
        5 * 1000,
        history,
    )
    if payload is None:
        return json_response({"error": "not_found"}, status=404)
    return json_response(payload)


# --- Release download counts -------------------------------------------------

async def record_release_download(env, repo_bi, sha256):
    """Log one completed release-asset download. Best-effort; never raises —
    called fire-and-forget from the streaming DO response, so a D1 hiccup must
    never fail or delay the download itself."""
    try:
        await ensure_schema(env)
        await d1_run(
            env,
            "INSERT INTO release_downloads (repo_bi, sha256, ts) VALUES (?, ?, ?)",
            repo_bi, sha256, int(Date.now()),
        )
    except Exception:
        pass


async def release_download_counts(env, repo_bi):
    await ensure_schema(env)
    rows = await d1_all(
        env,
        "SELECT sha256, COUNT(*) AS n FROM release_downloads "
        "WHERE repo_bi=? GROUP BY sha256",
        repo_bi,
    )
    return {str(r.get("sha256")): int(r.get("n") or 0) for r in rows if r.get("sha256")}


async def release_downloads_handler(env, request, owner, repo):
    if method_name(request) != "GET":
        return json_response({"error": "method_not_allowed"}, status=405)
    repo_bi = await blind_index(env, owner + "/" + repo)
    counts = await release_download_counts(env, repo_bi)
    return json_response({"ok": True, "counts": counts})


# --- Public waitlist (waitlist table) ---------------------------------------

async def waitlist_handler(env, request):
    await ensure_schema(env)
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    if not isinstance(data, dict):
        return json_response({"error": "invalid_json"}, status=400)
    email = normalize_email(data.get("email", ""))
    if not email:
        return json_response({"error": "valid_email_required"}, status=400)
    source = clean_string(data.get("source", "features"), MAX_WAITLIST_FIELD)
    path = clean_string(data.get("path", ""), MAX_WAITLIST_FIELD)
    user_agent = clean_string(request.headers.get("user-agent") or "", 512)
    await d1_run(
        env,
        """INSERT OR IGNORE INTO waitlist
           (email, created_at, source, path, user_agent)
           VALUES (?,?,?,?,?)""",
        email, int(Date.now()), source, path, user_agent,
    )
    return json_response({"ok": True}, status=201)


# --- Accounts (accounts table) — name + solana + password + TOTP ------------

async def _account_row(env, name):
    name_bi = await blind_index(env, name)
    row = await d1_first(env, "SELECT data FROM accounts WHERE name_bi=?", name_bi)
    if not row:
        return name_bi, None
    return name_bi, await decrypt_row(env, row["data"])


async def _owner_pubkey(env, owner):
    _, rec = await _account_row(env, owner)
    return rec.get("pubkey", "") if rec else ""


async def _account_row_by_pubkey(env, pubkey):
    # Node accounts aren't indexed by key (only by name_bi), so this is a full
    # scan — the same tradeoff _wallet_name_map already makes at this
    # project's scale. Only reached as a claim-node fallback when the input
    # isn't shaped like a node name (see valid_node_pubkey).
    rows = await d1_all(env, "SELECT name_bi, data FROM accounts")
    for row in rows:
        rec = await decrypt_row(env, row.get("data"))
        if rec and rec.get("pubkey") == pubkey:
            return row["name_bi"], rec
    return None, None


async def _resolve_claimable_node(env, raw_id):
    # A claim-node "node ID" may be the account's chosen name (the common
    # case) or its Ed25519 public key, which the desktop app's own profile
    # card labels "Node ID" (issue #351). Returns (node_bi, node_rec,
    # node_name) or (None, None, "") when neither resolves.
    candidate = (raw_id or "").strip()
    name = candidate.lower()
    if valid_node_name(name):
        node_bi, node_rec = await _account_row(env, name)
        if node_rec:
            return node_bi, node_rec, name
    if valid_node_pubkey(candidate):
        node_bi, node_rec = await _account_row_by_pubkey(env, candidate)
        if node_rec:
            return node_bi, node_rec, node_rec.get("name", "")
    return None, None, ""


def _ts_ok(ts):
    try:
        return abs(int(Date.now()) - int(ts)) <= LOGIN_MAX_SKEW_MS
    except (TypeError, ValueError):
        return False


async def verify_host_token(env, owner, repo, ts, sig):
    # Only the holder of the owner account's registered key may register as a host
    # for owner/repo. Mirrors _account_heartbeat: fresh ts + ed25519 over an
    # explicit canonical string, verified against the account's pubkey. Returns
    # False when there is no registered account (hard gate — no self-assert).
    if not owner or not repo or not sig or not _ts_ok(ts):
        return False
    pubkey = await _owner_pubkey(env, owner)
    if not pubkey:
        return False
    canonical = ("forkmesh-host-v1\n" + owner + "\n" + repo + "\n" + str(ts)).encode()
    return await ed25519_verify(pubkey, sig, canonical)


async def verify_view_token(env, owner, repo, ts, sig):
    # Read gate for private repos: same shape as verify_host_token but a distinct
    # canonical string so a host token can't be replayed as a view token (and vice
    # versa). Only the owner account's key holder can browse/clone a private repo.
    if not owner or not repo or not sig or not _ts_ok(ts):
        return False
    pubkey = await _owner_pubkey(env, owner)
    if not pubkey:
        return False
    canonical = ("forkmesh-view-v1\n" + owner + "\n" + repo + "\n" + str(ts)).encode()
    return await ed25519_verify(pubkey, sig, canonical)


async def verify_push_token(env, owner, repo, ts, sig):
    # Write gate for git push over the relay (issue #358): only the owner
    # account's registered key may run receive-pack against the served mirror
    # (collaborator keys are a later extension). Same shape as verify_host_token /
    # verify_view_token — fresh ts + ed25519 over an explicit canonical string —
    # but a distinct prefix so a host or view token can never be replayed as a
    # push token, and vice versa. This ts-signed binding is deliberately the
    # simplest thing that stays in the existing trust model (no server-side nonce
    # state); the freshness window is the short-lived element.
    if not owner or not repo or not sig or not _ts_ok(ts):
        return False
    pubkey = await _owner_pubkey(env, owner)
    if not pubkey:
        return False
    canonical = ("forkmesh-push-v1\n" + owner + "\n" + repo + "\n" + str(ts)).encode()
    return await ed25519_verify(pubkey, sig, canonical)


async def _basic_auth_push_ok(env, owner, repo, request):
    # git supplies the push token in HTTP Basic auth (password="<ts>.<sig>"),
    # from the push URL or a credential helper. The username selects whose key
    # signed it; only the owner may push for now (collaborator keys later).
    header = request.headers.get("authorization") or ""
    if not header.lower().startswith("basic "):
        return False
    try:
        decoded = base64.b64decode(header[6:].strip()).decode("utf-8", "replace")
    except Exception:
        return False
    username, _, password = decoded.partition(":")
    ts, _, sig = password.partition(".")
    if not ts or not sig:
        return False
    if username and username != owner:
        return False
    return await verify_push_token(env, owner, repo, ts, sig)


async def verify_catalog_view_token(env, viewer, ts, sig):
    # Listing gate: lets a logged-in owner additionally see their OWN private repos
    # in the catalog, which is otherwise public-only. Not bound to a single repo
    # (it authorizes a listing, not one clone) and uses a distinct canonical string
    # so a per-repo forkmesh-view-v1 token can't be replayed as a listing token and
    # vice versa. Returns the viewer's account name on success, "" otherwise — an
    # unknown/garbled viewer simply falls back to the public-only catalog.
    if not viewer or not sig or not _ts_ok(ts):
        return ""
    pubkey = await _owner_pubkey(env, viewer)
    if not pubkey:
        return ""
    canonical = ("forkmesh-catalog-view-v1\n" + viewer + "\n" + str(ts)).encode()
    if await ed25519_verify(pubkey, sig, canonical):
        return viewer
    return ""


async def _repo_shared_with(env, owner, repo, grantee):
    # True when the owner has shared `owner/repo` with the `grantee` account
    # (issue #9). repo_bi is keyed the same way as repositories.key_bi so it joins
    # a private repo to its collaborator rows. Best-effort: any error => not shared
    # (fail closed; a transient DB hiccup must never grant access it shouldn't).
    if not owner or not repo or not grantee:
        return False
    try:
        await ensure_schema(env)
        repo_bi = await blind_index(env, owner + "/" + repo)
        grantee_bi = await blind_index(env, grantee)
        row = await d1_first(
            env,
            "SELECT 1 AS one FROM repo_shares WHERE repo_bi=? AND grantee_bi=?",
            repo_bi, grantee_bi)
        return bool(row)
    except Exception:
        return False


async def verify_share_view_token(env, viewer, owner, repo, ts, sig):
    # Read gate for a private repo shared with a DIFFERENT account (issue #9). The
    # grantee signs with their OWN key — the owner never hands out its key — and
    # access is granted only while the owner keeps an active repo_shares row for
    # them. Distinct canonical prefix so an owner forkmesh-view-v1 token, a host
    # token, or a catalog-listing token can never be replayed here, and vice versa.
    if not viewer or not owner or not repo or not sig or not _ts_ok(ts):
        return False
    if viewer == owner:
        return False  # the owner authenticates via verify_view_token, not here
    pubkey = await _owner_pubkey(env, viewer)
    if not pubkey:
        return False
    canonical = ("forkmesh-share-view-v1\n" + viewer + "\n" + owner + "\n" +
                 repo + "\n" + str(ts)).encode()
    if not await ed25519_verify(pubkey, sig, canonical):
        return False
    return await _repo_shared_with(env, owner, repo, viewer)


async def _repo_is_private(env, owner, repo):
    # Plaintext is_private flag for owner/repo. A missing catalog row means the
    # repo was never published (e.g. an ad-hoc host) and stays public, preserving
    # the pre-private-repos behavior. Best-effort: any error => treat as public so
    # a transient DB hiccup can't lock everyone out of public repos.
    try:
        await ensure_schema(env)
        key_bi = await blind_index(env, owner + "/" + repo)
        row = await d1_first(
            env, "SELECT is_private FROM repositories WHERE key_bi=?", key_bi)
        return bool(row and row.get("is_private"))
    except Exception:
        return False


async def _basic_auth_view_ok(env, owner, repo, request):
    # Git smart-HTTP carries the view token in HTTP Basic auth (password=
    # "<ts>.<sig>"), which git supplies from the clone URL or a credential helper.
    # The username selects whose key signed the token: the owner itself
    # (forkmesh-view-v1) or a collaborator the repo was shared with, who signs
    # with their OWN key (forkmesh-share-view-v1, issue #9).
    header = request.headers.get("authorization") or ""
    if not header.lower().startswith("basic "):
        return False
    try:
        decoded = base64.b64decode(header[6:].strip()).decode("utf-8", "replace")
    except Exception:
        return False
    username, _, password = decoded.partition(":")
    ts, _, sig = password.partition(".")
    if not ts or not sig:
        return False
    if username and username != owner:
        return await verify_share_view_token(env, username, owner, repo, ts, sig)
    return await verify_view_token(env, owner, repo, ts, sig)


def _basic_auth_challenge():
    return Response(
        "Authentication required.",
        status=401,
        headers={"WWW-Authenticate": 'Basic realm="forkmesh"'},
    )


async def _save_account(env, name_bi, rec, email_bi=None, ip_bi=None):
    # Persist the encrypted record; pass email_bi to (re)index for email login and
    # ip_bi to (re)index the signup IP for duplicate detection. Both are only
    # written when supplied, so a caller that doesn't have them in hand (e.g. a
    # donation-poll save) never clobbers a value set at finalize. The plaintext
    # `name` column mirrors the (public) node name so an operator can grant admin
    # in the DB by name; is_admin is never written here, so a value set directly in
    # the DB survives ordinary account updates.
    enc = await encrypt_row(env, rec)
    name = rec.get("name", "")
    # Column names here are fixed literals (never user input), so building the
    # statement by name is safe.
    cols = ["data", "name"]
    vals = [enc, name]
    if email_bi is not None:
        cols.append("email_bi")
        vals.append(email_bi)
    if ip_bi is not None:
        cols.append("ip_bi")
        vals.append(ip_bi)
    insert_cols = ", ".join(["name_bi"] + cols)
    placeholders = ", ".join(["?"] * (1 + len(vals)))
    set_clause = ", ".join(c + "=excluded." + c for c in cols)
    await d1_run(
        env,
        "INSERT INTO accounts (" + insert_cols + ") VALUES (" + placeholders + ") "
        "ON CONFLICT(name_bi) DO UPDATE SET " + set_clause,
        name_bi, *vals,
    )


async def _save_account_full(env, name_bi, rec, email_bi=None, ip_bi=None,
                             is_admin=0):
    enc = await encrypt_row(env, rec)
    await d1_run(
        env,
        """INSERT INTO accounts (name_bi, data, email_bi, name, is_admin, ip_bi)
           VALUES (?,?,?,?,?,?)
           ON CONFLICT(name_bi) DO UPDATE SET
             data=excluded.data, email_bi=excluded.email_bi,
             name=excluded.name, is_admin=excluded.is_admin,
             ip_bi=excluded.ip_bi""",
        name_bi, enc, email_bi, rec.get("name", ""),
        int(is_admin or 0), ip_bi,
    )


async def _move_repo_shares(env, old_repo_bi, new_repo_bi, new_owner, repo):
    rows = await d1_all(
        env, "SELECT grantee_bi, data, ts FROM repo_shares WHERE repo_bi=?",
        old_repo_bi)
    for row in rows:
        rec = await decrypt_row(env, row.get("data", "")) or {}
        rec["owner"] = new_owner
        rec["repo"] = repo
        enc = await encrypt_row(env, rec)
        await d1_run(
            env,
            """INSERT OR REPLACE INTO repo_shares
               (repo_bi, grantee_bi, data, ts) VALUES (?,?,?,?)""",
            new_repo_bi, row.get("grantee_bi"), enc, row.get("ts", 0))
    await d1_run(env, "DELETE FROM repo_shares WHERE repo_bi=?", old_repo_bi)


async def _move_bounties_namespace(env, old_owner, new_owner, repo,
                                   apply_changes=True):
    rows = await d1_all(env, "SELECT bounty_bi, data FROM issue_bounty")
    moves = []
    for row in rows:
        rec = await decrypt_row(env, row.get("data", "")) or {}
        if (rec.get("owner") != old_owner or rec.get("repo") != repo):
            continue
        try:
            number = int(rec.get("number", 0))
        except (TypeError, ValueError):
            number = 0
        if number <= 0:
            continue
        new_bi = await _bounty_bi(env, new_owner, repo, number,
                                  rec.get("kind", ""))
        old_bi = row.get("bounty_bi")
        if new_bi != old_bi:
            existing = await d1_first(
                env, "SELECT bounty_bi FROM issue_bounty WHERE bounty_bi=?",
                new_bi)
            if existing:
                return "repo_namespace_conflict"
        moves.append((old_bi, new_bi, rec))
    if not apply_changes:
        return ""
    for old_bi, new_bi, rec in moves:
        rec["owner"] = new_owner
        rec["repo"] = repo
        enc = await encrypt_row(env, rec)
        await d1_run(
            env,
            """INSERT INTO issue_bounty (bounty_bi, data) VALUES (?,?)
               ON CONFLICT(bounty_bi) DO UPDATE SET data=excluded.data""",
            new_bi, enc)
        if new_bi != old_bi:
            await d1_run(
                env, "DELETE FROM issue_bounty WHERE bounty_bi=?", old_bi)
    return ""


async def _move_chat_history_namespace(env, old_owner, new_owner, repo):
    old_prefix = "repo:" + old_owner + "/" + repo + ":room:"
    new_prefix = "repo:" + new_owner + "/" + repo + ":room:"
    rows = await d1_all(
        env,
        "SELECT room_key, msg_id, ts, body FROM chat_history WHERE room_key LIKE ?",
        old_prefix + "%")
    for row in rows:
        old_key = row.get("room_key", "")
        new_key = new_prefix + old_key[len(old_prefix):]
        await d1_run(
            env,
            """INSERT OR REPLACE INTO chat_history
               (room_key, msg_id, ts, body) VALUES (?,?,?,?)""",
            new_key, row.get("msg_id"), row.get("ts", 0), row.get("body", ""))
        await d1_run(
            env,
            "DELETE FROM chat_history WHERE room_key=? AND msg_id=?",
            old_key, row.get("msg_id"))


async def _move_repo_namespace(env, old_owner_bi, old_owner, new_owner_bi,
                               new_owner, apply_changes=True):
    rows = await d1_all(
        env, "SELECT key_bi, data, is_private FROM repositories WHERE owner_bi=?",
        old_owner_bi)
    moves = []
    for row in rows:
        rec = await decrypt_row(env, row.get("data", "")) or {}
        if (rec.get("owner", "").lower() != old_owner.lower()):
            continue
        repo = clean_string(rec.get("name", ""), MAX_REPO_SEGMENT)
        if not repo:
            continue
        old_repo_bi = row.get("key_bi")
        new_repo_bi = await blind_index(env, new_owner + "/" + repo)
        if new_repo_bi != old_repo_bi:
            existing = await d1_first(
                env, "SELECT key_bi FROM repositories WHERE key_bi=?",
                new_repo_bi)
            if existing:
                return "repo_namespace_conflict"
        bounty_error = await _move_bounties_namespace(
            env, old_owner, new_owner, repo, apply_changes=False)
        if bounty_error:
            return bounty_error
        moves.append((row, rec, repo, old_repo_bi, new_repo_bi))
    if not apply_changes:
        return ""

    for row, rec, repo, old_repo_bi, new_repo_bi in moves:
        rec["owner"] = new_owner
        enc = await encrypt_row(env, rec)
        await d1_run(
            env,
            """INSERT INTO repositories (key_bi, owner_bi, data, is_private)
               VALUES (?,?,?,?)
               ON CONFLICT(key_bi) DO UPDATE SET
                 owner_bi=excluded.owner_bi, data=excluded.data,
                 is_private=excluded.is_private""",
            new_repo_bi, new_owner_bi, enc, int(row.get("is_private") or 0))
        await _move_repo_shares(env, old_repo_bi, new_repo_bi, new_owner, repo)
        # Carry the attested pin history to the new namespace so mirrors of a
        # renamed source keep clearing the integrity gate without waiting for
        # fresh publishes to rebuild it.
        try:
            await d1_run(
                env, "UPDATE repo_state_history SET key_bi=? WHERE key_bi=?",
                new_repo_bi, old_repo_bi)
        except Exception:
            pass
        await d1_run(
            env, "UPDATE issue_inbox SET repo_bi=? WHERE repo_bi=?",
            new_repo_bi, old_repo_bi)
        await d1_run(
            env, "UPDATE pull_inbox SET repo_bi=? WHERE repo_bi=?",
            new_repo_bi, old_repo_bi)
        await d1_run(
            env, "UPDATE commit_inbox SET repo_bi=? WHERE repo_bi=?",
            new_repo_bi, old_repo_bi)
        await d1_run(
            env, "UPDATE discussion_inbox SET repo_bi=? WHERE repo_bi=?",
            new_repo_bi, old_repo_bi)
        await d1_run(
            env, "UPDATE host_presence SET repo_bi=? WHERE repo_bi=?",
            new_repo_bi, old_repo_bi)
        await d1_run(
            env, "UPDATE clone_rr SET repo_bi=? WHERE repo_bi=?",
            new_repo_bi, old_repo_bi)
        await d1_run(
            env, "UPDATE repo_first_hosted SET repo_bi=? WHERE repo_bi=?",
            new_repo_bi, old_repo_bi)
        await _move_bounties_namespace(env, old_owner, new_owner, repo)
        await _move_chat_history_namespace(env, old_owner, new_owner, repo)
        await d1_run(
            env,
            "UPDATE funds_received SET key=?, name=? WHERE scope='project' AND key=?",
            new_owner + "/" + repo, new_owner + "/" + repo,
            old_owner + "/" + repo)
        if new_repo_bi != old_repo_bi:
            await d1_run(
                env, "DELETE FROM repositories WHERE key_bi=?", old_repo_bi)
    return ""


async def _rename_account_namespace(env, name_bi, rec, new_name):
    old_name = clean_string(rec.get("name", ""), MAX_NODE_NAME).lower()
    new_name = clean_string(new_name, MAX_NODE_NAME).lower()
    if not valid_node_name(new_name):
        return name_bi, rec, "invalid_node_name"
    if new_name == old_name:
        return name_bi, rec, "node_name_unchanged"

    new_name_bi, target = await _account_row(env, new_name)
    if target and (target.get("status") == "active" or
                   target.get("donation_confirmed") or
                   _donation_in_progress(target)):
        return name_bi, rec, "node_name_taken"

    account_row = await d1_first(
        env, "SELECT data, email_bi, ip_bi, is_admin FROM accounts WHERE name_bi=?",
        name_bi)
    if not account_row:
        return name_bi, rec, "invalid_credentials"

    move_error = await _move_repo_namespace(
        env, name_bi, old_name, new_name_bi, new_name, apply_changes=False)
    if move_error:
        return name_bi, rec, move_error

    next_rec = dict(rec)
    next_rec["name"] = new_name
    await _save_account_full(
        env, new_name_bi, next_rec,
        email_bi=account_row.get("email_bi"),
        ip_bi=account_row.get("ip_bi"),
        is_admin=account_row.get("is_admin", 0),
    )
    move_error = await _move_repo_namespace(
        env, name_bi, old_name, new_name_bi, new_name)
    if move_error:
        return name_bi, rec, move_error
    await d1_run(
        env, "UPDATE account_presence SET name_bi=? WHERE name_bi=?",
        new_name_bi, name_bi)
    await d1_run(
        env, "UPDATE pending_verifications SET name_bi=? WHERE name_bi=?",
        new_name_bi, name_bi)
    await d1_run(
        env, "UPDATE notifications SET recipient_bi=? WHERE recipient_bi=?",
        new_name_bi, name_bi)
    await d1_run(
        env, "UPDATE catalog_rate SET owner_bi=? WHERE owner_bi=?",
        new_name_bi, name_bi)
    await d1_run(env, "DELETE FROM accounts WHERE name_bi=?", name_bi)
    await edge_cache_delete(CATALOG_CACHE_KEY)
    return new_name_bi, next_rec, ""


async def _delete_bounties_namespace(env, owner, repo):
    rows = await d1_all(env, "SELECT bounty_bi, data FROM issue_bounty")
    for row in rows:
        rec = await decrypt_row(env, row.get("data", "")) or {}
        if rec.get("owner") == owner and rec.get("repo") == repo:
            await d1_run(
                env, "DELETE FROM issue_bounty WHERE bounty_bi=?",
                row.get("bounty_bi"))


async def _delete_repo_scoped_state(env, repo_bi):
    await d1_run(env, "DELETE FROM repo_shares WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM issue_inbox WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM pull_inbox WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM commit_inbox WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM discussion_inbox WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM host_presence WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM clone_rr WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM repo_first_hosted WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM repo_agents WHERE repo_bi=?", repo_bi)
    await d1_run(env, "DELETE FROM agent_prompts WHERE repo_bi=?", repo_bi)


async def _delete_repo_namespace(env, owner_bi, owner):
    rows = await d1_all(
        env, "SELECT key_bi, data FROM repositories WHERE owner_bi=?",
        owner_bi)
    owner = clean_string(owner, MAX_NODE_NAME).lower()
    for row in rows:
        repo_bi = row.get("key_bi")
        rec = await decrypt_row(env, row.get("data", "")) or {}
        repo = clean_string(rec.get("name", ""), MAX_REPO_SEGMENT)
        if repo_bi:
            await _delete_repo_scoped_state(env, repo_bi)
            await d1_run(env, "DELETE FROM repositories WHERE key_bi=?", repo_bi)
        if owner and repo:
            await _delete_bounties_namespace(env, owner, repo)
            await d1_run(
                env, "DELETE FROM chat_history WHERE room_key LIKE ?",
                "repo:" + owner + "/" + repo + ":room:%")
            await d1_run(
                env,
                "DELETE FROM funds_received WHERE scope='project' AND key=?",
                owner + "/" + repo)
    await d1_run(env, "DELETE FROM catalog_rate WHERE owner_bi=?", owner_bi)
    await edge_cache_delete(CATALOG_CACHE_KEY)


async def _delete_account_namespace(env, name_bi, rec):
    name = clean_string(rec.get("name", ""), MAX_NODE_NAME).lower()
    email = clean_string(rec.get("email", ""), 254).strip().lower()
    await _delete_repo_namespace(env, name_bi, name)
    await d1_run(env, "DELETE FROM repo_shares WHERE grantee_bi=?", name_bi)
    await d1_run(env, "DELETE FROM account_presence WHERE name_bi=?", name_bi)
    await d1_run(env, "DELETE FROM pending_verifications WHERE name_bi=?", name_bi)
    await d1_run(env, "DELETE FROM notifications WHERE recipient_bi=?", name_bi)
    await d1_run(env, "DELETE FROM login_attempts WHERE id_bi=?", name_bi)
    if email:
        email_bi = await blind_index(env, email)
        await d1_run(env, "DELETE FROM login_attempts WHERE id_bi=?", email_bi)
    await d1_run(env, "DELETE FROM accounts WHERE name_bi=?", name_bi)


# Users vs nodes (adhoc #53). Both kinds live in the accounts table; the kind is
# derived from the record: an account with login credentials is a "user" (a
# person — their desktop node is intrinsically theirs), a key-bound-only account
# (e.g. a headless mirror's auto-registration) is a "node" a user can claim.
def _account_kind(rec):
    return "user" if rec.get("pass_hash") else "node"


def _owned_nodes(rec):
    nodes = rec.get("nodes")
    if not isinstance(nodes, list):
        return []
    return [n for n in nodes if isinstance(n, str) and n]


async def _account_public_payload(env, rec):
    name = rec.get("name", "")
    solana = (rec.get("solana") or "").strip()
    has_payout = bool(solana and SOLANA_RE.match(solana))
    is_admin = await _is_admin(env, name)
    payload = {
        "ok": True,
        "nodeName": name,
        "email": rec.get("email", ""),
        "status": rec.get("status", "active"),
        "pubkey": rec.get("pubkey", ""),
        "emailVerified": bool(rec.get("email_verified")),
        "isAdmin": is_admin,
        "solana": solana if has_payout else "",
        "hasPayoutAddress": has_payout,
        "avatarPng": rec.get("avatar_png", ""),
        "avatarUpdatedAt": rec.get("avatar_updated_at", 0),
        "kind": _account_kind(rec),
        "owner": rec.get("owner", ""),
        "nodes": _owned_nodes(rec),
    }
    if is_admin:
        admin_path = _admin_path(env)
        if admin_path:
            payload["adminUrl"] = "/" + admin_path
    return payload


def _donation_expiry_fields(rec, now):
    try:
        created = int(rec.get("donation_created_at") or rec.get("created_at") or now)
    except (TypeError, ValueError):
        created = now
    try:
        expires = int(rec.get("donation_expires_at") or
                      created + DONATION_ADDRESS_TTL_MS)
    except (TypeError, ValueError):
        expires = created + DONATION_ADDRESS_TTL_MS
    try:
        delete_after = int(rec.get("donation_delete_after") or
                           expires + DONATION_ADDRESS_DELETE_GRACE_MS)
    except (TypeError, ValueError):
        delete_after = expires + DONATION_ADDRESS_DELETE_GRACE_MS
    return created, expires, delete_after


def _ensure_donation_expiry_fields(rec, now):
    if not rec.get("donation_address"):
        return False
    created, expires, delete_after = _donation_expiry_fields(rec, now)
    before = (
        rec.get("donation_created_at"),
        rec.get("donation_expires_at"),
        rec.get("donation_delete_after"),
    )
    rec["donation_created_at"] = created
    rec["donation_expires_at"] = expires
    rec["donation_delete_after"] = delete_after
    return before != (created, expires, delete_after)


def _clear_donation_address(rec):
    for key in (
        "donation_address", "donation_reference", "donation_required_lamports",
        "donation_confirmed", "donation_created_at", "donation_expires_at",
        "donation_delete_after", "donation_received_lamports", "donation_secret",
    ):
        rec.pop(key, None)
    if rec.get("status") != "active":
        rec["status"] = "reserved"


# A bare reservation only holds the name while a signup is genuinely in flight:
# the holder has an open, unpaid, unexpired donation request. Once that lapses
# (or never existed) the reservation is abandoned and the name is free again.
def _donation_in_progress(rec):
    if not rec.get("donation_address") or rec.get("donation_confirmed"):
        return False
    _, expires, _ = _donation_expiry_fields(rec, Date.now())
    return Date.now() < expires


# Simple web signup: create an active account from node name + email + password.
# Solana payout details are intentionally handled later from the dashboard profile.
async def _account_signup(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    email = clean_string(data.get("email", ""), 254).strip().lower()
    password = (data.get("password", "") or "")[:256]
    if not valid_node_name(name):
        return json_response({"error": "invalid_node_name"}, status=400)
    if "@" not in email or len(email) < 3:
        return json_response({"error": "valid_email_required"}, status=400)
    if len(password) < 8:
        return json_response({"error": "password_too_short"}, status=400)

    name_bi, existing = await _account_row(env, name)
    if existing and (existing.get("status") == "active" or
                     existing.get("donation_confirmed") or
                     _donation_in_progress(existing)):
        return json_response({"error": "node_name_taken"}, status=409)

    email_bi = await blind_index(env, email)
    dup = await d1_first(env, "SELECT name_bi FROM accounts WHERE email_bi=?", email_bi)
    if dup and dup.get("name_bi") != name_bi:
        return json_response({"error": "email_taken"}, status=409)

    salt, phash = await hash_password(password)
    rec = existing or {}
    rec.update({
        "name": name,
        "email": email,
        "pass_salt": salt,
        "pass_hash": phash,
    })
    rec["status"] = "active"
    rec["email_verified"] = bool(rec.get("email_verified", False))
    rec.setdefault("created_at", int(Date.now()))
    rec.setdefault("signup", _signup_metadata(request))
    signup_ip = (rec.get("signup") or {}).get("ip") or ""
    ip_bi = await blind_index(env, signup_ip) if signup_ip else None
    await _save_account(env, name_bi, rec, email_bi=email_bi, ip_bi=ip_bi)
    if rec.get("email") and not rec["email_verified"]:
        sent = await _send_verification_email(env, request, name, rec["email"])
        if not sent:
            await _enqueue_verification(env, name_bi, name, rec["email"])
    return json_response(await _account_public_payload(env, rec), status=201)


# Step 1 of the funnel: claim a public node name. The desktop client signs the
# claim with its Ed25519 identity (binding the name to a key); the website may
# reserve without a key. A name is only "taken" once it is finalized/paid.
async def _account_reserve(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    pubkey = clean_string(data.get("pubkey", ""), 120)
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    if not valid_node_name(name):
        return json_response({"error": "invalid_node_name"}, status=400)

    name_bi, existing = await _account_row(env, name)
    if existing:
        ex_pub = existing.get("pubkey", "")
        if existing.get("status") == "active" or existing.get("donation_confirmed"):
            return json_response({"error": "node_name_taken"}, status=409)
        held_by_other = bool(ex_pub) and (not pubkey or ex_pub != pubkey)
        # A reservation bound to a different key only blocks the name while that
        # holder's signup is still live (an open, unexpired donation). Otherwise
        # it's an abandoned reservation — a fresh install on a new key (the common
        # case) must be able to reclaim its own name instead of forever hitting
        # "node_name_taken" for a name nobody actually paid for.
        if held_by_other and _donation_in_progress(existing):
            return json_response({"error": "node_name_taken"}, status=409)
        if held_by_other:
            # Reclaiming an abandoned reservation: drop the previous holder's
            # stale state so the new owner starts a clean, key-bound signup.
            existing = None
        # else: a stale/own reservation — allow re-reserving it (idempotent).

    if pubkey:
        if not _ts_ok(ts):
            return json_response({"error": "stale_request"}, status=401)
        canonical = ("forkmesh-reserve-v1\n" + name + "\n" + ts).encode()
        if not await ed25519_verify(pubkey, signature, canonical):
            return json_response({"error": "bad_signature"}, status=401)

    rec = existing or {}
    rec.update({
        "name": name,
        "status": "reserved",
        "created_at": int(rec.get("created_at") or Date.now()),
    })
    if pubkey:
        rec["pubkey"] = pubkey
    await _save_account(env, name_bi, rec)
    return json_response(
        {"ok": True, "nodeName": name, "status": "reserved"}, status=201)


# Step 2 (the "Join" step): create a Solana payment request for this signup.
# Signup payments land in unique per-account deposit wallets; the worker sweeps
# confirmed deposits to the treasury and currently-online node payout addresses.
# The base58/base64url codecs, JSON-RPC client, transfer-message assembly and
# Ed25519 signing all live in solana.py (imported at the top of this module).


def _amount_sol(lamports):
    return "%.9f" % (int(lamports) / LAMPORTS_PER_SOL)


def _solana_pay_uri(address, amount_lamports, reference="", message="Join ForkMesh"):
    uri = ("solana:" + address +
           "?amount=" + _amount_sol(amount_lamports))
    if reference:
        uri += "&reference=" + reference
    uri += ("&label=" + quote("ForkMesh") +
            "&message=" + quote(message))
    return uri


# --- Low-level balance check ------------------------------------------------
# Returns the address balance in lamports, or None if the RPC call failed (so
# callers can distinguish "zero balance" from "couldn't check"). This is the
# lowest-level on-chain read available: a single getBalance, no transaction
# history scan or third-party indexer.
async def _solana_balance_lamports(env, address):
    if not address or not SOLANA_RE.match(address):
        return None
    resp = await _solana_rpc(env, "getBalance", [address])
    if not isinstance(resp, dict):
        return None
    result = resp.get("result")
    if not isinstance(result, dict):
        return None
    try:
        return int(result.get("value"))
    except (TypeError, ValueError):
        return None


async def _solana_send_transfers(env, from_addr, seed_b64url, transfers):
    transfers = [(to, int(lamports)) for to, lamports in transfers
                 if SOLANA_RE.match(to or "") and int(lamports) > 0]
    if not transfers:
        return ""
    blockhash = await _solana_latest_blockhash(env)
    if not blockhash:
        return ""
    message = _solana_transfer_message(from_addr, transfers, blockhash)
    sig = await _solana_sign_message(from_addr, seed_b64url, message)
    if len(sig) != 64:
        return ""
    tx = _shortvec(1) + sig + message
    return await _solana_send_transaction(env, tx)


async def _solana_sign_transfers(env, from_addr, seed_b64url, transfers):
    # Build and sign a transfer transaction WITHOUT broadcasting it, returning
    # (signature_base58, tx_base64). The signature is fully determined by the
    # (from, transfers, blockhash) tuple and computed locally, so it can be
    # persisted BEFORE the transaction ever hits the network. That is the linchpin
    # of an idempotent payout: a retry rebroadcasts these exact bytes (identical
    # signature), which the cluster dedupes, rather than minting a second transfer.
    transfers = [(to, int(lamports)) for to, lamports in transfers
                 if SOLANA_RE.match(to or "") and int(lamports) > 0]
    if not transfers:
        return "", ""
    blockhash = await _solana_latest_blockhash(env)
    if not blockhash:
        return "", ""
    message = _solana_transfer_message(from_addr, transfers, blockhash)
    if not message:
        return "", ""
    sig = await _solana_sign_message(from_addr, seed_b64url, message)
    if len(sig) != 64:
        return "", ""
    tx = _shortvec(1) + sig + message
    return _base58_encode(sig), base64.b64encode(tx).decode()


async def _solana_broadcast_raw(env, tx_b64):
    # Broadcast an already-signed transaction (base64, from _solana_sign_transfers).
    # Retry-safe: sending the same bytes twice yields the same signature, which the
    # network dedupes, so this can never produce a second on-chain transfer.
    if not tx_b64:
        return ""
    try:
        tx = base64.b64decode(tx_b64)
    except Exception:
        return ""
    return await _solana_send_transaction(env, tx)


async def _solana_signature_landed(env, signature):
    # True iff the cluster knows this signature AND it did not fail. Lets a payout
    # that crashed mid-broadcast tell whether the recorded transfer already settled
    # (must NOT re-pay) or never landed (safe to rebroadcast the same bytes).
    if not signature:
        return False
    resp = await _solana_rpc(
        env, "getSignatureStatuses",
        [[signature], {"searchTransactionHistory": True}])
    if not isinstance(resp, dict):
        return False
    try:
        value = resp["result"]["value"][0]
    except Exception:
        return False
    if not isinstance(value, dict):
        return False
    if value.get("err") is not None:
        return False
    return (value.get("confirmationStatus") in ("confirmed", "finalized") or
            value.get("slot") is not None)


# --- Per-node deposit keypair -----------------------------------------------
# A Solana address is an Ed25519 public key. We generate a fresh keypair per
# signup with the runtime's WebCrypto so each node gets a unique deposit
# address; the 32-byte seed (base64url, from the JWK "d" field) is stored
# encrypted so the funds can later be swept to the treasury.
async def _new_solana_keypair():
    pair = await js_crypto.subtle.generateKey(
        to_js({"name": "Ed25519"}), True, _to_js(["sign", "verify"]))
    pub_raw = await js_crypto.subtle.exportKey("raw", pair.publicKey)
    pub = bytes(Uint8Array.new(pub_raw).to_py())
    jwk = await js_crypto.subtle.exportKey("jwk", pair.privateKey)
    seed_b64url = str(getattr(jwk, "d", "") or "")
    if len(pub) != 32 or not seed_b64url:
        return None, None
    return _base58_encode(pub), seed_b64url


# --- SOL/USD price + dynamic minimum ----------------------------------------
async def _sol_usd_price(env):
    now = int(Date.now())
    if (_SOL_USD_CACHE["usd"] > 0 and
            now - _SOL_USD_CACHE["ts"] < _SOL_USD_CACHE_TTL_MS):
        return _SOL_USD_CACHE["usd"]
    # Prefer the on-chain Pyth oracle (no third party beyond the RPC we already
    # use); fall back to an HTTP price API only if that fails.
    price = await _sol_usd_from_pyth(env)
    if not (SOL_USD_MIN <= price <= SOL_USD_MAX):
        price = await _sol_usd_from_http(env)
    if not (SOL_USD_MIN <= price <= SOL_USD_MAX):
        return 0.0
    _SOL_USD_CACHE["usd"] = price
    _SOL_USD_CACHE["ts"] = now
    return price


async def _min_join_lamports(env):
    # Target ~$1, rounded UP. Falls back to a fixed floor if no price is known,
    # and is always clamped to [FLOOR, CEILING].
    price = await _sol_usd_price(env)
    if price <= 0:
        return MIN_JOIN_LAMPORTS
    exact = (MIN_JOIN_USD / price) * LAMPORTS_PER_SOL  # lamports for ~$1
    lamports = int(exact)
    if exact > lamports:  # ceil
        lamports += 1
    # Round up to the nearest 0.0001 SOL so the displayed amount stays tidy.
    step = 100_000
    lamports = ((lamports + step - 1) // step) * step
    return max(MIN_JOIN_LAMPORTS_FLOOR, min(MIN_JOIN_LAMPORTS_CEILING, lamports))


async def _account_donation_address(env, request):
    # Federated relay: route signup custody through the main relay rather than
    # minting/holding a deposit key locally.
    if not _is_main_relay(env):
        return await _federated_donation_address(env, request)
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    try:
        amount = int(data.get("amountLamports", 0) or 0)
    except (TypeError, ValueError):
        amount = 0
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "reserve_node_name_first"}, status=404)
    if rec.get("status") == "active":
        return json_response({"error": "already_active"}, status=409)
    # The treasury is where per-node deposits are later swept; require it so we
    # never collect funds we have nowhere to forward.
    treasury = _treasury_address(env)
    if not treasury:
        return json_response({"error": "treasury_not_configured"}, status=503)

    now = int(Date.now())
    renew_expired = bool(data.get("renewExpired") or data.get("generateNew"))
    # Migration guard: records created before per-node deposits used the shared
    # treasury address (no donation_secret). Never balance-check that — the
    # treasury's own balance would falsely confirm everyone. Recycle it so the
    # user gets a fresh dedicated address below.
    if (rec.get("donation_address") and not rec.get("donation_confirmed") and
            not rec.get("donation_secret")):
        _clear_donation_address(rec)
        await _save_account(env, name_bi, rec)

    if rec.get("donation_address") and not rec.get("donation_confirmed"):
        changed = _ensure_donation_expiry_fields(rec, now)
        required = int(rec.get("donation_required_lamports", MIN_JOIN_LAMPORTS))
        _, expires_at, delete_at = _donation_expiry_fields(rec, now)
        # Low-level balance read of this node's own deposit address. None means
        # the RPC was unreachable — we keep the address rather than blocking the
        # whole signup, and just let the user keep paying / polling.
        balance = await _solana_balance_lamports(env, rec.get("donation_address", ""))
        if balance is not None and balance >= required:
            rec["donation_confirmed"] = True
            rec["donation_received_lamports"] = balance
            await _sweep_confirmed_donation(env, name_bi, rec, balance)
            changed = True
        elif now >= expires_at and balance == 0:
            # Expired with nothing received: hide it, and recycle the address
            # once the user asks to renew or the delete grace passes.
            if renew_expired or now >= delete_at:
                _clear_donation_address(rec)
                changed = True
            else:
                if changed:
                    await _save_account(env, name_bi, rec)
                return json_response({
                    "ok": True, "expired": True, "hidden": True,
                    "canRenew": True, "receivedLamports": 0,
                    "requiredLamports": required,
                    "amountSol": _amount_sol(required),
                    "expiresAt": expires_at, "deleteAt": delete_at,
                })
        if changed:
            await _save_account(env, name_bi, rec)

    if not rec.get("donation_address"):
        addr, secret = await _new_solana_keypair()
        if not addr:
            return json_response({"error": "keypair_failed"}, status=500)
        rec["donation_address"] = addr
        rec["donation_secret"] = secret  # encrypted at rest via _save_account
        rec["donation_reference"] = ""    # a unique address needs no reference
        rec["donation_required_lamports"] = max(amount, await _min_join_lamports(env))
        rec["donation_confirmed"] = False
        rec["donation_received_lamports"] = 0
        rec["donation_created_at"] = now
        rec["donation_expires_at"] = now + DONATION_ADDRESS_TTL_MS
        rec["donation_delete_after"] = (
            rec["donation_expires_at"] + DONATION_ADDRESS_DELETE_GRACE_MS)
        rec["status"] = "pending_payment"
        await _save_account(env, name_bi, rec)

    addr = rec["donation_address"]
    required = int(rec.get("donation_required_lamports", MIN_JOIN_LAMPORTS))
    _, expires_at, delete_at = _donation_expiry_fields(rec, int(Date.now()))
    amount_sol = _amount_sol(required)
    price = await _sol_usd_price(env)
    return json_response({
        "ok": True, "address": addr,
        "reference": "",
        "uri": _solana_pay_uri(addr, required),
        "requiredLamports": required, "amountSol": amount_sol,
        "solUsd": price or 0,
        "amountUsd": round((required / LAMPORTS_PER_SOL) * price, 2) if price else 0,
        "expiresAt": expires_at, "deleteAt": delete_at,
    })


# Polled while the user waits to pay. When the per-account deposit address has
# the required balance, the account is marked confirmed and swept.
async def _account_donation_status(env, request):
    if not _is_main_relay(env):
        return await _federated_donation_status(env, request)
    params = parse_qs(urlparse(request.url).query)
    name = clean_string(params.get("nodeName", [""])[0], MAX_NODE_NAME).lower()
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "no_such_account"}, status=404)
    addr = rec.get("donation_address", "")
    if not addr:
        return json_response({"error": "no_donation_address"}, status=400)
    required = int(rec.get("donation_required_lamports", MIN_JOIN_LAMPORTS))
    now = int(Date.now())
    changed = _ensure_donation_expiry_fields(rec, now)
    _, expires_at, delete_at = _donation_expiry_fields(rec, now)
    already_confirmed = bool(rec.get("donation_confirmed"))

    # Migration guard: a legacy shared-treasury address (no donation_secret) must
    # not be balance-checked — ask the client to generate a fresh per-node one.
    if not already_confirmed and not rec.get("donation_secret"):
        return json_response({
            "ok": True, "paid": False, "hidden": True, "canRenew": True,
            "receivedLamports": 0, "requiredLamports": required,
            "expiresAt": expires_at, "deleteAt": delete_at,
        })

    # Low-level balance check on this node's own deposit address.
    balance = await _solana_balance_lamports(env, addr)
    if balance is None:
        # RPC unreachable: report a soft "still checking" state instead of a hard
        # error so the user can keep waiting (and paying) rather than being told
        # signup is broken. Fall back to the last balance we recorded.
        received = int(rec.get("donation_received_lamports", 0) or 0)
        return json_response({
            "ok": True, "paid": already_confirmed, "checking": True,
            "receivedLamports": received, "requiredLamports": required,
            "expiresAt": expires_at, "deleteAt": delete_at,
        })

    received = int(balance)
    if not already_confirmed and received == 0 and now >= delete_at:
        _clear_donation_address(rec)
        await _save_account(env, name_bi, rec)
        return json_response({
            "ok": True, "paid": False, "expired": True, "hidden": True,
            "deleted": True, "canRenew": True, "receivedLamports": 0,
            "requiredLamports": required, "expiresAt": expires_at,
            "deleteAt": delete_at,
        })
    if received != rec.get("donation_received_lamports"):
        rec["donation_received_lamports"] = received
        changed = True
    if received >= required and not already_confirmed:
        rec["donation_confirmed"] = True
        await _sweep_confirmed_donation(env, name_bi, rec, received)
        changed = True
    elif rec.get("donation_confirmed") and not rec.get("donation_sweep_sig"):
        if await _sweep_confirmed_donation(env, name_bi, rec, received):
            changed = True

    if changed:
        await _save_account(env, name_bi, rec)
    # Only "expire" an address that received nothing — once any SOL lands we keep
    # waiting for the rest rather than recycling under the user's payment.
    expired = (not rec.get("donation_confirmed") and received == 0 and
               now >= expires_at)
    return json_response({
        "ok": True, "paid": bool(rec.get("donation_confirmed")),
        "receivedLamports": received, "requiredLamports": required,
        "expired": expired, "hidden": expired, "canRenew": expired,
        "expiresAt": expires_at, "deleteAt": delete_at,
    })


def _signup_metadata(request):
    # Uniqueness/anti-abuse signals captured at signup. Cloudflare sets these on
    # every inbound request. They are PRIVACY-SENSITIVE, so the values only ever
    # live inside the AES-GCM-encrypted `data` blob (decryptable solely with
    # DATA_KEY); the IP additionally gets a one-way blind index (see ip_bi) so
    # duplicate signups can be counted without storing a reversible address.
    try:
        headers = request.headers
    except Exception:
        return {"ip": "", "ua": "", "country": "", "at": int(Date.now())}
    ip = (headers.get("cf-connecting-ip") or "").strip()
    if not ip:
        # Fall back to the first hop of X-Forwarded-For only if CF's header is
        # somehow absent (e.g. a non-CF test/proxy path).
        fwd = (headers.get("x-forwarded-for") or "").strip()
        ip = fwd.split(",")[0].strip() if fwd else ""
    return {
        "ip": ip[:64],
        "ua": (headers.get("user-agent") or "").strip()[:256],
        "country": (headers.get("cf-ipcountry") or "").strip()[:8],
        "at": int(Date.now()),
    }


# Step 3: after a confirmed donation, set the email + password that unlock
# universal (any-surface) login. Email + password hash are stored encrypted.
async def _account_finalize(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    email = clean_string(data.get("email", ""), 254)
    password = (data.get("password", "") or "")[:256]
    pubkey = clean_string(data.get("pubkey", ""), 120)
    solana = clean_string(data.get("solana", ""), 64)
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)

    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "no_such_account"}, status=404)

    # A key-bound (desktop) account proves ownership with its Ed25519 signature;
    # email/password are optional (it logs in by key). A keyless (web) signup just
    # sets an email + password — signup is free (no donation required), and the
    # verification email sent below (Mailtrap) is what confirms a real human is
    # behind the name.
    key_bound = bool(rec.get("pubkey"))
    if key_bound:
        if not _ts_ok(ts):
            return json_response({"error": "stale_request"}, status=401)
        canonical = ("forkmesh-finalize-v1\n" + name + "\n" + email + "\n" +
                     ts).encode()
        if not await ed25519_verify(rec["pubkey"], signature, canonical):
            return json_response({"error": "bad_signature"}, status=401)
    elif pubkey:
        rec["pubkey"] = pubkey  # bind a key now if a web user supplied one

    # Email + password unlock cross-device (password) login. They're required for
    # keyless signups and optional for key-bound ones (which log in by key); when
    # either is supplied they're validated and the email is indexed for login.
    set_credentials = bool(email) or bool(password) or not key_bound
    email_bi = None
    if set_credentials:
        if "@" not in email or len(email) < 3:
            return json_response({"error": "valid_email_required"}, status=400)
        if len(password) < 8:
            return json_response({"error": "password_too_short"}, status=400)
        email_bi = await blind_index(env, email)
        dup = await d1_first(
            env, "SELECT name_bi FROM accounts WHERE email_bi=?", email_bi)
        if dup and dup.get("name_bi") != name_bi:
            return json_response({"error": "email_taken"}, status=409)
        salt, phash = await hash_password(password)
        rec["email"] = email
        rec["pass_salt"] = salt
        rec["pass_hash"] = phash
    rec["status"] = "active"
    # Optional payout address (where this node receives its share of the split).
    if solana and SOLANA_RE.match(solana):
        rec["solana"] = solana
    rec["email_verified"] = bool(rec.get("email_verified", False))
    rec.setdefault("created_at", int(Date.now()))
    # Capture the signup IP (encrypted in the record) plus a blind index of it for
    # privacy-respecting duplicate-signup detection. The first finalize wins: keep
    # the original signup fingerprint rather than overwriting it on a re-finalize,
    # and derive the blind index from the IP we actually store so the two agree.
    rec.setdefault("signup", _signup_metadata(request))
    signup_ip = (rec.get("signup") or {}).get("ip") or ""
    ip_bi = await blind_index(env, signup_ip) if signup_ip else None
    await _save_account(env, name_bi, rec, email_bi=email_bi, ip_bi=ip_bi)
    # Send a verification email (Mailtrap). If the email service is unconfigured
    # or the send fails, fall back to the admin queue so an admin can still verify
    # by hand. A free key-bound join with no email yet has nothing to verify.
    if rec.get("email") and not rec["email_verified"]:
        sent = await _send_verification_email(env, request, name, rec["email"])
        if not sent:
            await _enqueue_verification(env, name_bi, name, rec["email"])
    # Installer link code (adhoc #53): a hosts/SSH install prints a code on the
    # fresh machine and hands it to the daemon, which presents it here when it
    # registers. If the installing desktop's signed offer already arrived the
    # node is linked to that user right now; otherwise the node's half is
    # parked until the offer lands (see _redeem_or_park_link_code).
    link_code = clean_string(data.get("linkCode", ""), 16).strip()
    if key_bound and LINK_CODE_RE.match(link_code) and not rec.get("owner"):
        result = await _redeem_or_park_link_code(env, link_code, node=name)
        if result.get("linked"):
            rec["owner"] = result.get("user", "")
    return json_response(await _account_public_payload(env, rec), status=201)


async def _account_profile(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    identifier = clean_string(
        data.get("nodeName", "") or data.get("identifier", "") or
        data.get("email", ""), 254).strip().lower()
    password = (data.get("password", "") or "")[:256]
    if not identifier or not password:
        return json_response({"error": "invalid_credentials"}, status=401)

    name_bi = ""
    rec = None
    if "@" in identifier:
        email_bi = await blind_index(env, identifier)
        row = await d1_first(env, "SELECT name_bi, data FROM accounts WHERE email_bi=?", email_bi)
        if row:
            name_bi = row.get("name_bi", "")
            rec = await decrypt_row(env, row.get("data"))
    elif valid_node_name(identifier):
        name_bi, rec = await _account_row(env, identifier)
    if not rec or rec.get("status") != "active" or not rec.get("pass_hash"):
        return json_response({"error": "invalid_credentials"}, status=401)
    if not await verify_password(password, rec.get("pass_salt", ""), rec.get("pass_hash", "")):
        return json_response({"error": "invalid_credentials"}, status=401)
    if not name_bi:
        name_bi = await blind_index(env, rec.get("name", ""))

    if data.get("deleteAccount") or data.get("disableAccount"):
        await _delete_account_namespace(env, name_bi, rec)
        return json_response({"ok": True, "accountDeleted": True})

    changed = False
    if "solana" in data:
        solana = clean_string(data.get("solana", ""), 64).strip()
        if solana and not SOLANA_RE.match(solana):
            return json_response({"error": "bad_solana"}, status=400)
        if solana:
            if rec.get("solana") != solana:
                rec["solana"] = solana
                changed = True
        elif rec.get("solana"):
            rec.pop("solana", None)
            changed = True

    if "avatarPng" in data:
        avatar_png, avatar_error = clean_avatar_png(data.get("avatarPng", ""))
        if avatar_error:
            status = 413 if avatar_error == "avatar_too_large" else 400
            return json_response({"error": avatar_error}, status=status)
        if avatar_png:
            if rec.get("avatar_png") != avatar_png:
                rec["avatar_png"] = avatar_png
                rec["avatar_updated_at"] = int(Date.now())
                changed = True
        elif rec.get("avatar_png"):
            rec.pop("avatar_png", None)
            rec["avatar_updated_at"] = int(Date.now())
            changed = True

    verification_sent = False
    verification_queued = False
    if data.get("resendVerification") and rec.get("email") and not rec.get("email_verified"):
        verification_sent = await _send_verification_email(env, request, rec.get("name", ""), rec.get("email", ""))
        if not verification_sent:
            await _enqueue_verification(env, name_bi, rec.get("name", ""), rec.get("email", ""))
            verification_queued = True

    new_name = clean_string(data.get("newNodeName", ""), MAX_NODE_NAME).lower()
    renamed = False
    if new_name:
        if not rec.get("email_verified"):
            return json_response({"error": "email_not_verified"}, status=403)
        name_bi, rec, rename_error = await _rename_account_namespace(env, name_bi, rec, new_name)
        if rename_error:
            status = 400
            if rename_error in ("node_name_taken", "repo_namespace_conflict"):
                status = 409
            elif rename_error == "invalid_credentials":
                status = 401
            return json_response({"error": rename_error}, status=status)
        changed = False
        renamed = True

    if changed:
        await _save_account(env, name_bi, rec)
    payload = await _account_public_payload(env, rec)
    payload["verificationSent"] = bool(verification_sent)
    payload["verificationQueued"] = bool(verification_queued)
    payload["nodeNameChanged"] = bool(renamed)
    return json_response(payload)


# --- Users vs nodes: claiming & linking (adhoc #53) --------------------------
#
# Two ways a user (an account with login credentials) becomes the owner of a
# node (a key-bound-only account, e.g. a headless mirror registration):
#
#  1. Website claim: the user enters the node's ID (its account name) on the
#     dashboard. The worker parks a short confirmation code on the node's
#     record; the node learns of it in its next signed heartbeat reply and
#     shows the code on the machine itself. Typing that code back into the
#     website proves the user can see the node, and the records are linked.
#
#  2. Installer link code: a hosts/SSH install prints a code on the fresh
#     machine and hands it to the launched daemon, which presents it when it
#     registers. The desktop app that drove the install offers the same code
#     signed by its own key; whichever side reaches the worker first parks its
#     half in link_codes and the second side completes the link. The new node
#     is attached to the USER behind the installing account (the account
#     itself when it is a user, else that node's own recorded owner).


def _generate_confirm_code():
    return "%06d" % (int.from_bytes(_random_bytes(4), "big") % 1000000)


def _claim_pending(rec, now):
    pending = rec.get("claim_pending")
    if not isinstance(pending, dict):
        return None
    try:
        expires = int(pending.get("expires") or 0)
    except (TypeError, ValueError):
        return None
    if now >= expires or not pending.get("user") or not pending.get("code"):
        return None
    return pending


def _transfer_pending(rec, now):
    pending = rec.get("ownership_transfer_pending")
    if not isinstance(pending, dict):
        return None
    try:
        expires = int(pending.get("expires") or 0)
    except (TypeError, ValueError):
        return None
    if now >= expires or not pending.get("admin"):
        return None
    return pending


async def _resolve_user_by_password(env, data):
    # Web-auth gate for the claim endpoints, matching _account_profile: a
    # state-changing dashboard action re-proves the password on each request
    # (the static site keeps no server-side session). Returns (name_bi, rec)
    # for an active credentialed account, else (None, None).
    identifier = clean_string(
        data.get("identifier", "") or data.get("email", "") or
        data.get("user", ""), 254).strip().lower()
    password = (data.get("password", "") or "")[:256]
    if not identifier or not password:
        return None, None
    name_bi, rec = "", None
    if "@" in identifier:
        email_bi = await blind_index(env, identifier)
        row = await d1_first(
            env, "SELECT name_bi, data FROM accounts WHERE email_bi=?", email_bi)
        if row:
            name_bi = row.get("name_bi", "")
            rec = await decrypt_row(env, row.get("data"))
    elif valid_node_name(identifier):
        name_bi, rec = await _account_row(env, identifier)
    if not rec or rec.get("status") != "active" or not rec.get("pass_hash"):
        return None, None
    if not await verify_password(password, rec.get("pass_salt", ""),
                                 rec.get("pass_hash", "")):
        return None, None
    if not name_bi:
        name_bi = await blind_index(env, rec.get("name", ""))
    return name_bi, rec


async def _link_node_to_user(env, node_name, node_bi, node_rec, user_name):
    # The one place ownership is written: node.owner = user and the node joins
    # the user's nodes list. The user's record is loaded fresh so a stale
    # caller copy can't clobber it. When the node was already owned (the
    # browser link-grant flow may re-home a node), it leaves the previous
    # owner's nodes list so the fleet views stay truthful.
    prev_owner = node_rec.get("owner") or ""
    node_rec["owner"] = user_name
    node_rec.pop("claim_pending", None)
    node_rec.pop("ownership_transfer_pending", None)
    await _save_account(env, node_bi, node_rec)
    if prev_owner and prev_owner != user_name:
        prev_bi, prev_rec = await _account_row(env, prev_owner)
        if prev_rec is not None:
            prev_rec["nodes"] = [n for n in _owned_nodes(prev_rec)
                                 if n != node_name]
            await _save_account(env, prev_bi, prev_rec)
    user_bi, user_rec = await _account_row(env, user_name)
    if user_rec is not None:
        nodes = _owned_nodes(user_rec)
        if node_name not in nodes:
            nodes.append(node_name)
        user_rec["nodes"] = nodes
        await _save_account(env, user_bi, user_rec)


async def _account_claim_node(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    _, user_rec = await _resolve_user_by_password(env, data)
    if not user_rec:
        return json_response({"error": "invalid_credentials"}, status=401)
    user_name = user_rec.get("name", "")
    raw_id = clean_string(data.get("nodeId", "") or data.get("nodeName", ""),
                          MAX_NODE_NAME)
    if not (valid_node_name(raw_id.strip().lower()) or valid_node_pubkey(raw_id)):
        return json_response({"error": "invalid_node_id"}, status=400)
    node_bi, node_rec, node_id = await _resolve_claimable_node(env, raw_id)
    if node_id == user_name:
        return json_response({"error": "cannot_claim_self"}, status=400)
    if not node_rec or node_rec.get("status") != "active":
        return json_response({"error": "no_such_node"}, status=404)
    if _account_kind(node_rec) != "node":
        # An account that can log in is a user in its own right, not claimable.
        return json_response({"error": "not_a_node"}, status=403)
    if node_rec.get("owner") == user_name:
        return json_response({"ok": True, "alreadyLinked": True,
                              "nodeId": node_id})
    if node_rec.get("owner"):
        return json_response({"error": "node_already_owned"}, status=409)
    now = int(Date.now())
    node_rec["claim_pending"] = {
        "user": user_name,
        "code": _generate_confirm_code(),
        "expires": now + CLAIM_CODE_TTL_MS,
        "attempts": 0,
    }
    await _save_account(env, node_bi, node_rec)
    # The code itself is deliberately NOT returned: it only ever travels
    # worker -> node (heartbeat reply) -> the person standing at both screens.
    return json_response({"ok": True, "pending": True, "nodeId": node_id,
                          "expiresAt": now + CLAIM_CODE_TTL_MS}, status=201)


async def _account_claim_confirm(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    _, user_rec = await _resolve_user_by_password(env, data)
    if not user_rec:
        return json_response({"error": "invalid_credentials"}, status=401)
    user_name = user_rec.get("name", "")
    raw_id = clean_string(data.get("nodeId", "") or data.get("nodeName", ""),
                          MAX_NODE_NAME)
    code = clean_string(data.get("code", ""), 16).strip()
    if not (valid_node_name(raw_id.strip().lower()) or valid_node_pubkey(raw_id)):
        return json_response({"error": "invalid_node_id"}, status=400)
    node_bi, node_rec, node_id = await _resolve_claimable_node(env, raw_id)
    if not node_rec or node_rec.get("status") != "active":
        return json_response({"error": "no_such_node"}, status=404)
    if node_rec.get("owner") == user_name:
        return json_response({"ok": True, "linked": True, "nodeId": node_id,
                              "nodes": _owned_nodes(user_rec)})
    pending = _claim_pending(node_rec, int(Date.now()))
    if not pending or pending.get("user") != user_name:
        return json_response({"error": "no_pending_claim"}, status=404)
    if not code or code != pending.get("code"):
        # A 6-digit code must not be guessable within its TTL: a few misses
        # invalidate the claim entirely (restart it from the dashboard).
        attempts = int(pending.get("attempts") or 0) + 1
        if attempts >= CLAIM_CODE_MAX_ATTEMPTS:
            node_rec.pop("claim_pending", None)
        else:
            pending["attempts"] = attempts
            node_rec["claim_pending"] = pending
        await _save_account(env, node_bi, node_rec)
        return json_response({"error": "bad_code"}, status=401)
    await _link_node_to_user(env, node_id, node_bi, node_rec, user_name)
    _, fresh_user = await _account_row(env, user_name)
    return json_response({"ok": True, "linked": True, "nodeId": node_id,
                          "nodes": _owned_nodes(fresh_user or user_rec)})


async def _redeem_or_park_link_code(env, code, node=None, user=None):
    # Order-independent rendezvous for installer link codes: called with node=
    # from the fresh node's registration and with user= from the installing
    # desktop's signed offer. When the opposite half is already parked (and
    # fresh), complete the link and burn the code; otherwise park this half.
    # Returns {"linked": True, "node": ..., "user": ...} when the link
    # completed now, else {"linked": False}.
    code_bi = await blind_index(env, "link:" + code)
    now = int(Date.now())
    row = await d1_first(
        env, "SELECT data, ts FROM link_codes WHERE code_bi=?", code_bi)
    other = None
    if row:
        try:
            fresh = now - int(row.get("ts") or 0) <= LINK_CODE_TTL_MS
        except (TypeError, ValueError):
            fresh = False
        other = await decrypt_row(env, row.get("data")) if fresh else None
    if node and other and other.get("user"):
        node_bi, node_rec = await _account_row(env, node)
        if node_rec is not None and not node_rec.get("owner"):
            await _link_node_to_user(env, node, node_bi, node_rec,
                                     other["user"])
        await d1_run(env, "DELETE FROM link_codes WHERE code_bi=?", code_bi)
        return {"linked": True, "node": node, "user": other["user"]}
    if user and other and other.get("node"):
        node_name = other["node"]
        node_bi, node_rec = await _account_row(env, node_name)
        if node_rec is not None and not node_rec.get("owner"):
            await _link_node_to_user(env, node_name, node_bi, node_rec, user)
        await d1_run(env, "DELETE FROM link_codes WHERE code_bi=?", code_bi)
        return {"linked": True, "node": node_name, "user": user}
    encrypted = await encrypt_row(env, {"node": node} if node else {"user": user})
    await d1_run(
        env,
        "INSERT INTO link_codes (code_bi, data, ts) VALUES (?,?,?) "
        "ON CONFLICT(code_bi) DO UPDATE SET data=excluded.data, ts=excluded.ts",
        code_bi, encrypted, now)
    return {"linked": False}


async def _account_link_node(env, request):
    # The installing desktop's half of the installer link-code flow: the code
    # printed by install.sh on the fresh machine, offered here signed by the
    # installing account's own key.
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    code = clean_string(data.get("code", ""), 16).strip()
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    if not valid_node_name(name) or not LINK_CODE_RE.match(code):
        return json_response({"error": "invalid_request"}, status=400)
    _, rec = await _account_row(env, name)
    if not rec or rec.get("status") != "active":
        return json_response({"error": "no_such_account"}, status=404)
    pubkey = rec.get("pubkey", "")
    if not pubkey or not _ts_ok(ts):
        return json_response({"error": "unauthorized"}, status=401)
    canonical = ("forkmesh-link-v1\n" + name + "\n" + code + "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)
    user = name if _account_kind(rec) == "user" else (rec.get("owner") or "")
    if not user:
        return json_response({"error": "no_user_account"}, status=403)
    result = await _redeem_or_park_link_code(env, code, user=user)
    if result.get("linked"):
        return json_response({"ok": True, "linked": True,
                              "node": result.get("node", ""), "user": user})
    return json_response({"ok": True, "linked": False, "pending": True,
                          "user": user}, status=202)


async def _account_link_self(env, request):
    # A node links ITSELF to a user account, driven from that node's own desktop
    # app ("Log in as a user" in the node profile). The request proves BOTH
    # secrets at once: control of the node's key (Ed25519 signature) and the
    # user's credentials (identifier + password), so the link completes
    # immediately with no confirmation code — holding both IS the authorization.
    # This is the in-app counterpart to the website's claim-node/claim-confirm
    # code flow, which only proves the password and needs the on-node code to
    # prove the claimer can see the machine.
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    node_name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    if not valid_node_name(node_name):
        return json_response({"error": "invalid_node_id"}, status=400)
    node_bi, node_rec = await _account_row(env, node_name)
    if not node_rec or node_rec.get("status") != "active":
        return json_response({"error": "no_such_node"}, status=404)
    pubkey = node_rec.get("pubkey", "")
    if not pubkey or not _ts_ok(ts):
        return json_response({"error": "unauthorized"}, status=401)
    identifier = clean_string(
        data.get("identifier", "") or data.get("email", "") or
        data.get("user", ""), 254).strip().lower()
    canonical = ("forkmesh-link-self-v1\n" + node_name + "\n" + identifier +
                 "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)
    # Only now spend a password verification (the node signature gates it).
    _, user_rec = await _resolve_user_by_password(env, data)
    if not user_rec:
        return json_response({"error": "invalid_credentials"}, status=401)
    user_name = user_rec.get("name", "")
    if node_name == user_name:
        return json_response({"error": "cannot_link_self"}, status=400)
    if _account_kind(node_rec) != "node":
        # An account that can log in is a user in its own right, not linkable.
        return json_response({"error": "not_a_node"}, status=403)
    if node_rec.get("owner") == user_name:
        return json_response({"ok": True, "linked": True, "alreadyLinked": True,
                              "nodeId": node_name, "user": user_name,
                              "nodes": _owned_nodes(user_rec)})
    if node_rec.get("owner"):
        return json_response({"error": "node_already_owned"}, status=409)
    await _link_node_to_user(env, node_name, node_bi, node_rec, user_name)
    _, fresh_user = await _account_row(env, user_name)
    return json_response({"ok": True, "linked": True, "nodeId": node_name,
                          "user": user_name,
                          "nodes": _owned_nodes(fresh_user or user_rec)})


async def _account_link_grant(env, request):
    # The node profile's "Link this node to your account" button (adhoc #120):
    # the desktop app signs a short-lived grant with the node's own key and
    # opens it as a /dashboard URL in the browser, where the already-logged-in
    # user redeems it here. The signature proves node-key control and names an
    # explicit consent ("attach me to whoever redeems this"), so unlike
    # claim-node no password re-entry or confirmation code is needed — the
    # browser session just names the user. Freshness (LOGIN_MAX_SKEW_MS) plus
    # single use keep a leaked URL from being replayable.
    #
    # Unlike claim-node/link-self this OVERRIDES the node's current
    # association: an already-owned node is re-homed to the redeeming user
    # (leaving the old owner's fleet), and even a node whose account is itself
    # a user can be taken possession of — the node's key signed the grant, so
    # the machine's operator has authorized the hand-over.
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    node_name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    user_name = clean_string(data.get("user", ""), MAX_NODE_NAME).strip().lower()
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    if not valid_node_name(node_name) or not valid_node_name(user_name):
        return json_response({"error": "invalid_request"}, status=400)
    node_bi, node_rec = await _account_row(env, node_name)
    if not node_rec or node_rec.get("status") != "active":
        return json_response({"error": "no_such_node"}, status=404)
    pubkey = node_rec.get("pubkey", "")
    if not pubkey or not _ts_ok(ts):
        return json_response({"error": "unauthorized"}, status=401)
    canonical = ("forkmesh-link-grant-v1\n" + node_name + "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)
    if node_name == user_name:
        # The browser is logged in as this very node's account: nothing to
        # attach, it already belongs to itself.
        return json_response({"ok": True, "linked": True, "alreadyLinked": True,
                              "selfAccount": True, "nodeId": node_name,
                              "user": user_name,
                              "nodes": _owned_nodes(node_rec)})
    _, user_rec = await _account_row(env, user_name)
    if not user_rec or user_rec.get("status") != "active":
        return json_response({"error": "no_such_user"}, status=404)
    if _account_kind(user_rec) != "user":
        # Only an account that can log in can own nodes; a bare node session in
        # the browser can't take possession of another node.
        return json_response({"error": "not_a_user"}, status=403)
    if node_rec.get("owner") == user_name:
        return json_response({"ok": True, "linked": True, "alreadyLinked": True,
                              "nodeId": node_name, "user": user_name,
                              "nodes": _owned_nodes(user_rec)})
    # Burn the grant before linking so it is strictly one-time even if the node
    # is unlinked again inside the signature's freshness window. Parked in
    # link_codes keyed on the full grant; rows age out like installer codes.
    grant_bi = await blind_index(env, "grant:" + node_name + ":" + ts + ":" +
                                 signature)
    now = int(Date.now())
    row = await d1_first(
        env, "SELECT data, ts FROM link_codes WHERE code_bi=?", grant_bi)
    if row:
        return json_response({"error": "grant_used"}, status=409)
    encrypted = await encrypt_row(env, {"grant": node_name})
    await d1_run(
        env,
        "INSERT INTO link_codes (code_bi, data, ts) VALUES (?,?,?) "
        "ON CONFLICT(code_bi) DO UPDATE SET data=excluded.data, ts=excluded.ts",
        grant_bi, encrypted, now)
    await _link_node_to_user(env, node_name, node_bi, node_rec, user_name)
    _, fresh_user = await _account_row(env, user_name)
    return json_response({"ok": True, "linked": True, "nodeId": node_name,
                          "user": user_name,
                          "nodes": _owned_nodes(fresh_user or user_rec)})


async def _login_locked_until(env, id_bi):
    # Returns the lock-expiry ms if the identifier is currently locked, else 0.
    row = await d1_first(
        env, "SELECT locked_until FROM login_attempts WHERE id_bi=?", id_bi)
    if not row:
        return 0
    try:
        locked = int(row.get("locked_until") or 0)
    except (TypeError, ValueError):
        return 0
    return locked if locked > int(Date.now()) else 0


async def _login_record_fail(env, id_bi):
    # Increment the failure counter (resetting it once the rolling window passes)
    # and lock the identifier once it crosses the threshold.
    now = int(Date.now())
    row = await d1_first(
        env, "SELECT fails, first_fail_ts FROM login_attempts WHERE id_bi=?", id_bi)
    fails = 0
    first = now
    if row:
        try:
            fails = int(row.get("fails") or 0)
            first = int(row.get("first_fail_ts") or now)
        except (TypeError, ValueError):
            fails, first = 0, now
    if now - first > LOGIN_FAIL_WINDOW_MS:
        fails, first = 0, now
    fails += 1
    locked_until = now + LOGIN_LOCKOUT_MS if fails >= LOGIN_MAX_FAILS else 0
    await d1_run(
        env,
        "INSERT INTO login_attempts (id_bi, fails, first_fail_ts, locked_until) "
        "VALUES (?,?,?,?) ON CONFLICT(id_bi) DO UPDATE SET "
        "fails=excluded.fails, first_fail_ts=excluded.first_fail_ts, "
        "locked_until=excluded.locked_until",
        id_bi, fails, first, locked_until,
    )


async def _login_clear(env, id_bi):
    await d1_run(env, "DELETE FROM login_attempts WHERE id_bi=?", id_bi)


DESKTOP_NODE_CAPABILITIES = "browse,comment,submit_issue,submit_pr,host_repo,mirror_repo,publish_repo,owner_sign"
CLIENT_CAPABILITIES = "browse,comment,submit_issue,submit_pr"


async def _register_account_device(env, account_bi, pubkey, kind="desktop_node", label=""):
    if not account_bi or not pubkey:
        return None
    device_bi = await blind_index(env, account_bi + ":" + pubkey)
    now = int(Date.now())
    capabilities = DESKTOP_NODE_CAPABILITIES if kind == "desktop_node" else CLIENT_CAPABILITIES
    await d1_run(
        env,
        """INSERT INTO account_devices
             (device_bi, account_bi, pubkey, kind, label, capabilities, enabled,
              created_at, last_seen, revoked_at)
             VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?, 0)
             ON CONFLICT(device_bi) DO UPDATE SET
               last_seen=excluded.last_seen,
               enabled=CASE WHEN account_devices.revoked_at=0 THEN 1 ELSE account_devices.enabled END""",
        device_bi, account_bi, pubkey, kind, label, capabilities, now, now,
    )
    return {"id": device_bi[:16], "kind": kind, "pubkey": pubkey,
            "capabilities": capabilities.split(",")}


async def _account_device_for_pubkey(env, account_bi, pubkey):
    if not account_bi or not pubkey:
        return None
    row = await d1_first(
        env,
        """SELECT device_bi, pubkey, kind, label, capabilities, enabled, last_seen, revoked_at
             FROM account_devices WHERE account_bi=? AND pubkey=?""",
        account_bi, pubkey,
    )
    return row


async def _account_devices_list(env, account_bi):
    rows = await d1_all(
        env,
        """SELECT device_bi, pubkey, kind, label, capabilities, enabled, last_seen, revoked_at
             FROM account_devices WHERE account_bi=? ORDER BY created_at ASC""",
        account_bi,
    )
    out = []
    for row in rows:
        caps = clean_string(row.get("capabilities", ""), 512)
        out.append({
            "id": clean_string(row.get("device_bi", ""), 128)[:16],
            "pubkey": clean_string(row.get("pubkey", ""), 120),
            "kind": clean_string(row.get("kind", ""), 40),
            "label": clean_string(row.get("label", ""), 120),
            "capabilities": [c for c in caps.split(",") if c],
            "enabled": bool(row.get("enabled")) and not bool(row.get("revoked_at")),
            "lastSeen": int(row.get("last_seen") or 0),
        })
    return out


def _with_session_capabilities(payload, *, session_kind, device_kind="", key_matched=False,
                               desktop_capable=False, capabilities=None):
    caps = capabilities or (DESKTOP_NODE_CAPABILITIES.split(",") if desktop_capable else CLIENT_CAPABILITIES.split(","))
    payload["sessionKind"] = session_kind
    payload["deviceKind"] = device_kind
    payload["deviceKeyMatched"] = bool(key_matched)
    payload["desktopCapable"] = bool(desktop_capable)
    payload["capabilities"] = caps
    payload["canHost"] = "host_repo" in caps
    payload["canPublish"] = "publish_repo" in caps
    payload["canOwnerSign"] = "owner_sign" in caps
    return payload


async def _account_login(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)

    # Log in by email or node name (the "universal access" credential).
    identifier = clean_string(
        data.get("nodeName", "") or data.get("identifier", "") or
        data.get("email", ""), 254).strip().lower()
    password = (data.get("password", "") or "")[:256]
    totp = clean_string(data.get("totp", ""), 10)
    pubkey = clean_string(data.get("pubkey", ""), 120)

    # Brute-force throttle, keyed by a blind index of the identifier (no plaintext
    # stored). Checked before any account lookup so it also protects nonexistent
    # identifiers (and so the lockout itself doesn't leak whether an account
    # exists). A generic "invalid_credentials" is returned for every pre-TOTP
    # failure so an attacker can't enumerate accounts by error code.
    id_bi = await blind_index(env, identifier) if identifier else ""
    if not id_bi:
        return json_response({"error": "invalid_credentials"}, status=401)
    if await _login_locked_until(env, id_bi):
        return json_response({"error": "too_many_attempts"}, status=429)

    rec = None
    if "@" in identifier:
        email_bi = await blind_index(env, identifier)
        row = await d1_first(
            env, "SELECT data FROM accounts WHERE email_bi=?", email_bi)
        if row:
            rec = await decrypt_row(env, row["data"])
    elif valid_node_name(identifier):
        _, rec = await _account_row(env, identifier)

    if rec is not None and rec.get("status") != "active":
        if rec.get("pass_hash") and await verify_password(
                password, rec.get("pass_salt", ""), rec.get("pass_hash", "")):
            await _login_clear(env, id_bi)
            return json_response({"error": "account_disabled"}, status=403)

    ok = (rec is not None and rec.get("status") == "active" and
          rec.get("pass_hash") and
          await verify_password(password, rec.get("pass_salt", ""),
                                rec.get("pass_hash", "")))
    if not ok:
        await _login_record_fail(env, id_bi)
        return json_response({"error": "invalid_credentials"}, status=401)
    # TOTP is only enforced for accounts that have enrolled it. A bad code counts
    # toward the lockout but the password was already correct, so the distinct
    # error here doesn't aid account enumeration.
    if rec.get("totp_enrolled"):
        if not await totp_verify(rec.get("totp_secret", ""), totp):
            await _login_record_fail(env, id_bi)
            return json_response({"error": "bad_totp"}, status=401)
    await _login_clear(env, id_bi)

    # Multi-device account model: password/TOTP authenticates the human account;
    # a supplied pubkey identifies/registers this particular desktop node. Any
    # number of desktop nodes may be enabled for the same account. The legacy
    # accounts.pubkey remains as the primary/first desktop key for compatibility,
    # but a different registered desktop key is no longer a login failure.
    name_bi = await blind_index(env, rec.get("name", ""))
    device = None
    desktop_capable = False
    device_kind = ""
    if pubkey:
        device = await _account_device_for_pubkey(env, name_bi, pubkey)
        if not device:
            device = await _register_account_device(
                env, name_bi, pubkey, "desktop_node",
                clean_string(data.get("deviceLabel", ""), 120))
        if not rec.get("pubkey"):
            rec["pubkey"] = pubkey
            await _save_account(env, name_bi, rec)
        caps = device.get("capabilities", DESKTOP_NODE_CAPABILITIES) if device else DESKTOP_NODE_CAPABILITIES
        if isinstance(caps, str):
            caps = [c for c in caps.split(",") if c]
        enabled = bool(device.get("enabled", True)) and not bool(device.get("revoked_at", 0)) if device else True
        desktop_capable = enabled and "owner_sign" in caps
        device_kind = device.get("kind", "desktop_node") if device else "desktop_node"
        payload = await _account_public_payload(env, rec)
        return json_response(_with_session_capabilities(
            payload, session_kind="desktop_node", device_kind=device_kind,
            key_matched=True, desktop_capable=desktop_capable, capabilities=caps))

    payload = await _account_public_payload(env, rec)
    return json_response(_with_session_capabilities(
        payload, session_kind="account", device_kind="web_or_mobile",
        key_matched=False, desktop_capable=False))


def _random_bytes(n):
    return bytes(js_crypto.getRandomValues(Uint8Array.new(n)).to_py())


def _treasury_address(env):
    addr = (getattr(env, "TREASURY_SOLANA_ADDRESS", "") or
            getattr(env, "NODE_SOLANA_ADDRESS", "") or "").strip()
    return addr if SOLANA_RE.match(addr) else ""


# A running node calls this on an interval to stay eligible for the reward
# split. Signed with the account's key so only the key holder can mark its node
# online; optionally updates the payout Solana address.
async def _account_heartbeat(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    solana = clean_string(data.get("solana", ""), 64)
    avatar_png, avatar_error = clean_avatar_png(data.get("avatarPng", ""))
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    if avatar_error:
        status = 413 if avatar_error == "avatar_too_large" else 400
        return json_response({"error": avatar_error}, status=status)
    name_bi, rec = await _account_row(env, name)
    if not rec or rec.get("status") != "active":
        return json_response({"ok": True, "online": False})
    pubkey = rec.get("pubkey", "")
    if not pubkey or not _ts_ok(ts):
        return json_response({"error": "unauthorized"}, status=401)
    canonical = ("forkmesh-heartbeat-v1\n" + name + "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)

    # Issue #346: the node itself is the only thing that knows when its local
    # Claude Code usage window refilled after running out, so it rides this
    # already-signed heartbeat to ask for a notification (opt-in on the
    # desktop side). Not part of the signed canonical string, same as solana/
    # avatarPng above — worst case a stale replay re-flags a notification the
    # recipient already has, not a forgeable action on someone else's account.
    credits_kind = clean_string(data.get("creditsRefilled", ""), 10)
    if credits_kind in ("5h", "weekly"):
        window = "5-hour" if credits_kind == "5h" else "weekly"
        await enqueue_notification(
            env, name, "credits_refilled",
            "Claude Code credits refilled",
            body=("Your " + window + " usage window has reset — Claude Code "
                  "credits are available again."),
            source="credits_refilled",
            dedupe="credits_refilled:" + credits_kind,
        )

    # Keep the payout address current if the node sent a valid one.
    if solana and SOLANA_RE.match(solana) and rec.get("solana") != solana:
        rec["solana"] = solana
        await _save_account(env, name_bi, rec)
    if avatar_png and rec.get("avatar_png") != avatar_png:
        rec["avatar_png"] = avatar_png
        rec["avatar_updated_at"] = int(Date.now())
        await _save_account(env, name_bi, rec)

    await d1_run(
        env,
        "INSERT INTO account_presence (name_bi, ts) VALUES (?, ?) "
        "ON CONFLICT(name_bi) DO UPDATE SET ts=excluded.ts",
        name_bi, int(Date.now()),
    )

    # Report this node's payout-wallet balance in the heartbeat reply so the
    # client doesn't have to poll Solana itself. We track the last balance we
    # saw for this account; an increase since then means a donation/disbursement
    # arrived, which the client surfaces (and only then refreshes its display).
    wallet = rec.get("solana", "")
    balance_lamports = None
    donation_received = False
    if wallet and SOLANA_RE.match(wallet):
        balance_lamports = await _solana_balance_lamports(env, wallet)
        if balance_lamports is not None:
            prev = rec.get("last_balance_lamports")
            if isinstance(prev, int) and balance_lamports > prev:
                donation_received = True
            if prev != balance_lamports:
                rec["last_balance_lamports"] = balance_lamports
                await _save_account(env, name_bi, rec)

    response = {"ok": True, "online": True,
                "hasPayoutAddress": bool(rec.get("solana")),
                "isAdmin": await _is_admin(env, name)}
    if balance_lamports is not None:
        response["balanceLamports"] = balance_lamports
        response["donationReceived"] = donation_received
    # A pending website claim (adhoc #53) rides back on the signed heartbeat:
    # only the node's key holder ever sees the confirmation code, and the node
    # shows it on its own screen for the claiming user to type into the site.
    # Expired claims are cleaned off the record here.
    claim = _claim_pending(rec, int(Date.now()))
    if claim:
        response["claim"] = {"user": claim.get("user", ""),
                             "code": claim.get("code", "")}
    elif rec.get("claim_pending"):
        rec.pop("claim_pending", None)
        await _save_account(env, name_bi, rec)
    # An admin-initiated ownership takeover (adhoc #141) rides back the same
    # way: only the node's own signed heartbeat carries it, so only whoever is
    # actually logged into that machine ever sees the prompt.
    transfer = _transfer_pending(rec, int(Date.now()))
    if transfer:
        response["ownershipTransfer"] = {"admin": transfer.get("admin", "")}
    elif rec.get("ownership_transfer_pending"):
        rec.pop("ownership_transfer_pending", None)
        await _save_account(env, name_bi, rec)
    return json_response(response)


async def _mirroring_owners(env):
    # Set of owner names that actually mirror a repo for another node, used to
    # keep single-repo-only nodes out of the donation split (issue #94). Best
    # effort: on any read error return None so callers keep their prior behaviour
    # (fail open) rather than starving every node of its share.
    try:
        rows = await d1_all(
            env, "SELECT data FROM repositories WHERE is_private = 0")
    except Exception:
        return None
    records = []
    for row in rows or []:
        rec = await decrypt_row(env, row.get("data"))
        if not rec:
            continue
        if _is_blocked_catalog_identity(env, rec.get("owner"), rec.get("name")):
            continue
        records.append(rec)
    return mirroring_owner_set(records)


async def _online_payout_addresses(env):
    # Deduplicated payout addresses of currently-online, active accounts that
    # actually mirror a repo for another node. A node hosting only its own
    # un-mirrored repos adds no redundancy, so it is left out of the split
    # (issue #94). Stale presence rows self-heal here so the table stays bounded.
    cutoff = int(Date.now()) - ACCOUNT_PRESENCE_STALE_MS
    try:
        await d1_run(env, "DELETE FROM account_presence WHERE ts < ?", cutoff)
    except Exception:
        pass
    mirroring = await _mirroring_owners(env)
    rows = await d1_all(
        env, "SELECT name_bi FROM account_presence WHERE ts >= ? ORDER BY ts DESC",
        cutoff,
    )
    seen = set()
    addresses = []
    for r in rows:
        row = await d1_first(
            env, "SELECT name, data FROM accounts WHERE name_bi=?", r["name_bi"])
        if not row:
            continue
        rec = await decrypt_row(env, row["data"])
        if not rec or rec.get("status") != "active":
            continue
        owner = clean_string(row.get("name") or rec.get("name", ""), MAX_NODE_NAME)
        # Only nodes that mirror a repo shared with another node earn a share.
        if mirroring is not None and owner.lower() not in mirroring:
            continue
        solana = (rec.get("solana") or "").strip()
        if not solana or not SOLANA_RE.match(solana) or solana in seen:
            continue
        balance = await _solana_balance_lamports(env, solana)
        if balance is None or balance < MIN_ACTIVE_LAMPORTS:
            continue
        seen.add(solana)
        addresses.append(solana)
    # Main relay: also disburse to every online node on every approved federated
    # relay (fresh federated_presence rows), so the node split spans all relays.
    if _is_main_relay(env):
        try:
            fed = await d1_all(
                env,
                "SELECT fp.wallet AS wallet FROM federated_presence fp "
                "JOIN relays r ON r.relay_bi = fp.relay_bi "
                "WHERE fp.ts >= ? AND r.status = 'approved'",
                cutoff)
            for r in (fed or []):
                wallet = (r.get("wallet") or "").strip()
                if wallet and SOLANA_RE.match(wallet) and wallet not in seen:
                    balance = await _solana_balance_lamports(env, wallet)
                    if balance is None or balance < MIN_ACTIVE_LAMPORTS:
                        continue
                    seen.add(wallet)
                    addresses.append(wallet)
        except Exception:
            pass
    return addresses


def _set_sweep_error(rec, message):
    if rec.get("donation_sweep_error") == message:
        return False
    rec["donation_sweep_error"] = message
    rec["donation_sweep_checked_at"] = int(Date.now())
    return True


async def _sweep_confirmed_donation(env, name_bi, rec, balance=None):
    # Idempotent: once a transaction signature is stored, later signup polls and
    # admin retries only report it instead of sending another transaction.
    if rec.get("donation_sweep_sig"):
        return False
    from_addr = rec.get("donation_address", "")
    secret = rec.get("donation_secret", "")
    if not SOLANA_RE.match(from_addr or "") or not secret:
        return False
    treasury = _treasury_address(env)
    if not treasury:
        return _set_sweep_error(rec, "treasury_not_configured")
    if balance is None:
        balance = await _solana_balance_lamports(env, from_addr)
    if balance is None:
        return _set_sweep_error(rec, "balance_unavailable")
    transferable = int(balance) - SOLANA_SWEEP_FEE_RESERVE_LAMPORTS
    if transferable <= 0:
        return _set_sweep_error(rec, "balance_too_low_for_fee")

    payees = [p for p in await _online_payout_addresses(env) if p != from_addr]
    treasury_lamports = transferable
    transfers = []
    if payees:
        treasury_lamports = (transferable * TREASURY_SPLIT_NUMERATOR //
                             TREASURY_SPLIT_DENOMINATOR)
        node_pool = transferable - treasury_lamports
        per_node = node_pool // len(payees)
        if per_node > 0:
            for payee in payees:
                transfers.append((payee, per_node))
            treasury_lamports = transferable - (per_node * len(payees))
    transfers.insert(0, (treasury, treasury_lamports))

    # Combine duplicate destinations while preserving order.
    merged = []
    by_addr = {}
    for addr, lamports in transfers:
        if addr in by_addr:
            by_addr[addr] += int(lamports)
        else:
            by_addr[addr] = int(lamports)
            merged.append(addr)
    transfers = [(addr, by_addr[addr]) for addr in merged if by_addr[addr] > 0]
    sig = await _solana_send_transfers(env, from_addr, secret, transfers)
    if not sig:
        return _set_sweep_error(rec, "send_transaction_failed")
    rec["donation_sweep_sig"] = sig
    rec["donation_sweep_at"] = int(Date.now())
    rec["donation_sweep_transfers"] = [
        {"address": addr, "lamports": lamports} for addr, lamports in transfers
    ]
    # Credit each online node its share for the "funds received · mainnodes"
    # board (the treasury cut is not a leaderboard recipient).
    for addr, lamports in transfers:
        if addr != treasury:
            await _record_funds_received(env, "mainnode", addr, "", lamports)
    rec.pop("donation_sweep_error", None)
    rec.pop("donation_sweep_checked_at", None)
    return True


# --- Central donation fund (issue #308) -------------------------------------
# One worker-custodied Solana wallet that anyone can donate to. A cron sweeps its
# whole balance out to the currently-online nodes once an hour, so a single
# donation address fans out to everyone keeping the network alive. Only the main
# relay custodies it (federated relays proxy the address lookup, like treasury).
async def _central_fund_record(env):
    # Load-or-create the fund keypair, kept in D1 so the operator needs no key
    # management. Mirrors _relay_identity's single-row pattern.
    row = await d1_first(env, "SELECT data FROM central_fund WHERE id=1")
    if row:
        rec = await decrypt_row(env, row.get("data"))
        if rec and rec.get("address") and rec.get("secret"):
            return rec
    addr, secret = await _new_solana_keypair()
    if not addr:
        return None
    rec = {"address": addr, "secret": secret}
    await _save_central_fund(env, rec)
    return rec


async def _save_central_fund(env, rec):
    await d1_run(
        env,
        "INSERT INTO central_fund (id, data) VALUES (1, ?) "
        "ON CONFLICT(id) DO UPDATE SET data=excluded.data",
        await encrypt_row(env, rec))


async def _central_fund_public(env):
    # Public donation payload: the fund's address, live balance, a Solana Pay URI,
    # and the last hourly distribution so a client can show a QR + status.
    rec = await _central_fund_record(env)
    if not rec:
        return None
    addr = rec.get("address", "")
    balance = await _solana_balance_lamports(env, addr)
    return {
        "address": addr,
        "uri": _solana_pay_uri(addr, 0, message="ForkMesh central fund"),
        "balanceLamports": balance if balance is not None else 0,
        "balanceSol": _amount_sol(balance or 0),
        "distributionIntervalMs": CENTRAL_FUND_DISTRIBUTION_INTERVAL_MS,
        "lastDistributionAt": int(rec.get("last_distribution_at", 0) or 0),
        "lastDistributionSig": rec.get("last_distribution_sig", ""),
        "lastDistributionLamports": int(rec.get("last_distribution_lamports", 0) or 0),
        "lastDistributionPayees": int(rec.get("last_distribution_payees", 0) or 0),
    }


async def _distribute_central_fund(env):
    # Cron: once an hour, sweep the whole central fund out to the currently-online
    # nodes, split evenly. Main relay only; best-effort. The interval is measured
    # from the last SUCCESSFUL distribution, so a donation that lands after a quiet
    # spell still goes out at the next tick.
    if not _is_main_relay(env):
        return False
    rec = await _central_fund_record(env)
    if not rec:
        return False
    now = int(Date.now())
    last = int(rec.get("last_distribution_at", 0) or 0)
    if last and now - last < CENTRAL_FUND_DISTRIBUTION_INTERVAL_MS:
        return False
    from_addr = rec.get("address", "")
    secret = rec.get("secret", "")
    if not SOLANA_RE.match(from_addr or "") or not secret:
        return False
    balance = await _solana_balance_lamports(env, from_addr)
    if balance is None:
        return False
    transferable = int(balance) - SOLANA_SWEEP_FEE_RESERVE_LAMPORTS
    if transferable < CENTRAL_FUND_MIN_DISTRIBUTION_LAMPORTS:
        return False
    payees = [p for p in await _online_payout_addresses(env) if p != from_addr]
    if not payees:
        return False
    per_node = transferable // len(payees)
    if per_node <= 0:
        return False
    transfers = [(payee, per_node) for payee in payees]
    sig = await _solana_send_transfers(env, from_addr, secret, transfers)
    if not sig:
        return False
    rec["last_distribution_at"] = now
    rec["last_distribution_sig"] = sig
    rec["last_distribution_lamports"] = per_node * len(payees)
    rec["last_distribution_payees"] = len(payees)
    await _save_central_fund(env, rec)
    # Credit each node its share for the "funds received · mainnodes" board.
    for payee, lamports in transfers:
        await _record_funds_received(env, "mainnode", payee, "", lamports)
    return True


# --- Issue bounties ---------------------------------------------------------
# Same custody model as the signup donation funnel: each bounty gets its OWN
# freshly generated Solana deposit address; the worker holds the key (encrypted
# at rest) and, when the issue's pull request merges, splits the balance 90% to
# the PR author and 10% to the treasury. Mainnet.
BOUNTY_MIN_USD = 1.0
BOUNTY_MAX_USD = 100000.0
BOUNTY_TREASURY_BPS = 1000  # 10% to treasury, in basis points


async def _usd_to_lamports(env, usd):
    price = await _sol_usd_price(env)  # USD per SOL
    if price <= 0:
        return 0
    return int(round((float(usd) / price) * LAMPORTS_PER_SOL))


async def _bounty_bi(env, owner, repo, number, kind=""):
    # kind "" is an issue bounty (key "<owner>/<repo>#<number>", unchanged);
    # kind "pr" is a per-pull-request bounty (key "…#pr-<number>"), so an issue
    # and a pull request that happen to share a number never collide.
    prefix = (kind + "-") if kind else ""
    return await blind_index(
        env, owner + "/" + repo + "#" + prefix + str(int(number)))


async def _bounty_wallet_bi(env, owner):
    # The owner's inbuilt bounty wallet — a single custody deposit key per owner,
    # pre-funded by the owner and debited to auto-pay per-PR bounties.
    return await blind_index(env, "bounty-wallet:" + owner)


async def _load_bounty_wallet(env, wallet_bi):
    row = await d1_first(
        env, "SELECT data FROM bounty_wallet WHERE wallet_bi=?", wallet_bi)
    if not row:
        return None
    return await decrypt_row(env, row.get("data", ""))


async def _save_bounty_wallet(env, wallet_bi, rec):
    enc = await encrypt_row(env, rec)
    await d1_run(
        env,
        """INSERT INTO bounty_wallet (wallet_bi, data) VALUES (?,?)
           ON CONFLICT(wallet_bi) DO UPDATE SET data=excluded.data""",
        wallet_bi, enc,
    )


async def _load_bounty(env, bounty_bi):
    row = await d1_first(
        env, "SELECT data FROM issue_bounty WHERE bounty_bi=?", bounty_bi)
    if not row:
        return None
    return await decrypt_row(env, row.get("data", ""))


async def _save_bounty(env, bounty_bi, rec):
    enc = await encrypt_row(env, rec)
    await d1_run(
        env,
        """INSERT INTO issue_bounty (bounty_bi, data) VALUES (?,?)
           ON CONFLICT(bounty_bi) DO UPDATE SET data=excluded.data""",
        bounty_bi, enc,
    )


def _bounty_public(rec):
    # Never leak the deposit key; only payout-safe fields go back to clients.
    return {
        "address": rec.get("address", ""),
        "amountUsd": rec.get("amount_usd", 0),
        "requiredLamports": rec.get("required_lamports", 0),
        "amountSol": _amount_sol(rec.get("required_lamports", 0)),
        "receivedLamports": rec.get("received_lamports", 0),
        "confirmed": bool(rec.get("confirmed")),
        "status": rec.get("status", "open"),
        "payee": rec.get("payee", ""),
        "payoutSig": rec.get("payout_sig", ""),
        "uri": _solana_pay_uri(
            rec.get("address", ""), int(rec.get("required_lamports", 0)),
            message="ForkMesh bounty"),
    }


async def _bounty_mark_paid(env, bounty_bi, rec):
    # Terminal transition for a payout: flip the row to "paid", drop the retained
    # raw transaction, and run the accounting/notification side effects exactly
    # once. Only ever called after the transfer is known to have hit the chain.
    rec["status"] = "paid"
    rec["paid_at"] = int(Date.now())
    rec.pop("payout_tx", None)
    transfers = [(t.get("address", ""), int(t.get("lamports", 0)))
                 for t in (rec.get("payout_transfers") or [])]
    await _save_bounty(env, bounty_bi, rec)
    await _record_bounty_payout(env, rec, transfers)
    await notify_bounty_event(env, rec, "bounty_paid")
    return rec


async def _bounty_auto_payout(env, bounty_bi, rec):
    # Split a funded escrow to the resolved payee (the merged PR's author) and the
    # treasury, with no second manual step. Safe to call repeatedly: it no-ops
    # unless the escrow has a balance, a payee, and hasn't already been paid, and
    # it never issues a second on-chain transfer for the same row.
    if not rec or rec.get("status") == "paid":
        return rec
    # Only ever auto-pay a payee the repo owner explicitly authorized (set via an
    # owner-signed create/payout). This is the gate that makes a funded escrow
    # un-stealable by an unauthenticated caller.
    if not rec.get("payee_authorized"):
        return rec
    payee = rec.get("payee", "")
    if not SOLANA_RE.match(payee or ""):
        return rec
    treasury = _treasury_address(env)
    if not treasury:
        return rec
    from_addr = rec.get("address", "")
    secret = rec.get("secret", "")
    if not SOLANA_RE.match(from_addr or "") or not secret:
        return rec

    # Idempotent resume. A previous attempt may have signed + recorded a payout
    # (status "paying") and then died around the broadcast. Before signing anything
    # new we ask the chain what happened to that recorded signature: if it settled
    # we finalize without paying again; if it never landed we rebroadcast the SAME
    # bytes (which the cluster dedupes should it have landed in a race).
    pending_sig = rec.get("payout_sig", "")
    pending_tx = rec.get("payout_tx", "")
    if pending_sig:
        if await _solana_signature_landed(env, pending_sig):
            return await _bounty_mark_paid(env, bounty_bi, rec)
        if pending_tx and await _solana_broadcast_raw(env, pending_tx):
            return await _bounty_mark_paid(env, bounty_bi, rec)
        # Otherwise the recorded blockhash likely expired without the tx ever
        # landing; fall through and sign a fresh transaction below.

    balance = await _solana_balance_lamports(env, from_addr)
    if balance is None:
        return rec
    transferable = int(balance) - SOLANA_SWEEP_FEE_RESERVE_LAMPORTS
    if transferable <= 0:
        # Nothing left to move. If we had a recorded signature the earlier transfer
        # must have drained the escrow, so finalize instead of looping forever.
        if pending_sig:
            return await _bounty_mark_paid(env, bounty_bi, rec)
        return rec
    treasury_lamports = transferable * BOUNTY_TREASURY_BPS // 10000
    payee_lamports = transferable - treasury_lamports
    transfers = []
    if payee_lamports > 0:
        transfers.append((payee, payee_lamports))
    if treasury_lamports > 0:
        transfers.append((treasury, treasury_lamports))

    # Sign first, PERSIST the signature + raw bytes (status "paying"), THEN
    # broadcast. If the worker dies between the save and a confirmed broadcast, the
    # resume path above rebroadcasts these exact bytes — it can never sign a
    # second, different transfer for this row, so a mid-payout RPC failure on
    # retry cannot double-pay.
    sig, tx_b64 = await _solana_sign_transfers(env, from_addr, secret, transfers)
    if not sig or not tx_b64:
        return rec
    rec["status"] = "paying"
    rec["payout_sig"] = sig
    rec["payout_tx"] = tx_b64
    rec["payout_transfers"] = [
        {"address": a, "lamports": l} for a, l in transfers]
    await _save_bounty(env, bounty_bi, rec)

    send_sig = await _solana_broadcast_raw(env, tx_b64)
    if not send_sig:
        # Broadcast failed outright. Leave the row "paying" so the next tick
        # resumes it (rechecks the signature, rebroadcasts the same bytes).
        return rec
    return await _bounty_mark_paid(env, bounty_bi, rec)


async def sweep_funded_bounties(env):
    # Cron backstop: pay out any funded bounty whose escrow holds a balance and
    # has a resolved payee, so the author/treasury split happens even if no client
    # ever polls status. Best-effort per row.
    try:
        rows = await d1_all(env, "SELECT bounty_bi, data FROM issue_bounty")
    except Exception:
        return
    for row in rows:
        try:
            rec = await decrypt_row(env, row["data"])
        except Exception:
            continue
        if (not rec or rec.get("status") == "paid" or not rec.get("payee") or
                not rec.get("payee_authorized") or
                not rec.get("address") or not rec.get("secret")):
            continue
        try:
            await _bounty_auto_payout(env, row["bounty_bi"], rec)
        except Exception:
            continue


async def bounties_handler(env, request, owner, repo):
    await ensure_schema(env)
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    action = (data.get("action") or "create").strip()

    if action == "wallet":
        # Mint (once) and report the owner's inbuilt bounty wallet: a custody
        # deposit address the owner pre-funds and that per-PR bounties in "wallet"
        # mode debit. Owner-signed so only the owner can learn/fund their wallet.
        try:
            ts = int(data.get("ts", 0))
        except (TypeError, ValueError):
            ts = 0
        if not _ts_ok(ts):
            return json_response({"error": "stale_request"}, status=401)
        owner_pub = await _owner_pubkey(env, owner)
        if not owner_pub:
            return json_response({"error": "no_owner_key"}, status=403)
        canonical = ("forkmesh-bounty-wallet-v1\n" + owner + "\n" +
                     str(ts)).encode()
        if not await ed25519_verify(owner_pub, data.get("sig", ""), canonical):
            return json_response({"error": "bad_signature"}, status=401)
        wallet_bi = await _bounty_wallet_bi(env, owner)
        wrec = await _load_bounty_wallet(env, wallet_bi)
        if not wrec or not wrec.get("address"):
            addr, secret = await _new_solana_keypair()
            if not addr:
                return json_response({"error": "keypair_failed"}, status=500)
            wrec = {"owner": owner, "address": addr, "secret": secret,
                    "created_at": int(Date.now())}
            await _save_bounty_wallet(env, wallet_bi, wrec)
        balance = await _solana_balance_lamports(env, wrec["address"])
        balance = int(balance) if balance is not None else 0
        return json_response({
            "address": wrec["address"],
            "balanceLamports": balance,
            "balanceSol": _amount_sol(balance),
            "uri": _solana_pay_uri(wrec["address"], 0,
                                   message="ForkMesh bounty wallet"),
        })

    try:
        number = int(data.get("number", 0))
    except (TypeError, ValueError):
        number = 0
    if number <= 0:
        return json_response({"error": "number_required"}, status=400)
    # kind "" is an issue bounty; "pr" is a per-pull-request bounty (issue #347),
    # keyed separately so an issue and a PR sharing a number never collide.
    kind = clean_string(data.get("kind", ""), 8)
    if kind not in ("", "pr"):
        kind = ""
    bounty_bi = await _bounty_bi(env, owner, repo, number, kind)
    rec = await _load_bounty(env, bounty_bi)

    if action == "create":
        # "paying" means a payout is mid-flight; treat it like "paid" so a re-create
        # can't repoint the escrow or reset its amount out from under the transfer.
        if rec and rec.get("status") in ("paid", "paying"):
            return json_response({"error": "already_paid"}, status=409)
        treasury = _treasury_address(env)
        if not treasury:
            return json_response({"error": "treasury_not_configured"}, status=503)
        try:
            amount_usd = float(data.get("amountUsd", 0))
        except (TypeError, ValueError):
            amount_usd = 0.0
        if not (BOUNTY_MIN_USD <= amount_usd <= BOUNTY_MAX_USD):
            return json_response({"error": "bad_amount"}, status=400)
        required = await _usd_to_lamports(env, amount_usd)
        if required <= 0:
            return json_response({"error": "price_unavailable"}, status=503)
        # Creating/funding a bounty AND choosing who it pays out to are both
        # owner-only: the repo owner signs the create so a funded escrow can only
        # ever be released to an owner-approved payee. Without this gate anyone
        # could repoint an unpaid bounty's payee to their own wallet and let the
        # auto-payout drain it. The payee identifier (explicit address, else the
        # PR author's node name) is bound into the signature.
        payee_addr = clean_string(data.get("payee", ""), 64)
        payee_node = clean_string(data.get("payeeNode", ""), MAX_NODE_NAME).lower()
        payee_id = (payee_addr if (payee_addr and SOLANA_RE.match(payee_addr))
                    else payee_node)
        try:
            ts = int(data.get("ts", 0))
        except (TypeError, ValueError):
            ts = 0
        sig = data.get("sig", "")
        if not _ts_ok(ts):
            return json_response({"error": "stale_request"}, status=401)
        owner_pub = await _owner_pubkey(env, owner)
        if not owner_pub:
            return json_response({"error": "no_owner_key"}, status=403)
        canonical = ("forkmesh-bounty-create-v1\n" + owner + "\n" + repo + "\n" +
                     str(number) + "\n" + payee_id + "\n" + str(ts)).encode()
        if not await ed25519_verify(owner_pub, sig, canonical):
            return json_response({"error": "bad_signature"}, status=401)
        # Resolve the signed payee identifier to a Solana address. An owner-signed
        # create with a resolvable payee marks it authorized for auto-payout.
        payee = ""
        if payee_addr and SOLANA_RE.match(payee_addr):
            payee = payee_addr
        elif payee_node:
            _, author_rec = await _account_row(env, payee_node)
            cand = (author_rec or {}).get("solana", "")
            if cand and SOLANA_RE.match(cand):
                payee = cand

        # Wallet mode (issue #347): instead of minting an escrow for the owner to
        # fund by hand, debit the owner's pre-funded inbuilt bounty wallet and pay
        # the split directly — the bounty is settled in one step.
        if data.get("fromWallet"):
            if not payee:
                return json_response({"error": "payee_unresolved"}, status=400)
            wallet_bi = await _bounty_wallet_bi(env, owner)
            wrec = await _load_bounty_wallet(env, wallet_bi)
            if not wrec or not wrec.get("address") or not wrec.get("secret"):
                return json_response({"error": "no_wallet"}, status=409)
            balance = await _solana_balance_lamports(env, wrec["address"])
            balance = int(balance) if balance is not None else 0
            needed = required + SOLANA_SWEEP_FEE_RESERVE_LAMPORTS
            if balance < needed:
                return json_response(
                    {"error": "insufficient_wallet_balance",
                     "balanceLamports": balance,
                     "requiredLamports": required}, status=402)
            treasury_lamports = required * BOUNTY_TREASURY_BPS // 10000
            payee_lamports = required - treasury_lamports
            transfers = []
            if payee_lamports > 0:
                transfers.append((payee, payee_lamports))
            if treasury_lamports > 0:
                transfers.append((treasury, treasury_lamports))
            send_sig = await _solana_send_transfers(
                env, wrec["address"], wrec["secret"], transfers)
            if not send_sig:
                return json_response({"error": "wallet_transfer_failed"},
                                     status=502)
            rec = {
                "owner": owner, "repo": repo, "number": number, "kind": kind,
                "address": "", "secret": "",
                "amount_usd": amount_usd, "required_lamports": required,
                "received_lamports": required, "confirmed": True,
                "status": "paid", "created_at": int(Date.now()),
                "payee": payee, "payee_authorized": True,
                "payout_sig": send_sig, "paid_at": int(Date.now()),
                "paid_from_wallet": True,
                "payout_transfers": [
                    {"address": a, "lamports": l} for a, l in transfers],
            }
            await _save_bounty(env, bounty_bi, rec)
            await _record_bounty_payout(env, rec, transfers)
            await notify_bounty_event(env, rec, "bounty_paid")
            return json_response(_bounty_public(rec))

        # Reuse an existing unpaid address (top-ups raise the target) so a repeat
        # call doesn't strand funds at a stale address.
        if (rec and rec.get("address") and
                rec.get("status") not in ("paid", "paying")):
            rec["amount_usd"] = amount_usd
            rec["required_lamports"] = required
            if payee:
                rec["payee"] = payee
                rec["payee_authorized"] = True
        else:
            addr, secret = await _new_solana_keypair()
            if not addr:
                return json_response({"error": "keypair_failed"}, status=500)
            rec = {
                "owner": owner, "repo": repo, "number": number, "kind": kind,
                "address": addr, "secret": secret,
                "amount_usd": amount_usd, "required_lamports": required,
                "received_lamports": 0, "confirmed": False, "status": "open",
                "created_at": int(Date.now()), "payee": payee,
                "payee_authorized": bool(payee), "payout_sig": "",
            }
        await _save_bounty(env, bounty_bi, rec)
        return json_response(_bounty_public(rec))

    if rec is None:
        return json_response({"error": "no_bounty"}, status=404)

    if action == "status":
        # A "paying" row has a payout mid-flight; resume it (finalize if it settled,
        # rebroadcast the recorded bytes otherwise) rather than re-polling balance.
        if rec.get("status") == "paying":
            rec = await _bounty_auto_payout(env, bounty_bi, rec)
        elif rec.get("status") != "paid" and rec.get("address"):
            previous_status = rec.get("status", "open")
            balance = await _solana_balance_lamports(env, rec["address"])
            if balance is not None:
                rec["received_lamports"] = balance
                if balance >= int(rec.get("required_lamports", 0)) and balance > 0:
                    rec["confirmed"] = True
                    if rec.get("status") == "open":
                        rec["status"] = "funded"
                await _save_bounty(env, bounty_bi, rec)
                if previous_status != "funded" and rec.get("status") == "funded":
                    await notify_bounty_event(env, rec, "bounty_funded")
                # As soon as it's funded and we have an owner-authorized payee,
                # split it — no second manual payout step is needed.
                if (rec.get("status") == "funded" and rec.get("payee") and
                        rec.get("payee_authorized")):
                    rec = await _bounty_auto_payout(env, bounty_bi, rec)
        return json_response(_bounty_public(rec))

    if action == "payout":
        # Only the repo owner (the account that can merge the PR) may release a
        # bounty. Signature is over a fresh, explicit canonical string.
        payee = clean_string(data.get("payee", ""), 64)
        try:
            ts = int(data.get("ts", 0))
        except (TypeError, ValueError):
            ts = 0
        sig = data.get("sig", "")
        if not SOLANA_RE.match(payee or ""):
            return json_response({"error": "bad_payee"}, status=400)
        if not _ts_ok(ts):
            return json_response({"error": "stale_request"}, status=400)
        pubkey = await _owner_pubkey(env, owner)
        if not pubkey:
            return json_response({"error": "no_owner_key"}, status=403)
        canonical = ("forkmesh-bounty-payout-v1\n" + owner + "\n" + repo + "\n" +
                     str(number) + "\n" + payee + "\n" + str(ts)).encode()
        if not await ed25519_verify(pubkey, sig, canonical):
            return json_response({"error": "bad_signature"}, status=401)
        if rec.get("status") == "paid" and rec.get("payout_sig"):
            return json_response(_bounty_public(rec))  # idempotent
        treasury = _treasury_address(env)
        if not treasury:
            return json_response({"error": "treasury_not_configured"}, status=503)
        from_addr = rec.get("address", "")
        secret = rec.get("secret", "")
        if not SOLANA_RE.match(from_addr or "") or not secret:
            return json_response({"error": "bounty_unfunded"}, status=409)
        # Authorize the signed payee, then run the SAME idempotent split the
        # on-merge path uses (sign → record → broadcast, resume-safe). Keeping one
        # payout implementation is what guarantees a retry here can't double-pay.
        rec["payee"] = payee
        rec["payee_authorized"] = True
        await _save_bounty(env, bounty_bi, rec)
        rec = await _bounty_auto_payout(env, bounty_bi, rec)
        if rec.get("status") != "paid":
            return json_response({"error": "send_transaction_failed"}, status=502)
        return json_response(_bounty_public(rec))

    return json_response({"error": "bad_action"}, status=400)


# Cap on collaborators per repo, so one owner can't fill the table with shares.
MAX_REPO_GRANTEES = 100


async def shares_handler(env, request, owner, repo):
    # Private-repo collaborator ACL (issue #9). Every operation is authorized by
    # the OWNER account's key — only the owner may grant/revoke/list who a private
    # repo is shared with. A grantee then sees the repo in their authenticated
    # catalog and clones it with their own key (verify_share_view_token); none of
    # that is gated here, only the membership list is.
    await ensure_schema(env)
    method = method_name(request)
    repo_bi = await blind_index(env, owner + "/" + repo)
    owner_pub = await _owner_pubkey(env, owner)
    if not owner_pub:
        return json_response({"error": "account_required"}, status=403)

    if method == "GET":
        # List current grantees for the owner's Collaborators UI. Owner-signed so
        # the membership of a private repo isn't world-readable.
        params = parse_qs(urlparse(request.url).query)
        ts = clean_string(params.get("ts", [""])[0], 20)
        sig = clean_string(params.get("sig", [""])[0], 200)
        canonical = ("forkmesh-shares-list-v1\n" + owner + "\n" + repo + "\n" +
                     str(ts)).encode()
        if not (_ts_ok(ts) and await ed25519_verify(owner_pub, sig, canonical)):
            return json_response({"error": "unauthorized"}, status=401)
        rows = await d1_all(
            env, "SELECT data FROM repo_shares WHERE repo_bi=? ORDER BY ts ASC",
            repo_bi)
        grantees = []
        for row in rows:
            rec = await decrypt_row(env, row["data"])
            if rec and rec.get("grantee"):
                grantees.append(rec["grantee"])
        return json_response({"ok": True, "grantees": grantees})

    if method in ("POST", "DELETE"):
        try:
            data = await request.json()
        except Exception:
            data = {}
        # DELETE is accepted as an alias for action=remove so the verb can carry
        # intent even when a client can't easily attach a body to a DELETE.
        action = "remove" if method == "DELETE" else \
            (clean_string(data.get("action", "add"), 12) or "add")
        if action not in ("add", "remove"):
            return json_response({"error": "bad_action"}, status=400)
        grantee = clean_string(data.get("grantee", ""), MAX_NODE_NAME).lower()
        ts = clean_string(str(data.get("ts", "")), 20)
        sig = clean_string(data.get("sig", ""), 200)
        if not grantee:
            return json_response({"error": "grantee_required"}, status=400)
        if grantee == owner:
            return json_response({"error": "cannot_share_with_self"}, status=400)
        # The action is bound into the signature so an "add" token can never be
        # replayed as a "remove" (or vice versa).
        canonical = ("forkmesh-share-v1\n" + owner + "\n" + repo + "\n" +
                     grantee + "\n" + action + "\n" + str(ts)).encode()
        if not (_ts_ok(ts) and await ed25519_verify(owner_pub, sig, canonical)):
            return json_response({"error": "unauthorized"}, status=401)
        grantee_bi = await blind_index(env, grantee)

        if action == "remove":
            await d1_run(
                env, "DELETE FROM repo_shares WHERE repo_bi=? AND grantee_bi=?",
                repo_bi, grantee_bi)
            return json_response({"ok": True, "grantee": grantee, "shared": False})

        # add: the grantee must be a real account (so a token signed by their key
        # can ever verify), and the per-repo cap must not be exceeded.
        if not await _owner_pubkey(env, grantee):
            return json_response({"error": "unknown_account"}, status=404)
        existing = await d1_first(
            env, "SELECT 1 AS one FROM repo_shares WHERE repo_bi=? AND grantee_bi=?",
            repo_bi, grantee_bi)
        if not existing:
            count_row = await d1_first(
                env, "SELECT COUNT(*) AS n FROM repo_shares WHERE repo_bi=?",
                repo_bi)
            if count_row and int(count_row.get("n") or 0) >= MAX_REPO_GRANTEES:
                return json_response({"error": "too_many_grantees"}, status=429)
        now = int(Date.now())
        enc = await encrypt_row(
            env, {"grantee": grantee, "owner": owner, "repo": repo, "ts": now})
        await d1_run(
            env,
            "INSERT INTO repo_shares (repo_bi, grantee_bi, data, ts) "
            "VALUES (?,?,?,?) ON CONFLICT(repo_bi, grantee_bi) DO UPDATE SET "
            "data=excluded.data, ts=excluded.ts",
            repo_bi, grantee_bi, enc, now)
        await enqueue_notification(env, grantee, "repo_shared",
                                   owner + " shared " + owner + "/" + repo + " with you",
                                   body="You can now view and clone this private repository with your own node key.",
                                   repo=owner + "/" + repo,
                                   href=repo_web_href(owner, repo),
                                   actor=owner, source="repo_share",
                                   dedupe="share:" + owner + "/" + repo + ":" + grantee)
        return json_response({"ok": True, "grantee": grantee, "shared": True})

    return json_response({"error": "method_not_allowed"}, status=405)


# --- Admins + manual email verification -------------------------------------
# A user is "set as administrator" via the accounts.is_admin column in the DB
# (UPDATE accounts SET is_admin=1 WHERE name='<node>'). Admins' clients are
# notified when a new user joins and can verify the new user's email by hand
# until an email service (e.g. SES) is wired up.

async def _is_admin(env, name):
    # Admin status lives entirely in the accounts table's is_admin column. Grant
    # it directly in the DB: UPDATE accounts SET is_admin=1 WHERE name='<node>'.
    name = (name or "").strip().lower()
    if not name:
        return False
    name_bi = await blind_index(env, name)
    try:
        row = await d1_first(
            env, "SELECT is_admin FROM accounts WHERE name_bi=?", name_bi)
    except Exception:
        return False  # column may predate migration 0006
    return bool(row and int(row.get("is_admin", 0) or 0))


async def _admin_authorized(env, node, ts, sig, canonical):
    # The requesting admin proves control of their node's key, and the account
    # must have is_admin set. Returns the admin's record on success, else None.
    node = (node or "").strip().lower()
    if not await _is_admin(env, node) or not _ts_ok(ts):
        return None
    _, rec = await _account_row(env, node)
    if not rec or rec.get("status") != "active":
        return None
    pubkey = rec.get("pubkey", "")
    if not pubkey or not await ed25519_verify(pubkey, sig, canonical):
        return None
    return rec


async def _enqueue_verification(env, name_bi, name, email):
    blob = await encrypt_row(
        env, {"name": name, "email": email, "joinedAt": int(Date.now())})
    await d1_run(
        env,
        "INSERT INTO pending_verifications (name_bi, data) VALUES (?,?) "
        "ON CONFLICT(name_bi) DO UPDATE SET data=excluded.data",
        name_bi, blob,
    )


async def _admin_pending(env, request):
    params = parse_qs(urlparse(request.url).query)
    node = clean_string(params.get("node", [""])[0], MAX_NODE_NAME).lower()
    ts = clean_string(params.get("ts", [""])[0], 20)
    sig = clean_string(params.get("sig", [""])[0], 200)
    canonical = ("forkmesh-admin-pending-v1\n" + node + "\n" + ts).encode()
    if not await _admin_authorized(env, node, ts, sig, canonical):
        return json_response({"error": "unauthorized"}, status=401)
    rows = await d1_all(env, "SELECT data FROM pending_verifications")
    pending = [rec for rec in
               [await decrypt_row(env, r["data"]) for r in rows] if rec]
    pending.sort(key=lambda x: x.get("joinedAt", 0))
    return json_response({"ok": True, "pending": pending})


async def _admin_verify_email(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    node = clean_string(data.get("node", ""), MAX_NODE_NAME).lower()
    target = clean_string(data.get("target", ""), MAX_NODE_NAME).lower()
    ts = clean_string(data.get("ts", ""), 20)
    sig = clean_string(data.get("sig", ""), 200)
    canonical = ("forkmesh-admin-verify-email-v1\n" + node + "\n" + target +
                 "\n" + ts).encode()
    if not await _admin_authorized(env, node, ts, sig, canonical):
        return json_response({"error": "unauthorized"}, status=401)

    target_bi, trec = await _account_row(env, target)
    if not trec:
        return json_response({"error": "no_such_account"}, status=404)
    trec["email_verified"] = True
    await _save_account(env, target_bi, trec)
    await d1_run(env, "DELETE FROM pending_verifications WHERE name_bi=?", target_bi)
    return json_response({"ok": True, "target": target, "emailVerified": True})


# --- Transactional email (Mailtrap) -----------------------------------------
# Signup confirmation goes out through Mailtrap's HTTP sending API. The token is
# a Worker secret (MAILTRAP_API_TOKEN, pushed from .env.production by deploy.sh).
# When it's unset the sender is a no-op, so a fork without an email provider
# still works — callers fall back to the admin verification queue.
MAILTRAP_SEND_URL = "https://send.api.mailtrap.io/api/send"


async def _send_email(env, to_email, subject, text, html=None):
    from js import fetch as js_fetch
    token = (getattr(env, "MAILTRAP_API_TOKEN", "") or "").strip()
    if not token or not to_email:
        return False
    sender = (getattr(env, "MAILTRAP_SENDER", "") or "no-reply@forkmesh.com").strip()
    sender_name = (getattr(env, "MAILTRAP_SENDER_NAME", "") or "ForkMesh").strip()
    url = (getattr(env, "MAILTRAP_API_URL", "") or MAILTRAP_SEND_URL).strip()
    payload = {
        "from": {"email": sender, "name": sender_name},
        "to": [{"email": to_email}],
        "subject": subject,
        "text": text,
    }
    if html:
        payload["html"] = html
    try:
        resp = await js_fetch(url, to_js({
            "method": "POST",
            "headers": {"content-type": "application/json",
                        "authorization": "Bearer " + token},
            "body": json.dumps(payload),
        }))
        return 200 <= int(getattr(resp, "status", 0)) < 300
    except Exception:
        return False


async def _email_verify_token(env, name, email):
    # Stateless, deterministic confirm token bound to (name, email). Only the
    # server can compute it (keyed by DATA_KEY via the blind-index HMAC key, with
    # its own domain-separation prefix), so a valid token in the link proves the
    # recipient controls the mailbox — no token column or extra storage needed.
    key = await _hmac_key(env)
    msg = ("forkmesh-email-verify-v1\n" + (name or "") + "\n" + (email or "")).encode()
    sig = await js_crypto.subtle.sign("HMAC", key, _to_js(msg))
    return bytes(Uint8Array.new(sig).to_py()).hex()[:32]


def _public_base_url(env, request):
    base = (getattr(env, "PUBLIC_BASE_URL", "") or "").strip().rstrip("/")
    if base:
        return base
    try:
        u = urlparse(request.url)
        if u.scheme and u.netloc:
            return u.scheme + "://" + u.netloc
    except Exception:
        pass
    return ""


async def _send_verification_email(env, request, name, email):
    token = await _email_verify_token(env, name, email)
    link = (_public_base_url(env, request) +
            "/api/accounts/verify-email?node=" + quote(name) + "&token=" + token)
    subject = "Confirm your ForkMesh email"
    text = ("Welcome to ForkMesh!\n\n"
            "Confirm the email for your node \"" + name + "\" by opening:\n" +
            link + "\n\n"
            "If you didn't create this account, you can ignore this email.")
    html = (
        "<p>Welcome to ForkMesh!</p>"
        "<p>Confirm the email for your node <strong>" + name + "</strong>:</p>"
        "<p><a href=\"" + link + "\">Confirm my email</a></p>"
        "<p style=\"color:#888;font-size:13px\">If you didn't create this account, "
        "you can ignore this email.</p>")
    return await _send_email(env, email, subject, text, html)


async def _password_reset_token(env, name, email, pass_hash, expires):
    # Stateless, deterministic reset token bound to (name, email, current
    # password hash, expiry). Only the server can compute it (keyed by DATA_KEY
    # via the blind-index HMAC key, with its own domain-separation prefix), so a
    # valid token in the link proves the recipient controls the mailbox — no
    # token column needed. Binding the *current* pass_hash makes the link
    # single-use: once the password changes the token no longer verifies. The
    # expiry (also carried in the link) bounds how long a leaked link is usable.
    key = await _hmac_key(env)
    msg = ("forkmesh-password-reset-v1\n" + (name or "") + "\n" + (email or "") +
           "\n" + (pass_hash or "") + "\n" + str(expires)).encode()
    sig = await js_crypto.subtle.sign("HMAC", key, _to_js(msg))
    return bytes(Uint8Array.new(sig).to_py()).hex()[:32]


async def _send_password_reset_email(env, request, name, email, pass_hash):
    expires = int(Date.now()) + PASSWORD_RESET_TTL_MS
    token = await _password_reset_token(env, name, email, pass_hash, expires)
    link = (_public_base_url(env, request) +
            "/reset-password.html?node=" + quote(name) +
            "&exp=" + str(expires) + "&token=" + token)
    subject = "Reset your ForkMesh password"
    text = ("A password reset was requested for your ForkMesh node \"" + name +
            "\".\n\nOpen this link to choose a new password:\n" + link +
            "\n\nThis link expires in 1 hour. If you didn't request a reset, "
            "you can ignore this email — your password won't change.")
    html = (
        "<p>A password reset was requested for your ForkMesh node "
        "<strong>" + name + "</strong>.</p>"
        "<p><a href=\"" + link + "\">Choose a new password</a></p>"
        "<p style=\"color:#888;font-size:13px\">This link expires in 1 hour. "
        "If you didn't request a reset, you can ignore this email — your "
        "password won't change.</p>")
    return await _send_email(env, email, subject, text, html)


# Step 1 of a password reset: a user who forgot their password gives their email
# (or node name); if it matches an active account with an email on file we send a
# time-bound reset link. Always returns {ok:true} — never revealing whether the
# identifier matched an account — so the endpoint can't be used to enumerate
# which emails/nodes are registered.
async def _account_forgot_password(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    identifier = clean_string(
        data.get("identifier", "") or data.get("email", "") or
        data.get("nodeName", ""), 254).strip().lower()
    if identifier:
        rec = None
        if "@" in identifier:
            email_bi = await blind_index(env, identifier)
            row = await d1_first(
                env, "SELECT data FROM accounts WHERE email_bi=?", email_bi)
            if row:
                rec = await decrypt_row(env, row.get("data"))
        elif valid_node_name(identifier):
            _, rec = await _account_row(env, identifier)
        if (rec and rec.get("status") == "active" and rec.get("email") and
                rec.get("pass_hash")):
            await _send_password_reset_email(
                env, request, rec.get("name", ""), rec.get("email", ""),
                rec.get("pass_hash", ""))
    return json_response({"ok": True})


# Step 2 of a password reset: the emailed link posts back the node name, expiry,
# token and a new password. The token is recomputed from the account's current
# pass_hash + the expiry, so a stale/used/tampered link fails, and the new
# password is PBKDF2-hashed and stored.
async def _account_reset_password(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(
        data.get("node", "") or data.get("nodeName", ""), MAX_NODE_NAME).lower()
    token = clean_string(data.get("token", ""), 64)
    password = (data.get("password", "") or "")[:256]
    try:
        expires = int(data.get("exp", 0))
    except (TypeError, ValueError):
        expires = 0
    if len(password) < 8:
        return json_response({"error": "password_too_short"}, status=400)
    if not name or not token or not expires:
        return json_response({"error": "invalid_reset_token"}, status=400)
    if int(Date.now()) > expires:
        return json_response({"error": "reset_link_expired"}, status=400)
    name_bi, rec = await _account_row(env, name)
    if (not rec or rec.get("status") != "active" or not rec.get("email") or
            not rec.get("pass_hash")):
        return json_response({"error": "invalid_reset_token"}, status=400)
    expected = await _password_reset_token(
        env, name, rec.get("email", ""), rec.get("pass_hash", ""), expires)
    if not hmac.compare_digest(token, expected):
        return json_response({"error": "invalid_reset_token"}, status=400)
    salt, phash = await hash_password(password)
    rec["pass_salt"] = salt
    rec["pass_hash"] = phash
    await _save_account(env, name_bi, rec)
    # A successful reset also lifts any brute-force lockout so the user can log in
    # right away, whether they log in by node name or by email.
    await _login_clear(env, name_bi)
    if rec.get("email"):
        await _login_clear(env, await blind_index(env, rec.get("email", "")))
    return json_response({"ok": True})


def _verify_email_page(message, status):
    body = ("<!doctype html><meta charset=utf-8><title>ForkMesh email</title>"
            "<body style=\"font-family:system-ui,sans-serif;max-width:32rem;"
            "margin:4rem auto;padding:0 1rem;line-height:1.5\">" + message +
            "</body>")
    return Response(body, status=status,
                    headers={"content-type": "text/html; charset=utf-8"})


async def _verify_email(env, request):
    params = parse_qs(urlparse(request.url).query)
    name = clean_string(params.get("node", [""])[0], MAX_NODE_NAME).lower()
    token = clean_string(params.get("token", [""])[0], 64)
    name_bi, rec = await _account_row(env, name)
    if rec and rec.get("email") and token:
        expected = await _email_verify_token(env, name, rec.get("email", ""))
        if hmac.compare_digest(token, expected):
            if not rec.get("email_verified"):
                rec["email_verified"] = True
                await _save_account(env, name_bi, rec)
                await d1_run(
                    env, "DELETE FROM pending_verifications WHERE name_bi=?", name_bi)
            return _verify_email_page(
                "<h1>Email confirmed</h1><p>Your ForkMesh email is verified. "
                "You can close this tab and <a href=\"/login.html\">log in</a>.</p>",
                200)
    return _verify_email_page(
        "<h1>Verification link invalid</h1><p>This confirmation link is invalid "
        "or has expired. Try logging in; if your email still shows unverified, "
        "sign up again.</p>", 400)


# --- Relay federation -------------------------------------------------------
# A relay with MAIN_RELAY_URL set is "federated": node signups custody their
# Solana on the main relay (funds flow through it) and its online nodes join the
# main relay's disbursement split. The main relay (no MAIN_RELAY_URL) holds an
# admin-approved allowlist of relays, custodies their signups, and disburses to
# every node on every approved relay. Relay->main calls are Ed25519-signed.

FEDERATION_CANON = "forkmesh-federation-v1"


def _main_relay_url(env):
    return (getattr(env, "MAIN_RELAY_URL", "") or "").strip().rstrip("/")


def _is_main_relay(env):
    return not _main_relay_url(env)


async def _new_ed25519_identity():
    # Fresh Ed25519 keypair as (raw pubkey base64url, seed base64url) — the same
    # base64url-raw shape the desktop identity and ed25519_verify use.
    pair = await js_crypto.subtle.generateKey(
        to_js({"name": "Ed25519"}), True, _to_js(["sign", "verify"]))
    pub_raw = await js_crypto.subtle.exportKey("raw", pair.publicKey)
    pub = bytes(Uint8Array.new(pub_raw).to_py())
    jwk = await js_crypto.subtle.exportKey("jwk", pair.privateKey)
    seed = str(getattr(jwk, "d", "") or "")
    if len(pub) != 32 or not seed:
        return None
    return {"pubkey": _b64url_encode(pub), "seed": seed}


async def ed25519_sign(pubkey_b64url, seed_b64url, data_bytes):
    # Sign with a raw-base64url Ed25519 keypair (mirrors _solana_sign_message but
    # keyed by the base64url pubkey rather than a base58 address).
    try:
        jwk = {"kty": "OKP", "crv": "Ed25519", "x": pubkey_b64url,
               "d": seed_b64url, "ext": True, "key_ops": ["sign"]}
        key = await js_crypto.subtle.importKey(
            "jwk", to_js(jwk), to_js({"name": "Ed25519"}), False, _to_js(["sign"]))
        sig = await js_crypto.subtle.sign(
            to_js({"name": "Ed25519"}), key, _to_js(data_bytes))
        return _b64url_encode(bytes(Uint8Array.new(sig).to_py()))
    except Exception:
        return ""


async def _relay_identity(env):
    # This (federated) relay's own signing identity, auto-generated once and kept
    # in D1 so the operator needs no key management — only MAIN_RELAY_URL.
    row = await d1_first(env, "SELECT data FROM relay_self WHERE id=1")
    if row:
        ident = await decrypt_row(env, row.get("data"))
        if ident and ident.get("pubkey") and ident.get("seed"):
            return ident
    ident = await _new_ed25519_identity()
    if not ident:
        return None
    await d1_run(
        env,
        "INSERT INTO relay_self (id, data) VALUES (1, ?) "
        "ON CONFLICT(id) DO UPDATE SET data=excluded.data",
        await encrypt_row(env, ident))
    return ident


def _federation_canonical(pubkey, ts, body_hash):
    return (FEDERATION_CANON + "\n" + pubkey + "\n" + str(ts) + "\n" +
            body_hash).encode()


async def _relay_sign_headers(env, body_str):
    ident = await _relay_identity(env)
    if not ident:
        return None
    ts = str(int(Date.now()))
    body_hash = await sha256_hex(body_str)
    sig = await ed25519_sign(
        ident["pubkey"], ident["seed"],
        _federation_canonical(ident["pubkey"], ts, body_hash))
    if not sig:
        return None
    return {"content-type": "application/json", "X-Relay-Pubkey": ident["pubkey"],
            "X-Relay-Ts": ts, "X-Relay-Sig": sig}


async def _call_main_relay(env, path, body_dict):
    from js import fetch as js_fetch
    base = _main_relay_url(env)
    if not base:
        return None
    body_str = json.dumps(body_dict)
    headers = await _relay_sign_headers(env, body_str)
    if not headers:
        return None
    try:
        resp = await js_fetch(base + path, to_js(
            {"method": "POST", "headers": headers, "body": body_str}))
        text = await resp.text()
        data = json.loads(text) if text else {}
        if not isinstance(data, dict):
            return None
        data["_status"] = int(getattr(resp, "status", 0))
        return data
    except Exception:
        return None


async def _verify_relay_sig(env, request, body_str):
    # Verify a relay->main request's Ed25519 signature (no approval check).
    pubkey = (request.headers.get("x-relay-pubkey") or "").strip()
    ts = (request.headers.get("x-relay-ts") or "").strip()
    sig = (request.headers.get("x-relay-sig") or "").strip()
    if not pubkey or not sig or not _ts_ok(ts):
        return None
    body_hash = await sha256_hex(body_str or "")
    if not await ed25519_verify(
            pubkey, sig, _federation_canonical(pubkey, ts, body_hash)):
        return None
    return pubkey


async def _relay_authorized(env, request, body_str):
    # As _verify_relay_sig, but also require the relay to be approved. Returns the
    # relay's blind index on success, else None.
    pubkey = await _verify_relay_sig(env, request, body_str)
    if not pubkey:
        return None
    relay_bi = await blind_index(env, pubkey)
    row = await d1_first(
        env, "SELECT status FROM relays WHERE relay_bi=?", relay_bi)
    if not row or row.get("status") != "approved":
        return None
    return relay_bi


# --- Main-relay federation endpoints ----------------------------------------

async def _federation_register(env, request):
    if not _is_main_relay(env):
        return json_response({"error": "not_main_relay"}, status=404)
    body = await request.text()
    pubkey = await _verify_relay_sig(env, request, body)
    if not pubkey:
        return json_response({"error": "bad_signature"}, status=401)
    try:
        data = json.loads(body or "{}")
    except Exception:
        data = {}
    relay_bi = await blind_index(env, pubkey)
    label = clean_string(data.get("label", ""), 80)
    base_url = clean_string(data.get("baseUrl", ""), 200)
    now = int(Date.now())
    existing = await d1_first(
        env, "SELECT status FROM relays WHERE relay_bi=?", relay_bi)
    if existing:
        await d1_run(
            env, "UPDATE relays SET pubkey=?, label=?, base_url=? WHERE relay_bi=?",
            pubkey, label, base_url, relay_bi)
        return json_response({"ok": True, "status": existing.get("status", "pending")})
    await d1_run(
        env,
        "INSERT INTO relays (relay_bi, pubkey, label, base_url, status, "
        "registered_at) VALUES (?,?,?,?, 'pending', ?)",
        relay_bi, pubkey, label, base_url, now)
    return json_response({"ok": True, "status": "pending"})


async def _federation_donation_address(env, request):
    if not _is_main_relay(env):
        return json_response({"error": "not_main_relay"}, status=404)
    body = await request.text()
    relay_bi = await _relay_authorized(env, request, body)
    if not relay_bi:
        return json_response({"error": "relay_not_approved"}, status=401)
    try:
        data = json.loads(body or "{}")
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("name", ""), MAX_NODE_NAME).lower()
    try:
        amount = int(data.get("amountLamports", 0) or 0)
    except (TypeError, ValueError):
        amount = 0
    if not _treasury_address(env):
        return json_response({"error": "treasury_not_configured"}, status=503)
    addr, secret = await _new_solana_keypair()
    if not addr:
        return json_response({"error": "keypair_failed"}, status=500)
    now = int(Date.now())
    required = max(amount, await _min_join_lamports(env))
    rec = {
        "relay_bi": relay_bi, "name": name,
        "donation_address": addr, "donation_secret": secret,
        "donation_required_lamports": required, "donation_confirmed": False,
        "donation_received_lamports": 0, "donation_created_at": now,
    }
    reference = bytes(_random_bytes(16)).hex()
    await d1_run(
        env,
        "INSERT INTO federated_signup (reference, relay_bi, data, created_at) "
        "VALUES (?,?,?,?)",
        reference, relay_bi, await encrypt_row(env, rec), now)
    price = await _sol_usd_price(env)
    return json_response({
        "ok": True, "reference": reference, "address": addr,
        "requiredLamports": required, "amountSol": _amount_sol(required),
        "uri": _solana_pay_uri(addr, required), "solUsd": price or 0,
        "amountUsd": round((required / LAMPORTS_PER_SOL) * price, 2) if price else 0,
    })


async def _federation_donation_status(env, request):
    if not _is_main_relay(env):
        return json_response({"error": "not_main_relay"}, status=404)
    body = await request.text()
    relay_bi = await _relay_authorized(env, request, body)
    if not relay_bi:
        return json_response({"error": "relay_not_approved"}, status=401)
    try:
        data = json.loads(body or "{}")
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    reference = clean_string(data.get("reference", ""), 64)
    row = await d1_first(
        env, "SELECT data FROM federated_signup WHERE reference=? AND relay_bi=?",
        reference, relay_bi)
    if not row:
        return json_response({"error": "no_such_signup"}, status=404)
    rec = await decrypt_row(env, row.get("data"))
    if not rec:
        return json_response({"error": "no_such_signup"}, status=404)
    required = int(rec.get("donation_required_lamports", MIN_JOIN_LAMPORTS))
    balance = await _solana_balance_lamports(env, rec.get("donation_address", ""))
    changed = False
    if balance is not None:
        if int(balance) != int(rec.get("donation_received_lamports", 0)):
            rec["donation_received_lamports"] = int(balance)
            changed = True
        if int(balance) >= required and not rec.get("donation_confirmed"):
            rec["donation_confirmed"] = True
            await _sweep_confirmed_donation(env, reference, rec, int(balance))
            changed = True
        elif rec.get("donation_confirmed") and not rec.get("donation_sweep_sig"):
            if await _sweep_confirmed_donation(env, reference, rec, int(balance)):
                changed = True
    if changed:
        await d1_run(
            env, "UPDATE federated_signup SET data=? WHERE reference=?",
            await encrypt_row(env, rec), reference)
    return json_response({
        "ok": True, "paid": bool(rec.get("donation_confirmed")),
        "receivedLamports": int(rec.get("donation_received_lamports", 0)),
        "requiredLamports": required,
        "checking": balance is None,
        "sweepSig": rec.get("donation_sweep_sig", ""),
    })


async def _federation_nodes(env, request):
    if not _is_main_relay(env):
        return json_response({"error": "not_main_relay"}, status=404)
    body = await request.text()
    relay_bi = await _relay_authorized(env, request, body)
    if not relay_bi:
        return json_response({"error": "relay_not_approved"}, status=401)
    try:
        data = json.loads(body or "{}")
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    nodes = data.get("nodes") or []
    now = int(Date.now())
    count = 0
    for node in nodes[:500]:
        if not isinstance(node, dict):
            continue
        wallet = (node.get("wallet") or "").strip()
        if not SOLANA_RE.match(wallet):
            continue
        name = clean_string(node.get("name", ""), MAX_NODE_NAME)
        await d1_run(
            env,
            "INSERT INTO federated_presence (relay_bi, wallet, name, ts) "
            "VALUES (?,?,?,?) ON CONFLICT(relay_bi, wallet) DO UPDATE SET "
            "name=excluded.name, ts=excluded.ts",
            relay_bi, wallet, name, now)
        count += 1
    return json_response({"ok": True, "count": count})


async def federation_handler(env, request):
    await ensure_schema(env)
    url = urlparse(request.url)
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    if url.path == "/api/federation/register":
        return await _federation_register(env, request)
    if url.path == "/api/federation/donation-address":
        return await _federation_donation_address(env, request)
    if url.path == "/api/federation/donation-status":
        return await _federation_donation_status(env, request)
    if url.path == "/api/federation/nodes":
        return await _federation_nodes(env, request)
    if url.path == "/api/federation/treasury-address":
        return await _federation_treasury_address(env, request)
    if url.path == "/api/federation/central-fund":
        return await _federation_central_fund(env, request)
    return json_response({"error": "not_found"}, status=404)


async def _federation_treasury_address(env, request):
    """Main relay: hand a federated relay the treasury address to display."""
    return json_response({"address": _treasury_address(env)})


async def _federation_central_fund(env, request):
    """Main relay: hand a federated relay the central-fund donation payload."""
    if not _is_main_relay(env):
        return json_response({"error": "not_main_relay"}, status=404)
    payload = await _central_fund_public(env)
    if not payload or not payload.get("address"):
        return json_response({"error": "central_fund_unavailable"}, status=503)
    return json_response(payload)


# --- Federated-relay proxy (the "flow through main relay") -------------------

async def _federated_donation_address(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    try:
        amount = int(data.get("amountLamports", 0) or 0)
    except (TypeError, ValueError):
        amount = 0
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "reserve_node_name_first"}, status=404)
    if rec.get("status") == "active":
        return json_response({"error": "already_active"}, status=409)
    reply = await _call_main_relay(
        env, "/api/federation/donation-address",
        {"name": name, "amountLamports": amount})
    if not reply or not reply.get("ok") or not reply.get("address"):
        return json_response({"error": "main_relay_unavailable"}, status=503)
    # Mirror the main relay's deposit address locally so polling works; we hold
    # no key here (no donation_secret) — custody lives on the main relay.
    rec["donation_address"] = reply.get("address", "")
    rec["donation_required_lamports"] = int(reply.get("requiredLamports", 0))
    rec["donation_confirmed"] = False
    rec["donation_received_lamports"] = 0
    rec["fed_reference"] = reply.get("reference", "")
    rec["status"] = "pending_payment"
    rec.pop("donation_secret", None)
    await _save_account(env, name_bi, rec)
    return json_response({
        "ok": True, "address": reply.get("address", ""),
        "reference": reply.get("reference", ""),
        "uri": reply.get("uri", ""),
        "requiredLamports": int(reply.get("requiredLamports", 0)),
        "amountSol": reply.get("amountSol", ""),
        "solUsd": reply.get("solUsd", 0), "amountUsd": reply.get("amountUsd", 0),
    })


async def _federated_donation_status(env, request):
    params = parse_qs(urlparse(request.url).query)
    name = clean_string(params.get("nodeName", [""])[0], MAX_NODE_NAME).lower()
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "no_such_account"}, status=404)
    reference = rec.get("fed_reference", "")
    required = int(rec.get("donation_required_lamports", MIN_JOIN_LAMPORTS))
    if not reference:
        return json_response({"error": "no_donation_address"}, status=400)
    reply = await _call_main_relay(
        env, "/api/federation/donation-status", {"reference": reference})
    if not reply or not reply.get("ok"):
        # Main relay unreachable: soft "still checking" so the user keeps waiting.
        return json_response({
            "ok": True, "paid": bool(rec.get("donation_confirmed")),
            "checking": True,
            "receivedLamports": int(rec.get("donation_received_lamports", 0)),
            "requiredLamports": required})
    received = int(reply.get("receivedLamports", 0))
    paid = bool(reply.get("paid"))
    changed = False
    if received != int(rec.get("donation_received_lamports", 0)):
        rec["donation_received_lamports"] = received
        changed = True
    if paid and not rec.get("donation_confirmed"):
        rec["donation_confirmed"] = True
        changed = True
    if changed:
        await _save_account(env, name_bi, rec)
    return json_response({
        "ok": True, "paid": paid, "checking": bool(reply.get("checking")),
        "receivedLamports": received,
        "requiredLamports": int(reply.get("requiredLamports", required))})


# --- Federation cron --------------------------------------------------------

async def _federation_report_nodes(env):
    # Federated relay: report its currently-online node payout wallets to the main
    # relay so they join the disbursement split. Reuses the presence selection.
    cutoff = int(Date.now()) - ACCOUNT_PRESENCE_STALE_MS
    rows = await d1_all(
        env, "SELECT name_bi FROM account_presence WHERE ts >= ? ORDER BY ts DESC",
        cutoff)
    nodes = []
    seen = set()
    for r in (rows or []):
        row = await d1_first(
            env, "SELECT data FROM accounts WHERE name_bi=?", r["name_bi"])
        if not row:
            continue
        rec = await decrypt_row(env, row["data"])
        if not rec or rec.get("status") != "active":
            continue
        wallet = (rec.get("solana") or "").strip()
        if not wallet or not SOLANA_RE.match(wallet) or wallet in seen:
            continue
        seen.add(wallet)
        nodes.append({"name": rec.get("name", ""), "wallet": wallet})
    if nodes:
        await _call_main_relay(env, "/api/federation/nodes", {"nodes": nodes})


async def _federation_cron(env):
    if not _is_main_relay(env):
        ident = await _relay_identity(env)
        if ident:
            label = clean_string(getattr(env, "RELAY_LABEL", "") or "", 80)
            base = clean_string(getattr(env, "PUBLIC_BASE_URL", "") or "", 200)
            await _call_main_relay(
                env, "/api/federation/register",
                {"label": label, "baseUrl": base})
            await _federation_report_nodes(env)
        return
    # Main relay: expire stale federated presence, then sweep confirmed-but-
    # unswept federated signups (mirrors _admin_disburse for local accounts).
    cutoff = int(Date.now()) - ACCOUNT_PRESENCE_STALE_MS
    try:
        await d1_run(env, "DELETE FROM federated_presence WHERE ts < ?", cutoff)
    except Exception:
        pass
    rows = await d1_all(
        env, "SELECT reference, data FROM federated_signup")
    for row in (rows or []):
        rec = await decrypt_row(env, row.get("data"))
        if not rec or not rec.get("donation_confirmed"):
            continue
        if rec.get("donation_sweep_sig") or not rec.get("donation_secret"):
            continue
        bal = await _solana_balance_lamports(env, rec.get("donation_address", ""))
        if not bal or bal <= SOLANA_SWEEP_FEE_RESERVE_LAMPORTS:
            continue
        if await _sweep_confirmed_donation(env, row.get("reference"), rec, bal):
            await d1_run(
                env, "UPDATE federated_signup SET data=? WHERE reference=?",
                await encrypt_row(env, rec), row.get("reference"))


# --- Admin: relay allowlist -------------------------------------------------

async def _admin_relays(env, request):
    params = parse_qs(urlparse(request.url).query)
    node = clean_string(params.get("node", [""])[0], MAX_NODE_NAME).lower()
    ts = clean_string(params.get("ts", [""])[0], 20)
    sig = clean_string(params.get("sig", [""])[0], 200)
    canonical = ("forkmesh-admin-relays-v1\n" + node + "\n" + ts).encode()
    if not await _admin_authorized(env, node, ts, sig, canonical):
        return json_response({"error": "unauthorized"}, status=401)
    rows = await d1_all(
        env, "SELECT pubkey, label, base_url, status, registered_at, approved_at "
        "FROM relays ORDER BY registered_at DESC")
    relays = [{
        "pubkey": r.get("pubkey", ""), "label": r.get("label", ""),
        "baseUrl": r.get("base_url", ""), "status": r.get("status", ""),
        "registeredAt": r.get("registered_at", 0),
        "approvedAt": r.get("approved_at", 0)} for r in (rows or [])]
    return json_response({"ok": True, "relays": relays})


async def _admin_relay_approve(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    node = clean_string(data.get("node", ""), MAX_NODE_NAME).lower()
    ts = clean_string(data.get("ts", ""), 20)
    sig = clean_string(data.get("sig", ""), 200)
    pubkey = clean_string(data.get("pubkey", ""), 120)
    action = clean_string(data.get("action", ""), 20)
    if action not in ("approve", "block"):
        return json_response({"error": "bad_action"}, status=400)
    canonical = ("forkmesh-admin-relay-approve-v1\n" + node + "\n" + pubkey +
                 "\n" + action + "\n" + ts).encode()
    if not await _admin_authorized(env, node, ts, sig, canonical):
        return json_response({"error": "unauthorized"}, status=401)
    relay_bi = await blind_index(env, pubkey)
    status = "approved" if action == "approve" else "blocked"
    now = int(Date.now())
    res = await d1_run(
        env, "UPDATE relays SET status=?, approved_at=? WHERE relay_bi=?",
        status, now if action == "approve" else None, relay_bi)
    return json_response({"ok": True, "status": status})


# --- Admin: node ownership takeover (adhoc #141) -----------------------------
# An admin can request to take ownership of any node. The request only parks a
# pending marker on the target node's own record; it does NOT transfer
# ownership by itself. The marker rides back to that node on its own signed
# heartbeat (same channel as a website claim code), the desktop app pops a
# confirm/deny prompt, and only a signature from the node's own key — i.e. the
# node the current owner is actually logged into — can approve or deny it.

async def _park_ownership_transfer(env, target, new_owner):
    # Shared core of every ownership-takeover trigger (signed node request or
    # the operator console): validate the target is a claimable node and park
    # the pending marker. Returns (ok, message_or_result_dict).
    if not valid_node_name(target):
        return False, "invalid_node_id"
    if target == new_owner:
        return False, "cannot_request_self"
    target_bi, target_rec = await _account_row(env, target)
    if not target_rec or target_rec.get("status") != "active":
        return False, "no_such_node"
    if _account_kind(target_rec) != "node":
        # An account that can log in is a user in its own right, not takeable.
        return False, "not_a_node"
    if target_rec.get("owner") == new_owner:
        return True, {"alreadyOwned": True, "nodeId": target}
    now = int(Date.now())
    target_rec["ownership_transfer_pending"] = {
        "admin": new_owner,
        "requestedAt": now,
        "expires": now + OWNERSHIP_TRANSFER_TTL_MS,
    }
    await _save_account(env, target_bi, target_rec)
    return True, {"pending": True, "nodeId": target,
                 "expiresAt": now + OWNERSHIP_TRANSFER_TTL_MS}


async def _admin_request_ownership(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    node = clean_string(data.get("node", ""), MAX_NODE_NAME).lower()
    target = clean_string(data.get("target", ""), MAX_NODE_NAME).lower()
    ts = clean_string(data.get("ts", ""), 20)
    sig = clean_string(data.get("sig", ""), 200)
    canonical = ("forkmesh-admin-request-ownership-v1\n" + node + "\n" +
                 target + "\n" + ts).encode()
    admin_rec = await _admin_authorized(env, node, ts, sig, canonical)
    if not admin_rec:
        return json_response({"error": "unauthorized"}, status=401)
    admin_name = admin_rec.get("name", node)
    ok, result = await _park_ownership_transfer(env, target, admin_name)
    if not ok:
        status = 404 if result == "no_such_node" else (
            403 if result == "not_a_node" else 400)
        return json_response({"error": result}, status=status)
    return json_response(dict({"ok": True}, **result),
                         status=201 if result.get("pending") else 200)


async def _account_ownership_transfer_confirm(env, request):
    # Called from the target node's own desktop app in response to the
    # heartbeat-delivered prompt, signed with the node's own key — the same
    # proof-of-control the node uses for its heartbeat, not the admin's.
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    node_name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    action = clean_string(data.get("action", ""), 20)
    if action not in ("approve", "deny"):
        return json_response({"error": "bad_action"}, status=400)
    if not valid_node_name(node_name):
        return json_response({"error": "invalid_node_id"}, status=400)
    node_bi, node_rec = await _account_row(env, node_name)
    if not node_rec or node_rec.get("status") != "active":
        return json_response({"error": "no_such_node"}, status=404)
    pubkey = node_rec.get("pubkey", "")
    if not pubkey or not _ts_ok(ts):
        return json_response({"error": "unauthorized"}, status=401)
    canonical = ("forkmesh-ownership-transfer-confirm-v1\n" + node_name +
                 "\n" + action + "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)
    pending = _transfer_pending(node_rec, int(Date.now()))
    if not pending:
        return json_response({"error": "no_pending_transfer"}, status=404)
    if action == "deny":
        node_rec.pop("ownership_transfer_pending", None)
        await _save_account(env, node_bi, node_rec)
        return json_response({"ok": True, "denied": True, "nodeId": node_name})
    admin_name = pending.get("admin", "")
    node_rec.pop("ownership_transfer_pending", None)
    await _link_node_to_user(env, node_name, node_bi, node_rec, admin_name)
    return json_response({"ok": True, "linked": True, "nodeId": node_name,
                          "owner": admin_name})


async def _account_treasury_address(env, request):
    """Public: the ForkMesh treasury Solana address for in-app donations.

    Lets clients render a "donate to the treasury" QR without embedding the
    address. On a federated (non-main) relay the treasury lives on the main
    relay, so proxy the lookup there.
    """
    addr = _treasury_address(env)
    if not addr and not _is_main_relay(env):
        reply = await _call_main_relay(env, "/api/federation/treasury-address", {})
        if reply:
            addr = (reply.get("address") or "")
    if not addr:
        return json_response({"address": ""}, status=503)
    return json_response({"address": addr})


async def _account_central_fund(env, request):
    """Public: the ForkMesh central donation fund (issue #308).

    Returns the one wallet anyone can donate to; a cron distributes its balance
    evenly to the online nodes hourly. On a federated (non-main) relay the fund
    lives on the main relay, so proxy the lookup there (like the treasury).
    """
    if not _is_main_relay(env):
        reply = await _call_main_relay(env, "/api/federation/central-fund", {})
        if reply and reply.get("address"):
            reply.pop("_status", None)
            return json_response(reply)
        return json_response({"address": ""}, status=503)
    payload = await _central_fund_public(env)
    if not payload or not payload.get("address"):
        return json_response({"address": ""}, status=503)
    return json_response(payload)


async def accounts_handler(env, request):
    await ensure_schema(env)
    url = urlparse(request.url)
    method = method_name(request)
    if url.path == "/api/accounts/signup" and method == "POST":
        return await _account_signup(env, request)
    if url.path == "/api/accounts/reserve" and method == "POST":
        return await _account_reserve(env, request)
    if url.path == "/api/accounts/profile" and method == "POST":
        return await _account_profile(env, request)
    if url.path == "/api/accounts/donation-address" and method == "POST":
        return await _account_donation_address(env, request)
    if url.path == "/api/accounts/donation-status" and method == "GET":
        return await _account_donation_status(env, request)
    if url.path == "/api/accounts/treasury-address" and method == "GET":
        return await _account_treasury_address(env, request)
    if url.path == "/api/accounts/central-fund" and method == "GET":
        return await _account_central_fund(env, request)
    if url.path == "/api/accounts/finalize" and method == "POST":
        return await _account_finalize(env, request)
    if url.path == "/api/accounts/heartbeat" and method == "POST":
        return await _account_heartbeat(env, request)
    if url.path == "/api/accounts/verify-email" and method == "GET":
        return await _verify_email(env, request)
    if url.path == "/api/accounts/admin-pending" and method == "GET":
        return await _admin_pending(env, request)
    if url.path == "/api/accounts/admin-verify-email" and method == "POST":
        return await _admin_verify_email(env, request)
    if url.path == "/api/accounts/admin-relays" and method == "GET":
        return await _admin_relays(env, request)
    if url.path == "/api/accounts/admin-relay-approve" and method == "POST":
        return await _admin_relay_approve(env, request)
    if url.path == "/api/accounts/admin-request-ownership" and method == "POST":
        return await _admin_request_ownership(env, request)
    if url.path == "/api/accounts/ownership-transfer-confirm" and method == "POST":
        return await _account_ownership_transfer_confirm(env, request)
    if url.path == "/api/accounts/login" and method == "POST":
        return await _account_login(env, request)
    if url.path == "/api/accounts/forgot-password" and method == "POST":
        return await _account_forgot_password(env, request)
    if url.path == "/api/accounts/reset-password" and method == "POST":
        return await _account_reset_password(env, request)
    if url.path == "/api/accounts/claim-node" and method == "POST":
        return await _account_claim_node(env, request)
    if url.path == "/api/accounts/claim-confirm" and method == "POST":
        return await _account_claim_confirm(env, request)
    if url.path == "/api/accounts/link-node" and method == "POST":
        return await _account_link_node(env, request)
    if url.path == "/api/accounts/link-self" and method == "POST":
        return await _account_link_self(env, request)
    if url.path == "/api/accounts/link-grant" and method == "POST":
        return await _account_link_grant(env, request)
    match = ACCOUNTS_RE.match(url.path)
    if match and method == "GET":
        name = clean_string(match.group(1), MAX_NODE_NAME).lower()
        _, rec = await _account_row(env, name)
        if not rec:
            return json_response(
                {"ok": True, "exists": False, "available": True, "name": name})
        # Public lookup never returns email/secret material. A name is only
        # unavailable once it has been paid for / finalized.
        taken = rec.get("status") == "active" or bool(rec.get("donation_confirmed"))
        # isAdmin + pubkey let any client authenticate a signed admin-moderation
        # action (e.g. a chat admin-delete) made by this account's identity key.
        # kind/owner/nodes let the dashboard's claim form tell "that's a user
        # account, not a claimable node" (and "already owned") before starting a
        # claim, and let a node's own profile list the nodes linked to its user
        # account; node ownership is public catalog-adjacent data like the name
        # itself (same fields _account_public_payload already exposes).
        # emailVerified is a boolean only (no address) so the dashboard's
        # periodic self-profile poll (refreshPublicProfile, which reuses this
        # same lookup) can pick up a verification that happened in another
        # tab instead of showing "verify your email" forever (issue #320).
        return json_response(
            {"ok": True, "exists": True, "available": not taken,
             "name": rec.get("name", name), "status": rec.get("status", ""),
             "pubkey": rec.get("pubkey", ""),
             "isAdmin": await _is_admin(env, rec.get("name", name)),
             "emailVerified": bool(rec.get("email_verified")),
             "avatarPng": rec.get("avatar_png", ""),
             "avatarUpdatedAt": rec.get("avatar_updated_at", 0),
             "createdAt": rec.get("created_at", 0),
             "kind": _account_kind(rec),
             "owner": rec.get("owner", ""),
             "nodes": _owned_nodes(rec)}
        )
    return json_response({"error": "not_found"}, status=404)


# --- Repository submission inboxes (encrypted) ------------------------------

async def _authorize_owner(env, request, owner):
    if not owner:
        return False
    params = parse_qs(urlparse(request.url).query)
    ts = params.get("ts", [""])[0]
    sig = params.get("sig", [""])[0]
    owner_pub = await _owner_pubkey(env, owner)
    if not owner_pub or not ts or not sig:
        return False
    try:
        skew = abs(int(Date.now()) - int(ts))
    except (TypeError, ValueError):
        return False
    if skew > LOGIN_MAX_SKEW_MS:
        return False
    canonical = ("forkmesh-issues-pull-v1\n" + owner + "\n" + ts).encode()
    return await ed25519_verify(owner_pub, sig, canonical)


async def _authorize_owner_account(env, owner, data):
    # Checks if the named account is the repo owner or a network admin, without
    # requiring password verification. Used for agents tab on website (adhoc #225).
    # Returns (True, None) on success, or (False, error_json_response) on failure.
    actor = clean_string(data.get("ownerAccount", "") or owner, 120)
    if not actor:
        return False, json_response({"error": "not_authorized"}, status=403)
    if actor.lower() != str(owner or "").lower() \
            and not await _is_admin(env, actor):
        return False, json_response({"error": "not_authorized"}, status=403)
    return True, None


async def _inbox_author_over_quota(env, table, repo_bi, submitter_bi):
    # True when this submitter already holds MAX_PENDING_PER_AUTHOR un-merged rows
    # in this repo's inbox (table is a fixed literal, safe to interpolate).
    if not submitter_bi:
        return False
    row = await d1_first(
        env,
        "SELECT COUNT(*) AS c FROM " + table +
        " WHERE repo_bi=? AND submitter_bi=?",
        repo_bi, submitter_bi,
    )
    return bool(row and (row.get("c", 0) or 0) >= MAX_PENDING_PER_AUTHOR)


async def enqueue_notification(env, recipient, kind, title, body="", repo="",
                               href="", actor="", source="", dedupe="",
                               ts=0, meta=None):
    # Account-scoped notification index. It intentionally stores only an encrypted
    # payload plus blind indexes/read state; the canonical event stays in repo git
    # files or a signed inbox table.
    await ensure_schema(env)
    recipient = clean_string(recipient, MAX_NODE_NAME).lower()
    actor = clean_string(actor, MAX_NODE_NAME).lower()
    if not valid_node_name(recipient) or kind not in NOTIFICATION_KINDS:
        return False
    if actor and actor == recipient:
        return False
    now = int(ts or Date.now())
    payload = notification_payload(
        kind, title, body=body, repo=repo, href=href, actor=actor,
        source=source, ts=now, meta=meta)
    recipient_bi = await blind_index(env, recipient)
    dedupe_value = dedupe or (kind + ":" + recipient + ":" + source + ":" +
                              repo + ":" + str(now))
    dedupe_bi = await blind_index(env, "notification:" + recipient + ":" + dedupe_value)
    await d1_run(
        env,
        """INSERT INTO notifications (dedupe_bi, recipient_bi, ts, read_at, data)
           VALUES (?,?,?,?,?)
           ON CONFLICT(dedupe_bi) DO UPDATE SET
             recipient_bi=excluded.recipient_bi,
             ts=excluded.ts,
             data=excluded.data""",
        dedupe_bi, recipient_bi, now, 0, await encrypt_row(env, payload),
    )
    cutoff = now - NOTIFICATION_RETAIN_MS
    await d1_run(
        env,
        "DELETE FROM notifications WHERE recipient_bi=? AND ts < ?",
        recipient_bi, cutoff,
    )
    await d1_run(
        env,
        """DELETE FROM notifications
           WHERE recipient_bi=? AND dedupe_bi NOT IN (
             SELECT dedupe_bi FROM notifications
             WHERE recipient_bi=? ORDER BY ts DESC LIMIT ?
           )""",
        recipient_bi, recipient_bi, MAX_NOTIFICATIONS_PER_RECIPIENT,
    )
    return True


async def notify_mentions(env, owner, repo, actor, title, body, href, source):
    for name in notification_mentions(title, body):
        await enqueue_notification(
            env, name, "mention", "You were mentioned in " + owner + "/" + repo,
            body=(title or body or "Open the repository item to read the mention."),
            repo=owner + "/" + repo, href=href, actor=actor, source=source,
            dedupe="mention:" + owner + "/" + repo + ":" + source + ":" + name +
                   ":" + clean_string(actor, 80) + ":" + clean_string(title, 80) +
                   ":" + clean_string(body, 80),
        )


def _thread_key(owner, repo, source, number):
    try:
        n = int(number or 0)
    except (TypeError, ValueError):
        n = 0
    return ("thread:" + (owner or "") + "/" + (repo or "") + ":" +
            (source or "") + ":" + str(n))


async def subscribe_thread(env, owner, repo, source, number, subscriber,
                           muted=False):
    # Record (or mute) one node's subscription to an issue/PR thread. Auto-called
    # for anyone who comments so they hear about later replies; the signed
    # subscribe endpoint calls it too. An explicit mute is sticky: a later
    # auto-subscribe won't silently re-enable a thread the user muted.
    subscriber = clean_string(subscriber, MAX_NODE_NAME).lower()
    if not valid_node_name(subscriber):
        return False
    try:
        number = int(number or 0)
    except (TypeError, ValueError):
        number = 0
    if number <= 0:
        # A brand-new submission has no durable number yet (the owner assigns it on
        # drain), so its thread key would collide with every other new item.
        return False
    await ensure_schema(env)
    thread_bi = await blind_index(env, _thread_key(owner, repo, source, number))
    subscriber_bi = await blind_index(env, subscriber)
    if not muted:
        existing = await d1_first(
            env,
            "SELECT data FROM thread_subscriptions "
            "WHERE thread_bi=? AND subscriber_bi=?",
            thread_bi, subscriber_bi)
        if existing:
            rec = await decrypt_row(env, existing.get("data", ""))
            if rec and rec.get("muted"):
                return False  # keep an explicit unsubscribe
            return True  # already subscribed
    await d1_run(
        env,
        "INSERT INTO thread_subscriptions (thread_bi, subscriber_bi, ts, data) "
        "VALUES (?,?,?,?) ON CONFLICT(thread_bi, subscriber_bi) DO UPDATE SET "
        "ts=excluded.ts, data=excluded.data",
        thread_bi, subscriber_bi, int(Date.now()),
        await encrypt_row(env, {"node": subscriber, "muted": bool(muted)}),
    )
    return True


async def notify_subscribers(env, owner, repo, source, number, actor, title,
                             body, href):
    # Fan a new comment out to everyone following the thread, minus the commenter
    # and anyone already covered by an @mention on this same event (so a mentioned
    # subscriber gets one notification, not two).
    try:
        number = int(number or 0)
    except (TypeError, ValueError):
        number = 0
    if number <= 0:
        return
    await ensure_schema(env)
    thread_bi = await blind_index(env, _thread_key(owner, repo, source, number))
    rows = await d1_all(
        env, "SELECT data FROM thread_subscriptions WHERE thread_bi=?", thread_bi)
    if not rows:
        return
    mentioned = set(notification_mentions(title, body))
    snippet = clean_string(body or title, 80)
    for row in rows:
        rec = await decrypt_row(env, row.get("data", ""))
        if not rec or rec.get("muted"):
            continue
        name = clean_string(rec.get("node", ""), MAX_NODE_NAME).lower()
        if not valid_node_name(name) or name == actor or name in mentioned:
            continue
        await enqueue_notification(
            env, name, "subscribed", "New activity in " + owner + "/" + repo,
            body=(title or body or "There's a new reply on a thread you follow."),
            repo=owner + "/" + repo, href=href, actor=actor, source=source,
            dedupe="subscribed:" + owner + "/" + repo + ":" + source + ":" +
                   str(number) + ":" + name + ":" + clean_string(actor, 80) +
                   ":" + snippet,
            meta={"number": number, "source": source},
        )


async def notify_pending_inbox(env, owner, repo, source, actor, title, number=0):
    kind = "pull_submitted" if source == "pull" else "pending_inbox"
    label = {
        "issue": "Issue submitted",
        "pull": "Pull request submitted",
        "commit_comment": "Commit comment submitted",
        "discussion": "Discussion submitted",
    }.get(source, "Pending inbox item")
    await enqueue_notification(
        env, owner, kind, label + " for " + owner + "/" + repo,
        body=title or "A signed item is waiting in your desktop inbox.",
        repo=owner + "/" + repo, href=repo_web_href(owner, repo),
        actor=actor, source=source,
        dedupe="pending:" + owner + "/" + repo + ":" + source + ":" +
               clean_string(actor, 120) + ":" + str(number) + ":" + clean_string(title, 120),
        meta={"number": number, "source": source},
    )


async def notify_issue_assignees(env, owner, repo, assignees, actor, title, number=0):
    for assignee in sorted({clean_string(a, MAX_NODE_NAME).lower() for a in (assignees or [])}):
        if not valid_node_name(assignee):
            continue
        await enqueue_notification(
            env, assignee, "issue_assigned",
            "Issue assigned in " + owner + "/" + repo,
            body=title or "You were assigned to an issue.",
            repo=owner + "/" + repo,
            href=repo_web_href(owner, repo),
            actor=actor, source="issue_assigned",
            dedupe="issue-assigned:" + owner + "/" + repo + ":" + str(number) +
                   ":" + assignee,
            meta={"number": number},
        )


async def notify_bounty_event(env, rec, kind):
    owner = clean_string(rec.get("owner", ""), MAX_NODE_NAME).lower()
    repo = clean_string(rec.get("repo", ""), MAX_REPO_SEGMENT)
    if not owner or not repo:
        return
    number = int(rec.get("number", 0) or 0)
    title = "Bounty funded" if kind == "bounty_funded" else "Bounty paid"
    await enqueue_notification(
        env, owner, kind, title + " on " + owner + "/" + repo,
        body=("Issue #" + str(number) + " bounty is " +
              ("funded." if kind == "bounty_funded" else "paid.")),
        repo=owner + "/" + repo,
        href=repo_web_href(owner, repo),
        source="bounty", dedupe=kind + ":" + owner + "/" + repo + ":" + str(number),
        meta={"number": number, "status": rec.get("status", "")},
    )


async def notify_release_published(env, owner, repo, release):
    tag = clean_string((release or {}).get("tag", ""), 128)
    actor = clean_string((release or {}).get("created_by", ""), MAX_NODE_NAME).lower()
    await enqueue_notification(
        env, owner, "release_published",
        "Release " + (tag or "published") + " published",
        body=(release or {}).get("name", "") or ("Release " + tag + " is live."),
        repo=owner + "/" + repo,
        href=repo_web_href(owner, repo),
        actor=actor, source="release",
        dedupe="release:" + owner + "/" + repo + ":" + tag,
        ts=int((release or {}).get("published_at", 0) or 0),
        meta={"tag": tag},
    )


async def _repo_notification_target(env, repo_bi):
    row = await d1_first(env, "SELECT data FROM repositories WHERE key_bi=?", repo_bi)
    if not row:
        return "", ""
    rec = await decrypt_row(env, row.get("data", ""))
    if not rec:
        return "", ""
    return (clean_string(rec.get("owner", ""), MAX_NODE_NAME).lower(),
            clean_string(rec.get("name", ""), MAX_REPO_SEGMENT))


async def notify_host_status(env, repo_bi, kind):
    owner, repo = await _repo_notification_target(env, repo_bi)
    if not owner or not repo:
        return
    title = "Host online" if kind == "host_online" else "Host offline"
    await enqueue_notification(
        env, owner, kind, title + " for " + owner + "/" + repo,
        body=("A desktop host is reachable." if kind == "host_online"
              else "No live host has checked in recently."),
        repo=owner + "/" + repo,
        href=repo_web_href(owner, repo),
        source="host", dedupe=kind + ":" + repo_bi,
    )


async def notify_stale_hosts_offline(env, cutoff):
    rows = await d1_all(env, "SELECT repo_bi FROM host_presence WHERE ts < ?", cutoff)
    for row in rows:
        repo_bi = row.get("repo_bi")
        if repo_bi:
            await notify_host_status(env, repo_bi, "host_offline")


async def notifications_handler(env, request):
    await ensure_schema(env)
    method = method_name(request)
    if method == "GET":
        params = parse_qs(urlparse(request.url).query)
        node = clean_string(params.get("node", [""])[0], MAX_NODE_NAME).lower()
        if not valid_node_name(node):
            return json_response({"error": "node_required"}, status=400)
        try:
            limit = int(params.get("limit", ["40"])[0])
        except (TypeError, ValueError):
            limit = 40
        limit = max(1, min(limit, MAX_NOTIFICATIONS_FETCH))
        recipient_bi = await blind_index(env, node)
        rows = await d1_all(
            env,
            """SELECT dedupe_bi, ts, read_at, data FROM notifications
               WHERE recipient_bi=? ORDER BY ts DESC LIMIT ?""",
            recipient_bi, limit,
        )
        items = []
        unread = 0
        # Derive the AES key once for the whole page instead of per row: each
        # derivation is its own async WebCrypto round trip, and this endpoint is
        # polled often enough that N sequential round trips (one per notification)
        # measurably drags out the request.
        row_key = await _data_key(env) if rows else None
        for row in rows:
            rec = await decrypt_row(env, row.get("data", ""), key=row_key)
            if not rec:
                continue
            read_at = int(row.get("read_at") or 0)
            rec["id"] = row.get("dedupe_bi", "")
            rec["readAt"] = read_at
            rec["ts"] = int(row.get("ts") or rec.get("ts") or 0)
            if not read_at:
                unread += 1
            items.append(rec)
        return json_response({"ok": True, "notifications": items, "unread": unread})

    if method == "POST":
        try:
            data = await request.json()
        except Exception:
            return json_response({"error": "invalid_json"}, status=400)
        node = clean_string(data.get("node", ""), MAX_NODE_NAME).lower()
        if not valid_node_name(node):
            return json_response({"error": "node_required"}, status=400)
        recipient_bi = await blind_index(env, node)
        now = int(Date.now())
        if data.get("all"):
            await d1_run(
                env, "UPDATE notifications SET read_at=? WHERE recipient_bi=?",
                now, recipient_bi)
            return json_response({"ok": True})
        ids = data.get("ids") if isinstance(data.get("ids"), list) else []
        for item_id in ids[:MAX_NOTIFICATIONS_FETCH]:
            item_id = clean_string(item_id, 160)
            if item_id:
                await d1_run(
                    env,
                    "UPDATE notifications SET read_at=? WHERE recipient_bi=? AND dedupe_bi=?",
                    now, recipient_bi, item_id)
        return json_response({"ok": True})

    return json_response({"error": "method_not_allowed"}, status=405)


async def subscribe_handler(env, request, owner, repo):
    # Signed subscribe/unsubscribe for one issue or PR thread (issue #361). The
    # request is signed with the node's own Ed25519 key — the same proof of
    # control as a heartbeat — so only the account holder can watch or mute a
    # thread on their own behalf.
    await ensure_schema(env)
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    node = clean_string(data.get("node", ""), MAX_NODE_NAME).lower()
    source = clean_string(data.get("source", ""), 20)
    if source not in ("issue", "pull"):
        return json_response({"error": "bad_source"}, status=400)
    try:
        number = int(data.get("number", 0))
    except (TypeError, ValueError):
        number = 0
    if number <= 0:
        return json_response({"error": "bad_number"}, status=400)
    subscribed = bool(data.get("subscribed", True))
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    if not valid_node_name(node) or not _ts_ok(ts):
        return json_response({"error": "unauthorized"}, status=401)
    _, rec = await _account_row(env, node)
    pubkey = rec.get("pubkey", "") if rec else ""
    if not pubkey:
        return json_response({"error": "unauthorized"}, status=401)
    canonical = ("forkmesh-subscribe-v1\n" + owner + "/" + repo + "\n" + source +
                 "\n" + str(number) + "\n" + ("1" if subscribed else "0") +
                 "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)
    await subscribe_thread(env, owner, repo, source, number, node,
                           muted=not subscribed)
    return json_response({"ok": True, "subscribed": subscribed})


async def send_notification_digests(env):
    # Cron: roll each recipient's unread notifications into a single email so a
    # reply reaches people who don't have the app open (issue #361). Only the main
    # relay sends mail; recipients need a verified email and must not have opted
    # out. Best-effort — never raise from the cron.
    if not _is_main_relay(env):
        return
    await ensure_schema(env)
    now = int(Date.now())
    max_age_cut = now - NOTIFICATION_DIGEST_MIN_AGE_MS
    recips = await d1_all(
        env,
        "SELECT DISTINCT recipient_bi FROM notifications "
        "WHERE read_at=0 AND ts<=? LIMIT ?",
        max_age_cut, NOTIFICATION_DIGEST_MAX_RECIPIENTS)
    for recip in recips or []:
        recipient_bi = recip.get("recipient_bi")
        if not recipient_bi:
            continue
        acct = await d1_first(
            env, "SELECT data FROM accounts WHERE name_bi=?", recipient_bi)
        if not acct:
            continue
        rec = await decrypt_row(env, acct.get("data", ""))
        if not rec or rec.get("status") != "active":
            continue
        # Use the node owner's email if the node has an owner; otherwise use the node's email.
        email_rec = rec
        owner = clean_string(rec.get("owner", ""), MAX_NODE_NAME).lower()
        if owner:
            _, owner_rec = await _account_row(env, owner)
            if owner_rec and owner_rec.get("status") == "active":
                email_rec = owner_rec
        email = clean_string(email_rec.get("email", ""), 254).strip()
        if not email or not email_rec.get("email_verified"):
            continue
        if email_rec.get("email_notifications") is False:
            continue  # explicit opt-out
        last = int(rec.get("last_digest_ts", 0) or 0)
        if last and now - last < NOTIFICATION_DIGEST_INTERVAL_MS:
            continue
        rows = await d1_all(
            env,
            "SELECT ts, data FROM notifications "
            "WHERE recipient_bi=? AND read_at=0 AND ts>? AND ts<=? "
            "ORDER BY ts DESC LIMIT ?",
            recipient_bi, last, max_age_cut, NOTIFICATION_DIGEST_MAX_ITEMS)
        items = []
        newest = last
        for row in rows or []:
            payload = await decrypt_row(env, row.get("data", ""))
            if not payload:
                continue
            items.append(payload)
            newest = max(newest, int(row.get("ts") or 0))
        if not items:
            continue
        node = clean_string(rec.get("name", ""), MAX_NODE_NAME).lower()
        subject, text, html = _notification_digest_email(node, items)
        if await _send_email(env, email, subject, text, html):
            rec["last_digest_ts"] = newest
            await _save_account(env, recipient_bi, rec)


def _notification_digest_email(node, items):
    n = len(items)
    subject = ("ForkMesh: " + str(n) + " new notification" +
               ("s" if n != 1 else ""))
    lines = ["Hi " + node + ",", "",
             "You have " + str(n) + " unread ForkMesh notification" +
             ("s" if n != 1 else "") + ":", ""]
    html_items = []
    for it in items:
        title = clean_string(it.get("title", ""), 160) or "Notification"
        body = clean_string(it.get("body", ""), 300)
        repo = clean_string(it.get("repo", ""), 180)
        prefix = ("[" + repo + "] ") if repo else ""
        lines.append("- " + prefix + title)
        if body:
            lines.append("    " + body)
        html_items.append(
            "<li><strong>" + _html_escape(prefix + title) + "</strong>" +
            ("<br><span style=\"color:#666\">" + _html_escape(body) + "</span>"
             if body else "") + "</li>")
    lines += ["", "Open ForkMesh to read and reply.",
              "To stop these emails, turn off email notifications in your "
              "profile settings."]
    text = "\n".join(lines)
    html = ("<p>Hi " + _html_escape(node) + ",</p>"
            "<p>You have " + str(n) + " unread ForkMesh notification" +
            ("s" if n != 1 else "") + ":</p><ul>" + "".join(html_items) +
            "</ul><p>Open ForkMesh to read and reply.</p>"
            "<p style=\"color:#888;font-size:13px\">To stop these emails, turn "
            "off email notifications in your profile settings.</p>")
    return subject, text, html


def _html_escape(text):
    return (str(text or "").replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


def _build_rev(env):
    # The git rev deploy.sh stamps as a Worker var on every production deploy.
    try:
        val = env.BUILD_REV
        if val:
            return str(val)
    except (AttributeError, TypeError):
        pass
    return "dev"


async def poll_handler(env, request):
    # One lightweight digest the client polls on its regular tick INSTEAD of
    # separately re-fetching the full profile (/api/accounts/<name> — avatar and
    # all) and the full notification list every time. It returns only cheap
    # change tokens (a couple of indexed reads, no avatar blob, no per-row
    # notification decrypt); the client fires those heavier fetches only when a
    # token here actually moves. Rolling three per-minute polls into one keeps
    # the hot dashboard path off the Worker CPU limit.
    await ensure_schema(env)
    params = parse_qs(urlparse(request.url).query)
    node = clean_string(params.get("node", [""])[0], MAX_NODE_NAME).lower()
    out = {"ok": True, "rev": _build_rev(env)}
    if not valid_node_name(node):
        return json_response(out)
    name_bi, rec = await _account_row(env, node)
    if rec:
        # Token folds the profile fields the dashboard actually re-renders, so a
        # change to any of them (verification, avatar, admin grant, ownership)
        # invalidates it and triggers the full /api/accounts/<name> fetch.
        out["profile"] = {"token": ":".join(str(x) for x in (
            rec.get("status", ""),
            1 if rec.get("email_verified") else 0,
            rec.get("avatar_updated_at", 0) or 0,
            1 if await _is_admin(env, node) else 0,
            rec.get("owner", "") or "",
        ))}
        # Unread count is returned outright so the bell badge updates from the
        # poll alone; token (latest ts + row total) moves on any add/remove so
        # the full list is re-pulled only when it changed.
        notif = await d1_first(
            env,
            "SELECT COUNT(*) AS total, "
            "SUM(CASE WHEN read_at=0 THEN 1 ELSE 0 END) AS unread, "
            "MAX(ts) AS latest FROM notifications WHERE recipient_bi=?",
            name_bi)
        total = int((notif or {}).get("total") or 0)
        unread = int((notif or {}).get("unread") or 0)
        latest = int((notif or {}).get("latest") or 0)
        out["notif"] = {"unread": unread, "token": str(latest) + ":" + str(total)}
    return json_response(out)


async def issues_handler(env, request, owner, repo):
    await ensure_schema(env)
    method = method_name(request)
    repo_bi = await blind_index(env, owner + "/" + repo)
    if method == "POST":
        try:
            data = await request.json()
        except Exception:
            return json_response({"error": "invalid_json"}, status=400)
        event = data.get("event")
        if not isinstance(event, dict):
            return json_response({"error": "event_required"}, status=400)
        try:
            number = int(data.get("number", 0))
        except (TypeError, ValueError):
            number = 0
        if len((event.get("body", "") or "").encode("utf-8")) > MAX_ISSUE_BYTES:
            return json_response({"error": "issue_too_large"}, status=413)
        if not await verify_issue_event(number, event):
            return json_response({"error": "bad_signature"}, status=401)
        count = await d1_first(
            env, "SELECT COUNT(*) AS c FROM issue_inbox WHERE repo_bi=?", repo_bi
        )
        if count and count.get("c", 0) >= MAX_PENDING_ISSUES:
            return json_response({"error": "inbox_full"}, status=429)
        submitter_bi = await blind_index(env, event.get("author", ""))
        if await _inbox_author_over_quota(env, "issue_inbox", repo_bi, submitter_bi):
            return json_response({"error": "author_quota"}, status=429)
        # Issue-level metadata for a new issue (labels/milestone/priority/
        # assignees). Not signature-bound; the owner applies it on merge. Bounded
        # to keep a submission small and the lists sane.
        meta_in = data.get("meta")
        meta = {}
        if isinstance(meta_in, dict):
            labels = meta_in.get("labels")
            assignees = meta_in.get("assignees")
            try:
                priority = int(meta_in.get("priority", 0))
            except (TypeError, ValueError):
                priority = 0
            # "wantsAgent" makes the owner's node start a coding agent on this
            # issue automatically once merged — unlike the other meta fields
            # above, that's an immediate, unreviewed side effect, so it's only
            # honored when the request comes from a privileged account: the repo
            # owner, or an admin acting on the owner's behalf (adhoc #225).
            wants_agent = False
            agent_model = ""
            agent_provider = ""
            if meta_in.get("wantsAgent"):
                ok, err = await _authorize_owner_account(env, owner, data)
                if not ok:
                    return err
                wants_agent = True
                # Optional model choice (e.g. "opus"/"sonnet"/"haiku"/"fable" or a
                # full model id) for the agent the desktop node auto-starts on this
                # issue; empty leaves the provider's own default. Only meaningful
                # alongside wantsAgent, so it's not parsed otherwise.
                agent_model = clean_string(meta_in.get("model", ""), 60)
                # Optional agent-provider choice from the web dropdown (adhoc #234);
                # only a known provider is honored, else the node picks its default.
                provider_in = clean_string(meta_in.get("provider", ""), 40)
                if provider_in in ("claude-code", "claude-api", "openai"):
                    agent_provider = provider_in
            meta = {
                "labels": [clean_string(x, 60) for x in (labels or [])][:20]
                if isinstance(labels, list) else [],
                "milestone": clean_string(meta_in.get("milestone", ""), 120),
                "priority": priority if 0 <= priority <= 99 else 0,
                "assignees": [clean_string(x, 60) for x in (assignees or [])][:20]
                if isinstance(assignees, list) else [],
                "wantsAgent": wants_agent,
                "model": agent_model,
                "provider": agent_provider,
            }
        item = {
            "number": number,
            "titleIfNew": clean_string(data.get("titleIfNew", ""), 240),
            "event": event,
            "meta": meta,
            "submitter": clean_string(event.get("author", ""), 120),
            "submittedAt": int(Date.now()),
        }
        await d1_run(
            env,
            "INSERT INTO issue_inbox (repo_bi, data, submitter_bi) VALUES (?,?,?)",
            repo_bi, await encrypt_row(env, item), submitter_bi,
        )
        await _record_contributor(env, event.get("author", ""), "issues")
        actor = clean_string(event.get("authorName", "") or event.get("author", ""), MAX_NODE_NAME).lower()
        await notify_pending_inbox(env, owner, repo, "issue", actor, item.get("titleIfNew", ""), number)
        await notify_mentions(env, owner, repo, actor, item.get("titleIfNew", ""), event.get("body", ""),
                              repo_web_href(owner, repo), "issue")
        assignees = list(meta.get("assignees", [])) if isinstance(meta, dict) else []
        if isinstance(event.get("assignees"), list):
            assignees.extend(event.get("assignees"))
        await notify_issue_assignees(env, owner, repo, assignees, actor, item.get("titleIfNew", ""), number)
        # Subscriptions (issue #361): tell everyone already following this issue
        # about the new activity, then auto-subscribe the commenter so they hear
        # about later replies. Both no-op for a brand-new issue (number 0).
        await notify_subscribers(env, owner, repo, "issue", number, actor,
                                 item.get("titleIfNew", ""), event.get("body", ""),
                                 repo_web_href(owner, repo))
        await subscribe_thread(env, owner, repo, "issue", number, actor)
        return json_response({"ok": True}, status=201)

    if method == "GET":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        rows = await d1_all(
            env, "SELECT data FROM issue_inbox WHERE repo_bi=? ORDER BY id ASC",
            repo_bi,
        )
        pending = [rec for rec in
                   [await decrypt_row(env, r["data"]) for r in rows] if rec]
        return json_response({"ok": True, "pending": pending})

    if method == "DELETE":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        await d1_run(env, "DELETE FROM issue_inbox WHERE repo_bi=?", repo_bi)
        return json_response({"ok": True})

    return json_response({"error": "method_not_allowed"}, status=405)


async def pulls_handler(env, request, owner, repo):
    await ensure_schema(env)
    method = method_name(request)
    repo_bi = await blind_index(env, owner + "/" + repo)
    if method == "POST":
        try:
            data = await request.json()
        except Exception:
            return json_response({"error": "invalid_json"}, status=400)
        # A submission is either a whole new PR ("pull") or a signed conversation
        # event ("event" + "number") — a comment or review on an existing PR.
        event = data.get("event")
        if isinstance(event, dict):
            try:
                number = int(data.get("number", 0))
            except (TypeError, ValueError):
                number = 0
            if len((event.get("body", "") or "").encode("utf-8")) > MAX_ISSUE_BYTES:
                return json_response({"error": "event_too_large"}, status=413)
            if not await verify_pull_comment_event(number, event):
                return json_response({"error": "bad_signature"}, status=401)
            count = await d1_first(
                env, "SELECT COUNT(*) AS c FROM pull_inbox WHERE repo_bi=?", repo_bi
            )
            if count and count.get("c", 0) >= MAX_PENDING_PULLS:
                return json_response({"error": "inbox_full"}, status=429)
            submitter_bi = await blind_index(env, event.get("author", ""))
            if await _inbox_author_over_quota(
                    env, "pull_inbox", repo_bi, submitter_bi):
                return json_response({"error": "author_quota"}, status=429)
            item = {
                "number": number,
                "event": event,
                "submitter": clean_string(event.get("author", ""), 120),
                "submittedAt": int(Date.now()),
            }
            await d1_run(
                env,
                "INSERT INTO pull_inbox (repo_bi, data, submitter_bi) "
                "VALUES (?,?,?)",
                repo_bi, await encrypt_row(env, item), submitter_bi,
            )
            await _record_contributor(env, event.get("author", ""), "pulls")
            actor = clean_string(event.get("authorName", "") or event.get("author", ""), MAX_NODE_NAME).lower()
            await notify_pending_inbox(env, owner, repo, "pull", actor, event.get("body", ""), number)
            await notify_mentions(env, owner, repo, actor, "Pull request comment", event.get("body", ""),
                                  repo_web_href(owner, repo), "pull")
            # Subscriptions (issue #361): fan the comment out to the PR's
            # followers, then auto-subscribe the commenter.
            await notify_subscribers(env, owner, repo, "pull", number, actor,
                                     "Pull request comment", event.get("body", ""),
                                     repo_web_href(owner, repo))
            await subscribe_thread(env, owner, repo, "pull", number, actor)
            return json_response({"ok": True}, status=201)
        pull = data.get("pull")
        if not isinstance(pull, dict):
            return json_response({"error": "pull_required"}, status=400)
        pull_bytes = len((pull.get("patch", "") or "").encode("utf-8")) + len(
            (pull.get("commits", "") or "").encode("utf-8"))
        if pull_bytes > MAX_PULL_BYTES:
            return json_response({"error": "pull_too_large"}, status=413)
        if not await verify_pull_event(pull):
            return json_response({"error": "bad_signature"}, status=401)
        count = await d1_first(
            env, "SELECT COUNT(*) AS c FROM pull_inbox WHERE repo_bi=?", repo_bi
        )
        if count and count.get("c", 0) >= MAX_PENDING_PULLS:
            return json_response({"error": "inbox_full"}, status=429)
        submitter_bi = await blind_index(env, pull.get("author", ""))
        if await _inbox_author_over_quota(env, "pull_inbox", repo_bi, submitter_bi):
            return json_response({"error": "author_quota"}, status=429)
        item = {
            "pull": pull,
            "submitter": clean_string(pull.get("author", ""), 120),
            "submittedAt": int(Date.now()),
        }
        await d1_run(
            env,
            "INSERT INTO pull_inbox (repo_bi, data, submitter_bi) VALUES (?,?,?)",
            repo_bi, await encrypt_row(env, item), submitter_bi,
        )
        await _record_contributor(env, pull.get("author", ""), "pulls")
        actor = clean_string(pull.get("authorName", "") or pull.get("author", ""), MAX_NODE_NAME).lower()
        title = clean_string(pull.get("title", "") or pull.get("subject", ""), 240)
        await notify_pending_inbox(env, owner, repo, "pull", actor, title, 0)
        await notify_mentions(env, owner, repo, actor, title, pull.get("body", ""),
                              repo_web_href(owner, repo), "pull")
        return json_response({"ok": True}, status=201)

    if method == "GET":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        rows = await d1_all(
            env, "SELECT data FROM pull_inbox WHERE repo_bi=? ORDER BY id ASC",
            repo_bi,
        )
        pending = [rec for rec in
                   [await decrypt_row(env, r["data"]) for r in rows] if rec]
        return json_response({"ok": True, "pending": pending})

    if method == "DELETE":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        await d1_run(env, "DELETE FROM pull_inbox WHERE repo_bi=?", repo_bi)
        return json_response({"ok": True})

    return json_response({"error": "method_not_allowed"}, status=405)


async def commits_handler(env, request, owner, repo):
    await ensure_schema(env)
    method = method_name(request)
    repo_bi = await blind_index(env, owner + "/" + repo)
    if method == "POST":
        try:
            data = await request.json()
        except Exception:
            return json_response({"error": "invalid_json"}, status=400)
        comment = data.get("comment")
        sha = clean_string(data.get("sha", ""), 40)
        if not isinstance(comment, dict) or not sha:
            return json_response({"error": "comment_required"}, status=400)
        if len((comment.get("body", "") or "").encode("utf-8")) > MAX_COMMIT_COMMENT_BYTES:
            return json_response({"error": "comment_too_large"}, status=413)
        if not await verify_commit_comment_event(sha, comment):
            return json_response({"error": "bad_signature"}, status=401)
        count = await d1_first(
            env, "SELECT COUNT(*) AS c FROM commit_inbox WHERE repo_bi=?", repo_bi
        )
        if count and count.get("c", 0) >= MAX_PENDING_COMMIT_COMMENTS:
            return json_response({"error": "inbox_full"}, status=429)
        submitter_bi = await blind_index(env, comment.get("author", ""))
        if await _inbox_author_over_quota(env, "commit_inbox", repo_bi, submitter_bi):
            return json_response({"error": "author_quota"}, status=429)
        item = {
            "sha": sha,
            "comment": comment,
            "submitter": clean_string(comment.get("author", ""), 120),
            "submittedAt": int(Date.now()),
        }
        await d1_run(
            env,
            "INSERT INTO commit_inbox (repo_bi, data, submitter_bi) VALUES (?,?,?)",
            repo_bi, await encrypt_row(env, item), submitter_bi,
        )
        await _record_contributor(env, comment.get("author", ""), "commits")
        actor = clean_string(comment.get("authorName", "") or comment.get("author", ""), MAX_NODE_NAME).lower()
        await notify_pending_inbox(env, owner, repo, "commit_comment", actor, sha, 0)
        await notify_mentions(env, owner, repo, actor, "Commit " + sha[:12], comment.get("body", ""),
                              repo_web_href(owner, repo), "commit_comment")
        return json_response({"ok": True}, status=201)

    if method == "GET":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        rows = await d1_all(
            env, "SELECT data FROM commit_inbox WHERE repo_bi=? ORDER BY id ASC",
            repo_bi,
        )
        pending = [rec for rec in
                   [await decrypt_row(env, r["data"]) for r in rows] if rec]
        return json_response({"ok": True, "pending": pending})

    if method == "DELETE":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        await d1_run(env, "DELETE FROM commit_inbox WHERE repo_bi=?", repo_bi)
        return json_response({"ok": True})

    return json_response({"error": "method_not_allowed"}, status=405)


async def discussions_handler(env, request, owner, repo):
    await ensure_schema(env)
    method = method_name(request)
    repo_bi = await blind_index(env, owner + "/" + repo)
    if method == "POST":
        try:
            data = await request.json()
        except Exception:
            return json_response({"error": "invalid_json"}, status=400)
        event = data.get("event")
        if not isinstance(event, dict):
            return json_response({"error": "event_required"}, status=400)
        try:
            number = int(data.get("number", 0))
        except (TypeError, ValueError):
            number = 0
        if event.get("type", "") == "open":
            number = 0
        if len(discussion_event_content(event).encode("utf-8")) > MAX_DISCUSSION_BYTES:
            return json_response({"error": "discussion_too_large"}, status=413)
        if not await verify_discussion_event(number, event):
            return json_response({"error": "bad_signature"}, status=401)
        count = await d1_first(
            env, "SELECT COUNT(*) AS c FROM discussion_inbox WHERE repo_bi=?", repo_bi
        )
        if count and count.get("c", 0) >= MAX_PENDING_DISCUSSIONS:
            return json_response({"error": "inbox_full"}, status=429)
        submitter_bi = await blind_index(env, event.get("author", ""))
        if await _inbox_author_over_quota(
                env, "discussion_inbox", repo_bi, submitter_bi):
            return json_response({"error": "author_quota"}, status=429)
        item = {
            "number": number,
            "titleIfNew": clean_string(data.get("titleIfNew", ""), 240),
            "event": event,
            "submitter": clean_string(event.get("author", ""), 120),
            "submittedAt": int(Date.now()),
        }
        await d1_run(
            env,
            "INSERT INTO discussion_inbox (repo_bi, data, submitter_bi) "
            "VALUES (?,?,?)",
            repo_bi, await encrypt_row(env, item), submitter_bi,
        )
        actor = clean_string(event.get("authorName", "") or event.get("author", ""), MAX_NODE_NAME).lower()
        title = item.get("titleIfNew", "") or "Discussion update"
        await notify_pending_inbox(env, owner, repo, "discussion", actor, title, number)
        await notify_mentions(env, owner, repo, actor, title, event.get("body", ""),
                              repo_web_href(owner, repo), "discussion")
        return json_response({"ok": True}, status=201)

    if method == "GET":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        rows = await d1_all(
            env, "SELECT data FROM discussion_inbox WHERE repo_bi=? ORDER BY id ASC",
            repo_bi,
        )
        pending = [rec for rec in
                   [await decrypt_row(env, r["data"]) for r in rows] if rec]
        return json_response({"ok": True, "pending": pending})

    if method == "DELETE":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        await d1_run(env, "DELETE FROM discussion_inbox WHERE repo_bi=?", repo_bi)
        return json_response({"ok": True})

    return json_response({"error": "method_not_allowed"}, status=405)


# --- Agent-session sync (website "Agents" tab, adhoc #182) ------------------
#
# The desktop app runs Claude Code coding "agent" sessions per repo/issue.
# There's no existing sync of that state to the worker; these three routes
# add it end to end:
#   POST /agents        desktop -> worker: full-replace push of this repo's
#                        sessions, authenticated like an issue-inbox drain
#                        (_authorize_owner: ts+sig query params).
#   GET  /agents         desktop -> worker: drain (select + delete) any
#                        prompts the website queued for this repo's agents.
#   POST /agents/list    website -> worker: owner-password-gated read of the
#                        current session list (browsers hold no signing key).
#   POST /agents/<id>/prompt
#                        website -> worker: owner-password-gated, queue one
#                        text prompt for a specific running agent.
def _clean_agent_session(item):
    """Normalize one posted agent-session dict, or None if it's unusable."""
    if not isinstance(item, dict):
        return None
    try:
        agent_id = int(item.get("id", 0))
    except (TypeError, ValueError):
        return None
    if not agent_id:
        return None
    out = {"id": agent_id}
    try:
        out["issueNumber"] = int(item.get("issueNumber", 0) or 0)
    except (TypeError, ValueError):
        out["issueNumber"] = 0
    out["issueTitle"] = clean_string(item.get("issueTitle", ""), MAX_AGENT_TITLE)
    # Bounded run-log tail for the website's live transcript view (adhoc #259);
    # empty for pushes from older desktop builds that don't send it.
    out["transcript"] = clean_string(item.get("transcript", ""), MAX_AGENT_TRANSCRIPT)
    for field in ("status", "provider", "model", "branchName", "lastError"):
        out[field] = clean_string(item.get(field, ""), MAX_AGENT_STRING)
    for field in ("createdAtMs", "startedAtMs", "finishedAtMs", "numTurns", "durationMs"):
        try:
            out[field] = int(item.get(field, 0) or 0)
        except (TypeError, ValueError):
            out[field] = 0
    try:
        out["costUsd"] = float(item.get("costUsd", 0) or 0)
    except (TypeError, ValueError):
        out["costUsd"] = 0.0
    return out


async def agents_handler(env, request, owner, repo):
    await ensure_schema(env)
    method = method_name(request)
    repo_bi = await blind_index(env, owner + "/" + repo)

    if method == "POST":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        try:
            data = await request.json()
        except Exception:
            return json_response({"error": "invalid_json"}, status=400)
        sessions_in = data.get("sessions")
        if not isinstance(sessions_in, list):
            sessions_in = []
        # Cap to the most recent MAX_AGENT_SESSIONS entries rather than
        # rejecting the whole push outright (same truncate-not-reject
        # convention as the labels/assignees lists in issues_handler).
        sessions = [s for s in
                    (_clean_agent_session(item) for item in sessions_in[:MAX_AGENT_SESSIONS]) if s]
        # Dedupe by id, keeping the last occurrence: repo_agents is keyed on
        # (repo_bi, agent_id), so a payload with a repeated id would otherwise
        # violate that primary key on the second INSERT below.
        by_id = {}
        for session in sessions:
            by_id[session["id"]] = session
        sessions = list(by_id.values())
        # Full-replace semantics: the desktop always pushes its whole current
        # view of this repo's sessions, so the stored set is exactly that.
        await d1_run(env, "DELETE FROM repo_agents WHERE repo_bi=?", repo_bi)
        now = int(Date.now())
        for session in sessions:
            await d1_run(
                env,
                "INSERT INTO repo_agents (repo_bi, agent_id, data, updated_at) "
                "VALUES (?,?,?,?)",
                repo_bi, str(session["id"]), await encrypt_row(env, session), now,
            )
        return json_response({"ok": True})

    if method == "GET":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        rows = await d1_all(
            env,
            "SELECT id, data FROM agent_prompts WHERE repo_bi=? ORDER BY id ASC",
            repo_bi,
        )
        prompts = [rec for rec in
                   [await decrypt_row(env, r["data"]) for r in rows] if rec]
        await d1_run(env, "DELETE FROM agent_prompts WHERE repo_bi=?", repo_bi)
        return json_response({"ok": True, "prompts": prompts})

    return json_response({"error": "method_not_allowed"}, status=405)


async def agents_list_handler(env, request, owner, repo):
    await ensure_schema(env)
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    ok, err = await _authorize_owner_account(env, owner, data)
    if not ok:
        return err
    repo_bi = await blind_index(env, owner + "/" + repo)
    rows = await d1_all(
        env, "SELECT data FROM repo_agents WHERE repo_bi=? ORDER BY updated_at DESC",
        repo_bi,
    )
    agents = [rec for rec in
              [await decrypt_row(env, r["data"]) for r in rows] if rec]
    # The transcript tail can be large; keep it out of the list payload and let
    # the detail page fetch just the selected agent's transcript on demand.
    for rec in agents:
        rec.pop("transcript", None)
    return json_response({"ok": True, "agents": agents})


async def agents_transcript_handler(env, request, owner, repo, agent_id):
    await ensure_schema(env)
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    ok, err = await _authorize_owner_account(env, owner, data)
    if not ok:
        return err
    repo_bi = await blind_index(env, owner + "/" + repo)
    row = await d1_first(
        env, "SELECT data FROM repo_agents WHERE repo_bi=? AND agent_id=?",
        repo_bi, str(agent_id),
    )
    rec = await decrypt_row(env, row["data"]) if row else None
    if not rec:
        return json_response({"error": "not_found"}, status=404)
    return json_response({
        "ok": True,
        "transcript": rec.get("transcript", ""),
        "status": rec.get("status", ""),
    })


async def agents_prompt_handler(env, request, owner, repo, agent_id):
    await ensure_schema(env)
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    ok, err = await _authorize_owner_account(env, owner, data)
    if not ok:
        return err
    text = str(data.get("text", "") or "").strip()
    if not text:
        return json_response({"error": "text_required"}, status=400)
    if len(text) > MAX_AGENT_PROMPT_TEXT:
        return json_response({"error": "text_too_long"}, status=400)
    repo_bi = await blind_index(env, owner + "/" + repo)
    count = await d1_first(
        env, "SELECT COUNT(*) AS c FROM agent_prompts WHERE repo_bi=?", repo_bi
    )
    if count and count.get("c", 0) >= MAX_PENDING_AGENT_PROMPTS:
        return json_response({"error": "prompt_queue_full"}, status=429)
    now = int(Date.now())
    item = {"agentId": agent_id, "text": text, "queuedAt": now}
    # Optional agent-provider choice from the website's top-of-list composer
    # (adhoc #271): only meaningful when starting a brand-new agent, and only a
    # known provider is honored — else the node falls back to its own default.
    provider_in = clean_string(data.get("provider", ""), 40)
    if provider_in in ("claude-code", "claude-api", "openai"):
        item["provider"] = provider_in
    # Optional agent-model choice (adhoc #276): lets the owner choose which
    # Claude model the agent uses (e.g. "claude-opus-4-8", "claude-sonnet-4-6",
    # "claude-haiku-4-5", "fable-5"). Only meaningful when starting a brand-new
    # agent, and empty leaves the provider's own default in place.
    model_in = clean_string(data.get("model", ""), 60)
    if model_in:
        item["model"] = model_in
    await d1_run(
        env,
        "INSERT INTO agent_prompts (repo_bi, agent_id, data, queued_at) "
        "VALUES (?,?,?,?)",
        repo_bi, agent_id, await encrypt_row(env, item), now,
    )
    return json_response({"ok": True})


# --- Error log + admin dashboard -------------------------------------------

def _html_escape(value):
    return (
        str(value)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


async def log_error(env, status, method, path, message, ray=""):
    """Record a 5xx / unhandled error. Best-effort: never raises."""
    try:
        await ensure_schema(env)
        await d1_run(
            env,
            """INSERT INTO error_log (ts, status, method, path, message, ray)
               VALUES (?,?,?,?,?,?)""",
            int(Date.now()), int(status), str(method or ""), str(path or ""),
            str(message or "")[:1000], str(ray or ""),
        )
        # Keep only the most-recent MAX_ERROR_LOG rows so the table is bounded.
        await d1_run(
            env,
            """DELETE FROM error_log WHERE id NOT IN
               (SELECT id FROM error_log ORDER BY id DESC LIMIT ?)""",
            MAX_ERROR_LOG,
        )
    except Exception:
        pass


# Ordered install funnel: every step the installer reports, in the order it runs.
# Used both to validate incoming events and to render the admin funnel in order.
INSTALL_DIAG_STEPS = (
    "start",    # installer launched
    "mirror",   # resolved an online mirror to clone from
    "deps",     # build prerequisites present / installed
    "fetch",    # repository cloned or updated
    "build",    # cmake configure + build
    "install",  # binary installed to ~/.local/bin
    "launch",   # first launch (or intentionally skipped)
    "done",     # whole install finished
)


# Known component labels for private vulnerability reports. Restricting the
# set prevents arbitrary strings from appearing in notification emails.
SECURITY_REPORT_COMPONENTS = frozenset({
    "identity",     # Ed25519 keys, signing
    "relay",        # Cloudflare relay / Durable Objects
    "mirroring",    # bare-git mirror fetch/serve
    "chat",         # encrypted relay chat
    "donations",    # Solana custody / sweep
    "website",      # forkmesh.com frontend / dashboard
    "client",       # desktop Qt node
    "other",        # anything else
})


def _sanitize_diag_field(value, max_length=64):
    # Coarse, non-identifying tokens only (platform/pm/distro/version/detail).
    # Strip to a safe charset so a crafted POST can't smuggle markup or control
    # characters into the admin HTML, and clamp the length.
    if not isinstance(value, str):
        value = "" if value is None else str(value)
    cleaned = "".join(
        c for c in value if c.isalnum() or c in " ._:+-,/()"
    ).strip()
    return cleaned[:max_length]


def _install_diag_fields(payload):
    """Normalize a posted install-diag event into stored columns, or None.

    Pure (no I/O) so it can be unit-tested. Rejects anything outside the known
    step set; clamps/sanitizes every other field. Returns
    (run, step, ok, os, arch, pm, distro, version, detail) on success.
    """
    if not isinstance(payload, dict):
        return None
    step = payload.get("step")
    if step not in INSTALL_DIAG_STEPS:
        return None
    run = _sanitize_diag_field(payload.get("run"), 64)
    if not run:
        return None
    ok = 1 if payload.get("ok") in (1, True, "1", "true", "True") else 0
    return (
        run,
        step,
        ok,
        _sanitize_diag_field(payload.get("os"), 32),
        _sanitize_diag_field(payload.get("arch"), 32),
        _sanitize_diag_field(payload.get("pm"), 32),
        _sanitize_diag_field(payload.get("distro"), 32),
        _sanitize_diag_field(payload.get("version"), 48),
        _sanitize_diag_field(payload.get("detail"), 200),
    )


async def install_diag_handler(env, request):
    # Anonymous, unauthenticated install telemetry from install.sh. Best-effort:
    # never errors out the caller (the installer fires these fire-and-forget). We
    # deliberately do not read or store the client IP / CF-Ray — see SCHEMA note.
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        payload = await request.json()
    except Exception:
        payload = None
    fields = _install_diag_fields(payload)
    if not fields:
        # Swallow malformed input rather than 4xx-ing the installer's background
        # curl; nothing actionable for the client to do.
        return json_response({"ok": True}, cache_control="no-store")
    try:
        await ensure_schema(env)
        await d1_run(
            env,
            """INSERT INTO install_diag
               (ts, run, step, ok, os, arch, pm, distro, version, detail)
               VALUES (?,?,?,?,?,?,?,?,?,?)""",
            int(Date.now()), *fields,
        )
        # Bound the table so this open endpoint can't grow D1 without limit.
        await d1_run(
            env,
            """DELETE FROM install_diag WHERE id NOT IN
               (SELECT id FROM install_diag ORDER BY id DESC LIMIT ?)""",
            MAX_INSTALL_DIAG,
        )
    except Exception:
        pass
    return json_response({"ok": True}, cache_control="no-store")


def _sanitize_node_hash(value):
    # The client's anonymized node hash is a hex SHA-256; keep only hex chars and
    # clamp so a crafted value can't smuggle anything into the admin HTML or bloat
    # a row. Empty (anonymous) is allowed.
    if not isinstance(value, str):
        return ""
    return "".join(c for c in value.lower() if c in "0123456789abcdef")[:64]


def _telemetry_rows(payload):
    """Normalize a posted telemetry payload into rows to insert, or [].

    Pure (no I/O) so it can be unit-tested. Anything malformed yields no rows —
    the endpoint swallows it rather than 4xx-ing a best-effort background POST.
    Every field is sanitized and hard-capped; at most TELEMETRY_MAX_EVENTS rows
    come back, so one POST can never fan out unbounded work in the isolate.
    Returns a list of (node, kind, version, os, summary) tuples.
    """
    if not isinstance(payload, dict):
        return []
    events = payload.get("events")
    if not isinstance(events, list) or not events:
        return []
    node = _sanitize_node_hash(payload.get("node"))
    version = _sanitize_diag_field(payload.get("version"), 48)
    os_field = _sanitize_diag_field(payload.get("os"), 64)
    rows = []
    for event in events[:TELEMETRY_MAX_EVENTS]:
        if not isinstance(event, dict):
            continue
        kind = event.get("kind")
        if kind not in TELEMETRY_KINDS:
            continue
        summary = event.get("summary")
        if not isinstance(summary, str):
            continue
        # Coarse control-character scrub + hard length cap. The client already
        # removed repo names / paths; this is belt-and-braces so a crafted POST
        # can't store arbitrarily large or binary junk.
        summary = "".join(
            c for c in summary if c == "\n" or c == "\t" or (c >= " " and c != "\x7f")
        ).strip()[:TELEMETRY_MAX_SUMMARY]
        if not summary:
            continue
        rows.append((node, kind, version, os_field, summary))
    return rows


async def telemetry_handler(env, request):
    # Opt-in crash/stall telemetry from desktop nodes (issue #354). Unauthenticated
    # and anonymous (a one-way node hash only, no account/email/IP). Best-effort:
    # never errors out the caller, since the client fires this fire-and-forget on
    # startup. The body is hard-capped BEFORE parsing so a huge POST can't blow up
    # the small isolate — this must never become an outage vector.
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        raw = await request.text()
    except Exception:
        raw = ""
    if len(raw) > TELEMETRY_MAX_BODY:
        # Reject oversized payloads outright rather than parsing them.
        return json_response({"error": "payload_too_large"}, status=413,
                             cache_control="no-store")
    try:
        payload = json.loads(raw) if raw else None
    except Exception:
        payload = None
    rows = _telemetry_rows(payload)
    if not rows:
        return json_response({"ok": True}, cache_control="no-store")
    try:
        await ensure_schema(env)
        now = int(Date.now())
        for node, kind, version, os_field, summary in rows:
            await d1_run(
                env,
                """INSERT INTO telemetry (ts, node, kind, version, os, summary)
                   VALUES (?,?,?,?,?,?)""",
                now, node, kind, version, os_field, summary,
            )
        # Bound the table so this open endpoint can't grow D1 without limit.
        await d1_run(
            env,
            """DELETE FROM telemetry WHERE id NOT IN
               (SELECT id FROM telemetry ORDER BY id DESC LIMIT ?)""",
            MAX_TELEMETRY,
        )
    except Exception:
        pass
    return json_response({"ok": True}, cache_control="no-store")


def _validate_security_report(payload):
    """Validate and sanitize a vulnerability report payload. Pure (no I/O).

    Returns a normalized dict on success, or None if the payload is invalid.
    Accepted fields:
      title     (required, 1-200 chars)
      body      (required, 1-16 384 chars — the vulnerability description)
      component (required, one of SECURITY_REPORT_COMPONENTS)
      contact   (optional, max 254 chars — e.g. an email or handle)
    """
    if not isinstance(payload, dict):
        return None
    title = (payload.get("title") or "").strip()[:200]
    if not title:
        return None
    body = (payload.get("body") or "").strip()[:16384]
    if not body:
        return None
    component = (payload.get("component") or "").strip().lower()
    if component not in SECURITY_REPORT_COMPONENTS:
        return None
    contact = (payload.get("contact") or "").strip()[:254]
    return {"title": title, "body": body, "component": component, "contact": contact}


async def security_report_handler(env, request):
    """Accept a private vulnerability report via POST /api/security/report.

    Unauthenticated: anyone can submit a report. The payload is AES-GCM
    encrypted before storage so only the operator (with DATA_KEY) can read it.
    An email is sent to SECURITY_EMAIL (or security@forkmesh.com) if configured.
    Always returns 200 {"ok": true} to avoid leaking internal state to probers.
    """
    if method_name(request) != "POST":
        return json_response({"error": "method_not_allowed"}, status=405)
    try:
        payload = await request.json()
    except Exception:
        payload = None
    report = _validate_security_report(payload)
    if not report:
        return json_response({"error": "invalid_report"}, status=400)
    try:
        await ensure_schema(env)
        encrypted = await encrypt_row(env, {
            "title": report["title"],
            "body": report["body"],
            "component": report["component"],
            "contact": report["contact"],
            "ts": int(Date.now()),
        })
        await d1_run(
            env,
            "INSERT INTO security_reports (ts, data) VALUES (?, ?)",
            int(Date.now()), encrypted,
        )
        await d1_run(
            env,
            """DELETE FROM security_reports WHERE id NOT IN
               (SELECT id FROM security_reports ORDER BY id DESC LIMIT ?)""",
            MAX_SECURITY_REPORTS,
        )
    except Exception:
        pass
    # Best-effort email notification to the security team.
    try:
        dest = (getattr(env, "SECURITY_EMAIL", "") or "security@forkmesh.com").strip()
        contact_line = ("Contact: " + report["contact"] + "\n") if report["contact"] else ""
        subject = "[ForkMesh Security] " + report["title"]
        text = (
            "A private vulnerability report was submitted via forkmesh.com.\n\n"
            "Component: " + report["component"] + "\n"
            + contact_line +
            "\n" + report["body"]
        )
        await _send_email(env, dest, subject, text)
    except Exception:
        pass
    return json_response({"ok": True}, cache_control="no-store")


def _admin_path(env):
    # Dynamic, secret admin URL segment. Set ADMIN_PATH in Cloudflare vars.
    return str(getattr(env, "ADMIN_PATH", "") or "").strip("/")


async def admin_stats(env):
    # Cheap snapshot for the dashboard so the operator can watch whether Durable
    # Object load is under control: live hosts/clients (the things that hold a DO
    # open) plus catalog size and recent error volume. All from D1 + one count.
    await ensure_schema(env)
    cutoff = int(Date.now()) - HOST_PRESENCE_STALE_MS
    try:
        await d1_run(env, "DELETE FROM host_presence WHERE ts < ?", cutoff)
    except Exception:
        pass
    repo_row = await d1_first(env, "SELECT COUNT(*) AS n FROM repositories")
    host_row = await d1_first(
        env, "SELECT COUNT(*) AS n FROM host_presence WHERE ts >= ?", cutoff
    )
    day_ago = int(Date.now()) - 24 * 60 * 60 * 1000
    err_row = await d1_first(
        env, "SELECT COUNT(*) AS n FROM error_log WHERE ts >= ?", day_ago,
    )
    # Installs started in the last 24h = distinct anonymous runs that reported the
    # opening "start" step. Best-effort; the table may not exist on a fresh DB.
    try:
        inst_row = await d1_first(
            env,
            "SELECT COUNT(DISTINCT run) AS n FROM install_diag "
            "WHERE step='start' AND ts >= ?",
            day_ago,
        )
    except Exception:
        inst_row = None
    return {
        "repos": int((repo_row or {}).get("n", 0) or 0),
        "hosts": int((host_row or {}).get("n", 0) or 0),
        "clients": await _flagship_client_count(env),
        "installs_24h": int((inst_row or {}).get("n", 0) or 0),
        "errors_24h": int((err_row or {}).get("n", 0) or 0),
    }


async def install_diag_summary(env):
    # Aggregate the anonymous install events into an install funnel plus coarse
    # platform / package-manager / distro breakdowns, over the retained window.
    # All counts are over DISTINCT runs so one chatty install counts once.
    await ensure_schema(env)
    since = int(Date.now()) - INSTALL_DIAG_RETAIN_MS
    funnel_rows = await d1_all(
        env,
        """SELECT step,
                  COUNT(DISTINCT run) AS runs,
                  COUNT(DISTINCT CASE WHEN ok=1 THEN run END) AS ok_runs,
                  COUNT(DISTINCT CASE WHEN ok=0 THEN run END) AS fail_runs
             FROM install_diag WHERE ts >= ? GROUP BY step""",
        since,
    )
    by_step = {str(r.get("step", "")): r for r in funnel_rows}
    funnel = []
    for step in INSTALL_DIAG_STEPS:
        r = by_step.get(step) or {}
        funnel.append({
            "step": step,
            "runs": int(r.get("runs", 0) or 0),
            "ok": int(r.get("ok_runs", 0) or 0),
            "failed": int(r.get("fail_runs", 0) or 0),
        })
    total_row = await d1_first(
        env, "SELECT COUNT(DISTINCT run) AS n FROM install_diag WHERE ts >= ?",
        since,
    )
    done_row = await d1_first(
        env,
        "SELECT COUNT(DISTINCT run) AS n FROM install_diag "
        "WHERE step='done' AND ok=1 AND ts >= ?",
        since,
    )

    async def _breakdown(expr):
        return await d1_all(
            env,
            "SELECT COALESCE(NULLIF(%s,''),'(unknown)') AS k, "
            "COUNT(DISTINCT run) AS runs FROM install_diag WHERE ts >= ? "
            "GROUP BY k ORDER BY runs DESC LIMIT 20" % expr,
            since,
        )

    platforms = await _breakdown("os || ' ' || arch")
    managers = await _breakdown("pm")
    distros = await _breakdown("distro")
    return {
        "total": int((total_row or {}).get("n", 0) or 0),
        "completed": int((done_row or {}).get("n", 0) or 0),
        "funnel": funnel,
        "platforms": [(str(r.get("k", "")), int(r.get("runs", 0) or 0)) for r in platforms],
        "managers": [(str(r.get("k", "")), int(r.get("runs", 0) or 0)) for r in managers],
        "distros": [(str(r.get("k", "")), int(r.get("runs", 0) or 0)) for r in distros],
    }


async def telemetry_summary(env):
    # Aggregate opt-in crash/stall telemetry (issue #354) for the admin dashboard:
    # totals by kind, distinct reporting nodes, and coarse version/OS breakdowns,
    # over the retained window. `node` is already an anonymized one-way hash.
    await ensure_schema(env)
    since = int(Date.now()) - TELEMETRY_RETAIN_MS
    totals = await d1_all(
        env,
        """SELECT kind, COUNT(*) AS n, COUNT(DISTINCT node) AS nodes
             FROM telemetry WHERE ts >= ? GROUP BY kind""",
        since,
    )
    by_kind = {str(r.get("kind", "")): r for r in totals}
    crashes = int((by_kind.get("crash") or {}).get("n", 0) or 0)
    stalls = int((by_kind.get("stall") or {}).get("n", 0) or 0)
    nodes_row = await d1_first(
        env, "SELECT COUNT(DISTINCT node) AS n FROM telemetry WHERE ts >= ?", since)

    async def _breakdown(col):
        return await d1_all(
            env,
            "SELECT COALESCE(NULLIF(%s,''),'(unknown)') AS k, COUNT(*) AS n "
            "FROM telemetry WHERE ts >= ? GROUP BY k ORDER BY n DESC LIMIT 20" % col,
            since,
        )

    versions = await _breakdown("version")
    systems = await _breakdown("os")
    return {
        "crashes": crashes,
        "stalls": stalls,
        "nodes": int((nodes_row or {}).get("n", 0) or 0),
        "versions": [(str(r.get("k", "")), int(r.get("n", 0) or 0)) for r in versions],
        "systems": [(str(r.get("k", "")), int(r.get("n", 0) or 0)) for r in systems],
    }


def _admin_csrf_token(env):
    # A deterministic CSRF token derived from admin/at-rest secrets. It is only
    # ever rendered inside the (Basic-auth-protected) admin HTML, so a cross-site
    # forged POST — which still carries the browser's cached Basic-auth creds —
    # cannot include it. No server-side session store is needed to verify it.
    secret = (str(getattr(env, "ADMIN_PASS", "") or "") + "|" +
              str(getattr(env, "DATA_KEY", "") or "")).encode()
    return hmac.new(secret, b"forkmesh-admin-csrf-v1", "sha256").hexdigest()


def _admin_csrf_ok(env, form):
    submitted = (form.get("csrf", [""]) or [""])[0]
    return bool(submitted) and hmac.compare_digest(
        submitted, _admin_csrf_token(env))


def _check_basic_auth(env, request):
    user = str(getattr(env, "ADMIN_USER", "") or "")
    password = str(getattr(env, "ADMIN_PASS", "") or "")
    if not user or not password:
        return False  # fail closed until creds are configured
    header = request.headers.get("authorization") or ""
    if not header.startswith("Basic "):
        return False
    try:
        decoded = base64.b64decode(header[6:]).decode("utf-8", "replace")
    except Exception:
        return False
    sep = decoded.find(":")
    if sep < 0:
        return False
    ok_user = hmac.compare_digest(decoded[:sep], user)
    ok_pass = hmac.compare_digest(decoded[sep + 1:], password)
    return ok_user and ok_pass


ADMIN_STYLE = """
 *{box-sizing:border-box}
 body{font:14px/1.5 system-ui,sans-serif;margin:0;background:#0d1117;color:#c9d1d9}
 header{padding:16px 24px;border-bottom:1px solid #21262d}
 h1{font-size:18px;margin:0}
 .meta{color:#8b949e;font-size:13px;margin-top:4px}
 a{color:#58a6ff;text-decoration:none}
 a:hover{text-decoration:underline}
 .cards{display:flex;gap:12px;flex-wrap:wrap;padding:16px 24px 0}
 .card{background:#161b22;border:1px solid #21262d;border-radius:8px;padding:12px 18px;min-width:120px}
 .card .n{font-size:24px;font-weight:600}
 .card .l{color:#8b949e;font-size:12px;margin-top:2px}
 .card.warn .n{color:#f85149}
 .tools{padding:12px 24px;display:flex;gap:12px;align-items:center}
 button{background:#238636;color:#fff;border:1px solid #2ea043;border-radius:6px;
        padding:8px 14px;font:600 13px system-ui;cursor:pointer}
 button:hover{background:#2ea043}
 .banner{margin:0 24px 8px;padding:10px 14px;border-radius:6px;border:1px solid #2ea043;
         background:#11251a;color:#aff5c2;white-space:pre-wrap;font:13px ui-monospace,monospace}
 .layout{display:flex;align-items:flex-start}
 nav{width:220px;flex:none;border-right:1px solid #21262d;min-height:60vh;padding:8px 0}
 nav a{display:block;padding:7px 20px;color:#c9d1d9}
 nav a.active{background:#161b22;border-left:3px solid #58a6ff;font-weight:600}
 nav .sec{padding:10px 20px 4px;color:#8b949e;font-size:11px;text-transform:uppercase;letter-spacing:.04em}
 main{flex:1;min-width:0;overflow-x:auto;padding:8px 0 40px}
 table{border-collapse:collapse;width:100%}
 th,td{text-align:left;padding:8px 12px;border-bottom:1px solid #21262d;vertical-align:top}
 th{position:sticky;top:0;background:#161b22;color:#8b949e;font-weight:600}
 td{font-family:ui-monospace,monospace;white-space:pre-wrap;word-break:break-word;max-width:560px}
 .s5{color:#f85149;font-weight:600}
 tr:hover{background:#161b22}
 .empty{padding:32px 24px;color:#8b949e}
 .title{padding:14px 24px 4px;font-weight:600}
 .navcount{color:#8b949e;font-size:11px;font-weight:400}
 .navlink{color:#58a6ff}
 nav a .navcount{float:right}
 .rowform{padding:8px 24px;max-width:760px}
 .rowfield{display:block;margin:10px 0}
 .rowfield span{display:block;color:#8b949e;font-size:12px;margin-bottom:4px}
 .rowfield input,.rowfield textarea{width:100%;background:#0d1117;color:#c9d1d9;
        border:1px solid #30363d;border-radius:6px;padding:8px;
        font:13px ui-monospace,monospace}
 .tools .navlink{padding:8px 4px}
 .diaggrid{display:flex;gap:24px;flex-wrap:wrap;padding:4px 24px 12px;align-items:flex-start}
 .diagcol{min-width:240px}
 .diagcol h3{font-size:13px;color:#8b949e;margin:8px 0 4px;font-weight:600}
 .diagcol table{width:auto;min-width:220px}
"""

# Cloudflare D1 keeps internal bookkeeping tables; hide them from the browser.
ADMIN_HIDDEN_TABLES = ("_cf_KV",)


async def _admin_list_tables(env):
    rows = await d1_all(
        env,
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE 'sqlite_%' AND name NOT LIKE '_cf_%' ORDER BY name",
    )
    return [str(r.get("name", "")) for r in rows
            if r.get("name") and r.get("name") not in ADMIN_HIDDEN_TABLES]


def _admin_cell(column, value, env_unused=None):
    text = "" if value is None else str(value)
    if len(text) > 4000:
        text = text[:4000] + "…"
    return _html_escape(text)


# --- Admin bulk select + delete helpers (operate on rowid) -------------------

def _admin_bulk_form_open(table, csrf_field=""):
    return (
        '<form method="post" action="?table=%s&amp;action=delete_rows" '
        'onsubmit="return confirm(\'Delete the selected row(s)? This cannot be '
        'undone.\')">'
        '%s'
        '<div class="tools">'
        '<button type="submit">Delete selected</button>'
        '<span class="meta">Tick rows (or the header box for all) then delete.'
        '</span></div>'
    ) % (_html_escape(table), csrf_field)


def _admin_select_all_th():
    return (
        '<th><input type="checkbox" title="Select all" '
        "onclick=\"for(const c of this.closest('table')"
        ".querySelectorAll('input[name=ids]'))c.checked=this.checked\"></th>"
    )


def _admin_row_checkbox(rowid):
    return ('<td><input type="checkbox" name="ids" value="%s"></td>'
            % _html_escape(rowid))


async def _admin_table_columns(env, table):
    # Real column names for the table (table name is validated by the caller).
    rows = await d1_all(env, "PRAGMA table_info(" + table + ")")
    return [str(r.get("name", "")) for r in rows if r.get("name")]


async def _render_row_form(env, table, rowid, csrf_field=""):
    # Full-page create/edit form for one row. Field per column; the encrypted
    # `data` column is shown decrypted as JSON in a textarea and re-encrypted on
    # save. Field names are prefixed "f_" so they never collide with rowid/action.
    columns = await _admin_table_columns(env, table)
    row = {}
    editing = bool(rowid) and str(rowid).isdigit()
    if editing:
        row = await d1_first(
            env, "SELECT * FROM " + table + " WHERE rowid=?", int(rowid)) or {}
    action = "update_row" if editing else "insert_row"
    fields = []
    for col in columns:
        value = row.get(col)
        if col == "data":
            shown = ""
            if isinstance(value, str) and value:
                decoded = await decrypt_row(env, value)
                shown = json.dumps(decoded, indent=2, sort_keys=True) if decoded is not None else value
            fields.append(
                '<label class="rowfield"><span>%s (JSON, encrypted on save)</span>'
                '<textarea name="f_%s" rows="12">%s</textarea></label>'
                % (_html_escape(col), _html_escape(col), _html_escape(shown)))
        else:
            text = "" if value is None else str(value)
            fields.append(
                '<label class="rowfield"><span>%s</span>'
                '<input type="text" name="f_%s" value="%s"></label>'
                % (_html_escape(col), _html_escape(col), _html_escape(text)))
    hidden_rowid = ('<input type="hidden" name="rowid" value="%s">'
                    % _html_escape(rowid)) if editing else ""
    title = ("Edit row in %s" % table) if editing else ("Add row to %s" % table)
    return (
        '<div class="title">%s</div>'
        '<form method="post" action="?table=%s&amp;action=%s" class="rowform">'
        '%s%s%s'
        '<div class="tools"><button type="submit">Save</button>'
        '<a class="navlink" href="?table=%s">Cancel</a></div>'
        '</form>'
        % (_html_escape(title), _html_escape(table), action,
           csrf_field, hidden_rowid, "".join(fields), _html_escape(table))
    )


async def _admin_row_values(env, table, form):
    # Map submitted f_<col> fields to (column, value) for valid columns only,
    # encrypting the `data` column from its edited JSON.
    valid = set(await _admin_table_columns(env, table))
    cols, values = [], []
    for key, vals in form.items():
        if not key.startswith("f_"):
            continue
        col = key[2:]
        if col not in valid:
            continue
        raw = vals[0] if vals else ""
        if col == "data":
            parsed = json.loads(raw) if raw.strip() else {}
            values.append(await encrypt_row(env, parsed))
        else:
            values.append(raw)
        cols.append(col)
    return cols, values


async def _admin_update_row(env, table, form):
    rowid = form.get("rowid", [""])[0]
    if not str(rowid).isdigit():
        return "Update failed: missing row id."
    cols, values = await _admin_row_values(env, table, form)
    if not cols:
        return "Update: nothing to change."
    assignments = ",".join(c + "=?" for c in cols)
    await d1_run(env, "UPDATE " + table + " SET " + assignments + " WHERE rowid=?",
                 *values, int(rowid))
    return "Updated row in %s." % table


async def _admin_insert_row(env, table, form):
    cols, values = await _admin_row_values(env, table, form)
    if not cols:
        return "Insert failed: no fields provided."
    placeholders = ",".join(["?"] * len(cols))
    await d1_run(env, "INSERT INTO " + table + " (" + ",".join(cols) + ") VALUES ("
                 + placeholders + ")", *values)
    return "Added a row to %s." % table


def _render_diag_breakdown(title, pairs):
    if not pairs:
        return ""
    rows = "".join(
        "<tr><td>%s</td><td>%s</td></tr>" % (_html_escape(k or "(unknown)"),
                                             _html_escape(n))
        for k, n in pairs)
    return ('<div class="diagcol"><h3>%s</h3><table><thead><tr>'
            '<th>%s</th><th>runs</th></tr></thead><tbody>%s</tbody></table></div>'
            % (_html_escape(title), _html_escape(title), rows))


def _render_install_diag_overview(summary):
    # Anonymous install funnel: per-step distinct-run counts (reached / ok /
    # failed) plus coarse platform breakdowns. Counts are over a 30-day window.
    total = summary.get("total", 0)
    completed = summary.get("completed", 0)
    funnel_rows = []
    for f in summary.get("funnel", []):
        runs = f["runs"]
        pct = ("%d%%" % round(100 * f["ok"] / runs)) if runs else "—"
        fail_cls = ' class="s5"' if f["failed"] else ""
        funnel_rows.append(
            "<tr><td>%s</td><td>%s</td><td>%s</td><td%s>%s</td><td>%s</td></tr>"
            % (_html_escape(f["step"]), _html_escape(runs), _html_escape(f["ok"]),
               fail_cls, _html_escape(f["failed"]), pct))
    funnel_table = (
        '<div class="diagcol"><h3>Install funnel (30d)</h3>'
        '<table><thead><tr><th>Step</th><th>Reached</th><th>OK</th>'
        '<th>Failed</th><th>OK %</th></tr></thead><tbody>'
        + "".join(funnel_rows) + "</tbody></table></div>")
    return (
        '<div class="title">Install diagnostics · %d run(s), %d completed</div>'
        '<div class="meta">Anonymous: each row is a random per-run id, never tied '
        'to an account, email, or IP. 30-day window.</div>'
        '<div class="diaggrid">%s%s%s%s</div>'
        % (total, completed, funnel_table,
           _render_diag_breakdown("Platform", summary.get("platforms", [])),
           _render_diag_breakdown("Pkg manager", summary.get("managers", [])),
           _render_diag_breakdown("Distro", summary.get("distros", [])))
    )


def _render_telemetry_overview(summary):
    # Opt-in crash/stall telemetry: totals by kind + distinct reporting nodes,
    # with coarse version / OS breakdowns. Counts are over a 30-day window and
    # every node id is an anonymized one-way hash.
    crashes = summary.get("crashes", 0)
    stalls = summary.get("stalls", 0)
    nodes = summary.get("nodes", 0)
    cards = (
        '<div class="cards">'
        '<div class="card%s"><div class="n">%s</div><div class="l">crashes (30d)</div></div>'
        '<div class="card"><div class="n">%s</div><div class="l">UI stalls (30d)</div></div>'
        '<div class="card"><div class="n">%s</div><div class="l">reporting nodes</div></div>'
        "</div>"
        % (" warn" if crashes else "", _html_escape(crashes),
           _html_escape(stalls), _html_escape(nodes)))
    return (
        '<div class="title">Crash &amp; stall telemetry</div>'
        '<div class="meta">Opt-in (off by default). Anonymous: each node id is a '
        'one-way hash of the node key, never tied to an account, email, or IP; '
        'repo names and file paths are scrubbed client-side. 30-day window.</div>'
        + cards
        + '<div class="diaggrid">%s%s</div>'
        % (_render_diag_breakdown("Version", summary.get("versions", [])),
           _render_diag_breakdown("OS", summary.get("systems", [])))
    )


async def _render_table_view(env, table, csrf_field=""):
    # Generic "show all rows" view for one D1 table. The encrypted `data` column
    # (accounts/repos/inboxes store an AES-GCM blob there) is decrypted in place
    # so the admin can actually read it. The table name is validated by the
    # caller against the live table list, so it is safe to interpolate.
    # rowid lets the admin select + bulk-delete any row regardless of the table's
    # declared primary key (all these tables are rowid tables).
    rows = await d1_all(env, "SELECT rowid AS _rowid_, * FROM " + table + " LIMIT 500")
    count_row = await d1_first(env, "SELECT COUNT(*) AS n FROM " + table)
    total = int((count_row or {}).get("n", 0) or 0)

    # Admin tool: reset any user account's login password. Shown above the
    # accounts table; posts back to ?action=set_password (handled in _admin).
    prefix = ""
    if table == "accounts":
        prefix = (
            '<div class="tools">'
            '<form method="post" action="?table=accounts&amp;action=set_password" '
            'onsubmit="return confirm(\'Set a new login password for this '
            'account?\')">'
            + csrf_field +
            '<input type="text" name="name" placeholder="node name" '
            'autocomplete="off" required>'
            '<input type="password" name="password" '
            'placeholder="new password (min 8 chars)" minlength="8" required>'
            '<button type="submit">Set password</button>'
            '</form>'
            '<span class="meta">Resets a user account\'s login password '
            '(PBKDF2-hashed); email and payout address are left unchanged.</span>'
            '</div>'
        )

    if table == "telemetry":
        # Purpose-built crash/stall dashboard + recent events, newest first.
        summary = await telemetry_summary(env)
        recent = await d1_all(
            env,
            "SELECT rowid AS _rowid_, * FROM telemetry ORDER BY id DESC LIMIT 200")
        body = []
        for r in recent:
            kind = str(r.get("kind", ""))
            cls = "s5" if kind == "crash" else ""
            body.append(
                "<tr>"
                + _admin_row_checkbox(r.get("_rowid_", ""))
                + '<td data-ts="%s">%s</td>'
                '<td class="%s">%s</td><td>%s</td><td>%s</td>'
                "<td>%s</td><td>%s</td></tr>"
                % (_html_escape(r.get("ts", "")), _html_escape(r.get("ts", "")),
                   cls, _html_escape(kind),
                   _html_escape(r.get("version", "")), _html_escape(r.get("os", "")),
                   _html_escape(str(r.get("node", ""))[:12]),
                   _html_escape(r.get("summary", "")))
            )
        if not body:
            events = '<div class="empty">No telemetry reported yet.</div>'
        else:
            events = (_admin_bulk_form_open(table, csrf_field)
                      + "<table><thead><tr>" + _admin_select_all_th()
                      + "<th>Time</th><th>Kind</th><th>Version</th><th>OS</th>"
                      "<th>Node</th><th>Report</th>"
                      "</tr></thead><tbody>" + "".join(body)
                      + "</tbody></table></form>")
        return (_render_telemetry_overview(summary)
                + '<div class="title">Recent events · %d of %d row(s)</div>'
                % (len(recent), total) + events)

    if table == "install_diag":
        # Purpose-built anonymous install funnel + recent events, newest first.
        summary = await install_diag_summary(env)
        recent = await d1_all(
            env,
            "SELECT rowid AS _rowid_, * FROM install_diag ORDER BY id DESC LIMIT 200")
        body = []
        for r in recent:
            ok = int(r.get("ok", 0) or 0)
            cls = "" if ok else "s5"
            body.append(
                "<tr>"
                + _admin_row_checkbox(r.get("_rowid_", ""))
                + '<td data-ts="%s">%s</td>'
                '<td>%s</td><td class="%s">%s</td><td>%s</td><td>%s</td>'
                "<td>%s</td><td>%s</td><td>%s</td></tr>"
                % (_html_escape(r.get("ts", "")), _html_escape(r.get("ts", "")),
                   _html_escape(r.get("step", "")), cls,
                   "ok" if ok else "fail",
                   _html_escape(r.get("os", "")), _html_escape(r.get("arch", "")),
                   _html_escape(r.get("pm", "")), _html_escape(r.get("distro", "")),
                   _html_escape(r.get("detail", "")))
            )
        if not body:
            events = '<div class="empty">No install events recorded yet.</div>'
        else:
            events = (_admin_bulk_form_open(table, csrf_field)
                      + "<table><thead><tr>" + _admin_select_all_th()
                      + "<th>Time</th><th>Step</th><th>Result</th><th>OS</th>"
                      "<th>Arch</th><th>PM</th><th>Distro</th><th>Detail</th>"
                      "</tr></thead><tbody>" + "".join(body)
                      + "</tbody></table></form>")
        return (_render_install_diag_overview(summary)
                + '<div class="title">Recent events · %d of %d row(s)</div>'
                % (len(recent), total) + events)

    if table == "error_log":
        # Keep the purpose-built, time-formatted error view.
        body = []
        for r in rows:
            status = r.get("status", "")
            cls = "s5" if str(status).startswith("5") else ""
            body.append(
                "<tr>"
                + _admin_row_checkbox(r.get("_rowid_", ""))
                + '<td data-ts="%s">%s</td>'
                '<td class="%s">%s</td>'
                "<td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>"
                % (_html_escape(r.get("ts", "")), _html_escape(r.get("ts", "")),
                   cls, _html_escape(status),
                   _html_escape(r.get("method", "")), _html_escape(r.get("path", "")),
                   _html_escape(r.get("message", "")), _html_escape(r.get("ray", "")))
            )
        if not body:
            inner = '<div class="empty">No errors recorded yet.</div>'
        else:
            inner = (_admin_bulk_form_open(table, csrf_field)
                     + "<table><thead><tr>" + _admin_select_all_th()
                     + "<th>Time</th><th>Status</th><th>Method</th>"
                     "<th>Path</th><th>Message</th><th>CF-Ray</th></tr></thead><tbody>"
                     + "".join(body) + "</tbody></table></form>")
        return ('<div class="title">Error logs · %d row(s)</div>' % total) + inner

    add_link = (' <a class="navlink" href="?table=%s&amp;action=new">+ Add row</a>'
                % _html_escape(table))
    if not rows:
        return (prefix
                + '<div class="title">%s · 0 rows%s</div>'
                  '<div class="empty">This table is empty.</div>'
                % (_html_escape(table), add_link))

    # Column order: union of keys, first row's order first. The synthetic
    # _rowid_ column drives row selection and is not displayed.
    columns = [c for c in rows[0].keys() if c != "_rowid_"]
    for r in rows:
        for k in r.keys():
            if k != "_rowid_" and k not in columns:
                columns.append(k)

    body = []
    for r in rows:
        rid = r.get("_rowid_", "")
        cells = [_admin_row_checkbox(rid),
                 '<td><a class="navlink" href="?table=%s&amp;action=edit&amp;rowid=%s">'
                 'Edit</a></td>' % (_html_escape(table), _html_escape(rid))]
        for col in columns:
            value = r.get(col)
            if col == "data" and isinstance(value, str) and value:
                decoded = await decrypt_row(env, value)
                if decoded is not None:
                    value = json.dumps(decoded, indent=2, sort_keys=True)
            cells.append("<td>%s</td>" % _admin_cell(col, value))
        body.append("<tr>" + "".join(cells) + "</tr>")

    head = _admin_select_all_th() + "<th>Edit</th>" + "".join(
        "<th>%s</th>" % _html_escape(c) for c in columns)
    return (
        prefix
        + '<div class="title">%s · %d row(s)%s%s</div>'
        % (_html_escape(table), total,
           " (showing 500)" if total > 500 else "", add_link)
        + _admin_bulk_form_open(table, csrf_field)
        + "<table><thead><tr>" + head + "</tr></thead><tbody>"
        + "".join(body) + "</tbody></table></form>"
    )


def _render_admin_stats(stats):
    if not stats:
        return ""
    cards = [
        ("hosts", "live hosts", False),
        ("clients", "chat clients", False),
        ("repos", "catalog repos", False),
        ("installs_24h", "installs (24h)", False),
        ("errors_24h", "errors (24h)", True),
    ]
    out = []
    for key, label, warn in cards:
        value = stats.get(key, 0)
        cls = "card warn" if warn and value else "card"
        out.append('<div class="%s"><div class="n">%s</div><div class="l">%s</div></div>'
                    % (cls, _html_escape(value), label))
    return '<div class="cards">' + "".join(out) + "</div>"


def _render_admin_nav(tables, active, counts=None):
    counts = counts or {}
    links = ['<div class="sec">Tables</div>']
    for t in tables:
        label = "Error logs" if t == "error_log" else t
        cls = ' class="active"' if t == active else ""
        n = counts.get(t)
        suffix = (' <span class="navcount">%d</span>' % n) if n is not None else ""
        links.append('<a href="?table=%s"%s>%s%s</a>'
                     % (_html_escape(t), cls, _html_escape(label), suffix))
    return "<nav>" + "".join(links) + "</nav>"


def render_admin_html(env_stats, tables, active_table, table_html, banner="",
                      counts=None, csrf_field=""):
    banner_html = ('<div class="banner">%s</div>' % _html_escape(banner)) if banner else ""
    return (
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>forkmesh · admin</title><style>" + ADMIN_STYLE + "</style></head><body>"
        "<header><h1>forkmesh · admin</h1>"
        "<div class=\"meta\">Live Durable Object load, every D1 table, and "
        "Solana payment-reference status.</div></header>"
        + _render_admin_stats(env_stats)
        + '<div class="tools"><form method="post" action="?action=disburse" '
          'onsubmit="return confirm(\'Sweep confirmed join deposits now?\')">'
          + csrf_field +
          '<button type="submit">Sweep join deposits</button></form>'
          '<span class="meta">Automated sweeping is enabled: confirmed join '
          'deposits sweep to the treasury and online node payout addresses; '
          'this retries any pending sweep.</span></div>'
        + '<div class="tools"><form method="post" action="?action=request_ownership" '
          'onsubmit="return confirm(\'Request ownership transfer for this node?\')">'
          + csrf_field +
          '<input type="text" name="target" placeholder="node to take (name)" '
          'autocomplete="off" required>'
          '<input type="text" name="owner" placeholder="new owner (account name)" '
          'autocomplete="off" required>'
          '<button type="submit">Request ownership transfer</button></form>'
          '<span class="meta">Parks a pending transfer on the node\'s own '
          'account record — it only completes once that node\'s current '
          'owner approves the confirmation prompt on its own client.</span></div>'
        + banner_html
        + '<div class="layout">'
        + _render_admin_nav(tables, active_table, counts)
        + "<main>" + table_html + "</main>"
        + "</div>"
        "<script>for (const el of document.querySelectorAll('[data-ts]')){"
        "const ms=Number(el.getAttribute('data-ts'));"
        "if(ms)el.textContent=new Date(ms).toLocaleString();}</script>"
        "</body></html>"
    )


async def _admin_set_password(env, name, password):
    # Reset a user account's login password. The admin (basic-auth) supplies a
    # node name and a new password; we PBKDF2-hash it and overwrite pass_salt /
    # pass_hash on the encrypted account record, leaving email/solana/etc intact.
    name = clean_string(name or "", MAX_NODE_NAME).lower()
    if not name:
        return "Set password failed: a node name is required."
    if len(password or "") < 8:
        return "Set password failed: password must be at least 8 characters."
    password = password[:256]
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return "Set password failed: no account named '%s'." % name
    salt, phash = await hash_password(password)
    rec["pass_salt"] = salt
    rec["pass_hash"] = phash
    rec.setdefault("status", "active")
    await _save_account(env, name_bi, rec)
    return "Password updated for '%s'. The user can log in with it now." % name


async def _admin_console_request_ownership(env, target, owner):
    # Operator-console counterpart to _admin_request_ownership: the operator is
    # already authenticated via Basic Auth + CSRF for this whole page, so no
    # node-key signature is needed here. Still only PARKS a pending marker —
    # the transfer completes only once the target node's own signed heartbeat
    # decision (approve/deny) comes back, same as the signed API path.
    target = clean_string(target or "", MAX_NODE_NAME).strip().lower()
    owner = clean_string(owner or "", MAX_NODE_NAME).strip().lower()
    if not target or not owner:
        return "Ownership request failed: both a node and a new owner are required."
    if not valid_node_name(owner):
        return "Ownership request failed: '%s' is not a valid account name." % owner
    owner_bi, owner_rec = await _account_row(env, owner)
    if not owner_rec or owner_rec.get("status") != "active":
        return "Ownership request failed: no active account named '%s'." % owner
    ok, result = await _park_ownership_transfer(env, target, owner)
    if not ok:
        return "Ownership request failed for '%s': %s." % (target, result)
    if result.get("alreadyOwned"):
        return "'%s' is already owned by '%s'." % (target, owner)
    return ("Ownership transfer requested: '%s' now needs to approve/deny from "
            "its own client before '%s' takes ownership." % (target, owner))


async def _admin_disburse(env):
    treasury = _treasury_address(env)
    if not treasury:
        return ("No Solana treasury address configured "
                "(set TREASURY_SOLANA_ADDRESS or NODE_SOLANA_ADDRESS).")
    rows = await d1_all(env, "SELECT name_bi, data FROM accounts")
    swept = 0
    already = 0
    pending = 0
    total = 0
    errors = []
    for row in (rows or []):
        rec = await decrypt_row(env, row.get("data"))
        if not rec or not rec.get("donation_confirmed"):
            continue
        if not rec.get("donation_address") or not rec.get("donation_secret"):
            continue
        if rec.get("donation_sweep_sig"):
            already += 1
            continue
        bal = await _solana_balance_lamports(env, rec.get("donation_address"))
        if not bal or bal <= SOLANA_SWEEP_FEE_RESERVE_LAMPORTS:
            continue
        total += bal
        before = rec.get("donation_sweep_sig")
        changed = await _sweep_confirmed_donation(env, row.get("name_bi"), rec, bal)
        if changed:
            await _save_account(env, row.get("name_bi"), rec)
        if rec.get("donation_sweep_sig") and rec.get("donation_sweep_sig") != before:
            swept += 1
        else:
            pending += 1
            if rec.get("donation_sweep_error"):
                errors.append(rec.get("name", "unknown") + ": " +
                              rec.get("donation_sweep_error"))
    if swept == 0 and pending == 0:
        return ("No confirmed per-node deposits awaiting sweep. "
                "Sweep destination (treasury): %s. Already swept: %d." %
                (treasury, already))
    msg = ("Swept %d deposit(s), %d still pending, %d already swept. Checked "
           "%s SOL. Destination treasury: %s." %
           (swept, pending, already, _amount_sol(total), treasury))
    if errors:
        msg += " Last errors: " + "; ".join(errors[:5])
    return msg


class Default(WorkerEntrypoint):
    async def scheduled(self, controller, env, ctx):
        # Cron trigger (every minute, see [triggers] in wrangler.toml): sample how
        # many nodes are online and fold it into the current hour's bucket for the
        # /network/ activity graph. Best-effort — never raise from the cron
        try:
            await record_online_sample(self.env)
        except Exception:
            pass
        # Fold one health check per system into today's bucket for the public
        # /status page's 30-day history.
        try:
            await record_status_sample(self.env)
        except Exception:
            pass
        # Keep blocked phantom catalog entries (and their host presence) purged
        # even if no one loads /network/.
        try:
            await purge_blocked_catalog(self.env)
        except Exception:
            pass
        # Backstop the bounty escrow split so a funded bounty pays out to the
        # author + treasury even if no client polls its status.
        try:
            await sweep_funded_bounties(self.env)
        except Exception:
            pass
        # Central donation fund: once an hour, sweep the fund wallet out to the
        # currently-online nodes (issue #308). The interval gate inside makes the
        # per-minute cron a no-op until an hour has elapsed.
        try:
            await ensure_schema(self.env)
            await _distribute_central_fund(self.env)
        except Exception:
            pass
        # Relay federation: a federated relay registers + reports its online nodes
        # to the main relay; the main relay expires stale federated presence and
        # sweeps confirmed federated signups.
        try:
            await ensure_schema(self.env)
            await _federation_cron(self.env)
        except Exception:
            pass
        # Retained chat history is only pruned per-room on client join
        # (ForkMeshRoom.fetch); sweep all rooms here too so an idle room still
        # gets its 7-day-old messages deleted.
        try:
            await chat_history_prune_expired(self.env)
        except Exception:
            pass
        # Email digest bridge (issue #361): roll each recipient's unread
        # notifications into one email so a reply reaches people who don't have
        # the app open. Per-recipient interval + min-age gates inside keep the
        # per-minute cron cheap and stop it emailing notifications the user is
        # actively reading.
        try:
            await send_notification_digests(self.env)
        except Exception:
            pass

    async def fetch(self, request):
        url = urlparse(request.url)

        # Admin error dashboard at a secret, env-configured path. Handled before
        # routing (and outside the 5xx wrapper) so its own 401 isn't logged.
        admin_path = _admin_path(self.env)
        if admin_path and url.path.strip("/") == admin_path:
            return await self._admin(request)

        # Capture any unhandled exception (which surfaces as an HTTP 500) and any
        # 5xx the handlers return, so the admin dashboard has a record of them.
        try:
            response = await self._route(request, url)
        except Exception as error:
            await log_error(
                self.env, 500, method_name(request), url.path,
                (repr(error) + "\n" + traceback.format_exc()),
                request.headers.get("cf-ray") or "",
            )
            return json_response({"error": "internal_error"}, status=500)
        try:
            status = int(getattr(response, "status", 200) or 200)
        except Exception:
            status = 200
        if status >= 500:
            await log_error(
                self.env, status, method_name(request), url.path,
                "response status %d" % status, request.headers.get("cf-ray") or "",
            )
        return response

    async def _admin(self, request):
        if not _check_basic_auth(self.env, request):
            return Response(
                "Authentication required.",
                status=401,
                headers={"WWW-Authenticate": 'Basic realm="forkmesh-admin"'},
            )
        await ensure_schema(self.env)
        params = parse_qs(urlparse(request.url).query)

        # POST actions: ?action=disburse retries join-deposit sweeps;
        # ?action=set_password resets a user account's login password. Every
        # state-changing POST must carry a CSRF token (rendered only into this
        # Basic-auth-gated page) so a cross-site form — which would still send the
        # browser's cached admin credentials — can't trigger these actions.
        banner = ""
        action = params.get("action", [""])[0]
        csrf_field = ('<input type="hidden" name="csrf" value="%s">'
                      % _html_escape(_admin_csrf_token(self.env)))
        if method_name(request) == "POST":
            try:
                form = parse_qs(await request.text(), keep_blank_values=True)
            except Exception:
                form = {}
            if not _admin_csrf_ok(self.env, form):
                banner = ("Action blocked: invalid or missing CSRF token. "
                          "Reload the admin page and try again.")
            elif action == "disburse":
                try:
                    banner = await _admin_disburse(self.env)
                except Exception as error:
                    banner = "Disburse failed: " + repr(error)
            elif action == "set_password":
                try:
                    banner = await _admin_set_password(
                        self.env,
                        form.get("name", [""])[0],
                        form.get("password", [""])[0],
                    )
                except Exception as error:
                    banner = "Set password failed: " + repr(error)
            elif action == "request_ownership":
                try:
                    banner = await _admin_console_request_ownership(
                        self.env,
                        form.get("target", [""])[0],
                        form.get("owner", [""])[0],
                    )
                except Exception as error:
                    banner = "Ownership request failed: " + repr(error)
            elif action == "delete_rows":
                try:
                    tables = await _admin_list_tables(self.env)
                    table = params.get("table", [""])[0]
                    ids = [int(x) for x in form.get("ids", []) if str(x).isdigit()]
                    if table not in tables:
                        banner = "Delete failed: unknown table."
                    elif not ids:
                        banner = "Delete: no rows were selected."
                    else:
                        # D1 caps bound parameters per query (~100), so delete in
                        # chunks rather than one giant IN (...) list.
                        chunk = 90
                        for start in range(0, len(ids), chunk):
                            batch = ids[start:start + chunk]
                            placeholders = ",".join(["?"] * len(batch))
                            await d1_run(
                                self.env,
                                "DELETE FROM " + table
                                + " WHERE rowid IN (" + placeholders + ")",
                                *batch,
                            )
                        banner = "Deleted %d row(s) from %s." % (len(ids), table)
                except Exception as error:
                    banner = "Delete failed: " + repr(error)
            elif action in ("update_row", "insert_row"):
                try:
                    tables = await _admin_list_tables(self.env)
                    table = params.get("table", [""])[0]
                    if table not in tables:
                        banner = "Save failed: unknown table."
                    elif action == "update_row":
                        banner = await _admin_update_row(self.env, table, form)
                    else:
                        banner = await _admin_insert_row(self.env, table, form)
                except Exception as error:
                    banner = "Save failed: " + repr(error)

        # Left-nav table browser: pick the requested table (validated against the
        # live list), defaulting to the error log.
        tables = await _admin_list_tables(self.env)
        requested = params.get("table", [""])[0]
        if requested in tables:
            active = requested
        elif "error_log" in tables:
            active = "error_log"
        else:
            active = tables[0] if tables else ""

        # Edit/create forms are full-page GET views for the active table.
        if action == "edit" and active:
            rowid = params.get("rowid", [""])[0]
            table_html = await _render_row_form(self.env, active, rowid, csrf_field)
        elif action == "new" and active:
            table_html = await _render_row_form(self.env, active, None, csrf_field)
        else:
            table_html = (await _render_table_view(self.env, active, csrf_field)
                          if active
                          else '<div class="empty">No tables found.</div>')

        # Row counts for the left nav.
        counts = {}
        for t in tables:
            try:
                row = await d1_first(self.env, "SELECT COUNT(*) AS n FROM " + t)
                counts[t] = int((row or {}).get("n", 0) or 0)
            except Exception:
                counts[t] = 0
        stats = await admin_stats(self.env)
        return Response(
            render_admin_html(stats, tables, active, table_html, banner, counts,
                              csrf_field=csrf_field),
            status=200,
            headers={"content-type": "text/html; charset=utf-8"},
        )

    async def _route(self, request, url):
        # Git smart-HTTP clone, proxied to the hosting client over the tunnel.
        git_info = GIT_INFO_RE.match(url.path)
        if git_info:
            service = parse_qs(url.query).get("service", [""])[0]
            if service == "git-upload-pack":
                return await self._git_host(
                    request, git_info.group(1), git_info.group(2))
            if service == "git-receive-pack":
                return await self._git_push(
                    request, git_info.group(1), git_info.group(2))
        git_pack = GIT_PACK_RE.match(url.path)
        if git_pack and method_name(request) == "POST":
            return await self._git_host(request, git_pack.group(1), git_pack.group(2))
        git_recv = GIT_RECEIVE_RE.match(url.path)
        if git_recv and method_name(request) == "POST":
            return await self._git_push(request, git_recv.group(1), git_recv.group(2))

        # Static docs/network directories are canonical with a trailing slash.
        # Keep this as routing support only; all persistent v0.3.0 APIs stay below.
        if url.path == "/network":
            return Response("", status=308, headers={"location": "/network/"})
        if url.path == "/docs":
            return Response("", status=308, headers={"location": "/docs/"})
        if url.path == "/blog":
            return Response("", status=308, headers={"location": "/blogs"})
        if url.path == "/blog.html":
            return Response("", status=308, headers={"location": "/blogs"})

        if url.path in ("/health", "/api/mainnode"):
            return json_response(
                {
                    "ok": True,
                    "service": "forkmesh-mainnode",
                    "node": getattr(self.env, "NODE_NAME", "forkmesh"),
                    "nodeSolanaAddress": getattr(self.env, "NODE_SOLANA_ADDRESS", ""),
                    "websocket": "/api/repo/{owner}/{repo}/rooms/{room}/ws",
                    "compatWebsocket": "/api/room/{room}/ws",
                    "capabilities": [
                        "encrypted-relay-rooms",
                        "repo-scoped-rooms",
                        "ephemeral-ciphertext-broadcast",
                    ],
                    "runtime": "python-workers",
                }
            )

        # Build/version marker for deploy verification. deploy.sh stamps BUILD_REV
        # (the git rev being shipped) as a Worker var on every production deploy and
        # then polls this endpoint: if the live rev never flips to the one it just
        # built, the upload didn't actually take effect on this origin (e.g. it hit
        # the wrong Cloudflare account — the account drift that let stale code keep
        # serving while the deploy reported success), and the script aborts loudly
        # instead of reporting a phantom success.
        if url.path in ("/api/version", "/api/version/"):
            return json_response(
                {
                    "ok": True,
                    "rev": _build_rev(self.env),
                    "now": Date.now(),
                }
            )

        # Cached aggregate stats for the homepage/network page. Served before the
        # per-repo handlers so a burst of visitors collapses to one computation
        # per colo per TTL instead of a Durable Object fan-out per page view.
        if url.path in ("/api/network/stats", "/api/network/stats/"):
            include_payouts = parse_qs(url.query).get("payouts", [""])[0] == "1"
            return await network_stats(self.env, include_payouts=include_payouts)

        # 24-hour online-activity series for the /network/ graph.
        if url.path in ("/api/network/online-history", "/api/network/online-history/"):
            return await online_history(self.env)

        # Public ranking boards (node uptime + repos per owner) for /network/.
        if url.path in ("/api/network/leaderboards", "/api/network/leaderboards/"):
            return await network_leaderboards(self.env)

        # 30-day per-system uptime history for the public /status page.
        if url.path in ("/api/status", "/api/status/"):
            return await status_history(self.env)

        # Installer clone source: pick the currently-online forkmesh host with
        # the most retained uptime instead of baking one node id into install.sh.
        if url.path in ("/api/install-source", "/api/install-source/"):
            return await install_source(self.env)

        # Anonymous per-step diagnostics posted by install.sh (no auth, no IP).
        if url.path in ("/api/install-diag", "/api/install-diag/"):
            return await install_diag_handler(self.env, request)

        # Opt-in crash/stall telemetry posted by desktop nodes (no auth, no IP;
        # anonymized node hash only). See telemetry_handler / issue #354.
        if url.path in ("/api/telemetry", "/api/telemetry/"):
            return await telemetry_handler(self.env, request)

        # Private vulnerability reports — stored encrypted, emailed to security@.
        if url.path in ("/api/security/report", "/api/security/report/"):
            return await security_report_handler(self.env, request)

        # Persistent data lives in D1, not Durable Objects.
        if url.path in ("/api/repositories", "/api/repositories/"):
            return await catalog_handler(self.env, request)

        if url.path in ("/api/notifications", "/api/notifications/"):
            return await notifications_handler(self.env, request)

        # Consolidated lightweight status digest (version + profile/notification
        # change tokens) so the client polls once instead of re-fetching the
        # full profile and notification list every tick.
        if url.path in ("/api/poll", "/api/poll/"):
            return await poll_handler(self.env, request)

        # All /api/accounts/* paths (reserve, donation-address, donation-status,
        # finalize, login, and GET /api/accounts/{name}) are single-segment, so
        # ACCOUNTS_RE matches them and accounts_handler dispatches by path.
        if url.path.startswith("/api/federation/"):
            return await federation_handler(self.env, request)

        if ACCOUNTS_RE.match(url.path):
            return await accounts_handler(self.env, request)

        issues_match = REPO_ISSUES_RE.match(url.path)
        if issues_match:
            owner = safe_segment(issues_match.group(1))
            repo = safe_segment(issues_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await issues_handler(self.env, request, owner, repo)

        pulls_match = REPO_PULLS_RE.match(url.path)
        if pulls_match:
            owner = safe_segment(pulls_match.group(1))
            repo = safe_segment(pulls_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await pulls_handler(self.env, request, owner, repo)

        commits_match = REPO_COMMITS_RE.match(url.path)
        if commits_match:
            owner = safe_segment(commits_match.group(1))
            repo = safe_segment(commits_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await commits_handler(self.env, request, owner, repo)

        discussions_match = REPO_DISCUSSIONS_RE.match(url.path)
        if discussions_match:
            owner = safe_segment(discussions_match.group(1))
            repo = safe_segment(discussions_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await discussions_handler(self.env, request, owner, repo)

        subscribe_match = REPO_SUBSCRIBE_RE.match(url.path)
        if subscribe_match:
            owner = safe_segment(subscribe_match.group(1))
            repo = safe_segment(subscribe_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await subscribe_handler(self.env, request, owner, repo)

        bounty_match = REPO_BOUNTY_RE.match(url.path)
        if bounty_match:
            owner = safe_segment(bounty_match.group(1))
            repo = safe_segment(bounty_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await bounties_handler(self.env, request, owner, repo)

        shares_match = REPO_SHARES_RE.match(url.path)
        if shares_match:
            owner = safe_segment(shares_match.group(1))
            repo = safe_segment(shares_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await shares_handler(self.env, request, owner, repo)

        mirrors_match = REPO_MIRRORS_RE.match(url.path)
        if mirrors_match:
            owner = safe_segment(mirrors_match.group(1))
            repo = safe_segment(mirrors_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await repo_mirrors_handler(self.env, request, owner, repo)

        agents_list_match = REPO_AGENTS_LIST_RE.match(url.path)
        if agents_list_match:
            owner = safe_segment(agents_list_match.group(1))
            repo = safe_segment(agents_list_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await agents_list_handler(self.env, request, owner, repo)

        agents_transcript_match = REPO_AGENTS_TRANSCRIPT_RE.match(url.path)
        if agents_transcript_match:
            owner = safe_segment(agents_transcript_match.group(1))
            repo = safe_segment(agents_transcript_match.group(2))
            agent_id = safe_segment(agents_transcript_match.group(3))
            if not owner or not repo or not agent_id:
                return json_response({"error": "not_found"}, status=404)
            return await agents_transcript_handler(self.env, request, owner, repo, agent_id)

        agents_prompt_match = REPO_AGENTS_PROMPT_RE.match(url.path)
        if agents_prompt_match:
            owner = safe_segment(agents_prompt_match.group(1))
            repo = safe_segment(agents_prompt_match.group(2))
            agent_id = safe_segment(agents_prompt_match.group(3))
            if not owner or not repo or not agent_id:
                return json_response({"error": "not_found"}, status=404)
            return await agents_prompt_handler(self.env, request, owner, repo, agent_id)

        agents_match = REPO_AGENTS_RE.match(url.path)
        if agents_match:
            owner = safe_segment(agents_match.group(1))
            repo = safe_segment(agents_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await agents_handler(self.env, request, owner, repo)

        release_downloads_match = REPO_RELEASE_DOWNLOADS_RE.match(url.path)
        if release_downloads_match:
            owner = safe_segment(release_downloads_match.group(1))
            repo = safe_segment(release_downloads_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            return await release_downloads_handler(self.env, request, owner, repo)

        release_blob_match = RELEASE_BLOB_RE.match(url.path)
        if release_blob_match:
            owner = safe_segment(release_blob_match.group(1))
            repo = safe_segment(release_blob_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            # Public, content-addressed download: forward to the repo's host DO,
            # which streams the blob from a serving node over the tunnel.
            host_id = self.env.FORKMESH_HOST.idFromName(f"host:{owner}/{repo}")
            host_object = self.env.FORKMESH_HOST.get(host_id)
            return await host_object.fetch(request)

        host_match = REPO_HOST_RE.match(url.path)
        if host_match:
            owner = safe_segment(host_match.group(1))
            repo = safe_segment(host_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            # Registering as a host (the WebSocket upgrade) requires a token signed
            # by the owner account's registered key. Read-only browse/clone of a
            # repo someone else hosts stays public, so only gate the upgrade.
            public_browse = False
            upgrade = (request.headers.get("upgrade") or "").lower()
            if upgrade == "websocket":
                params = parse_qs(url.query)
                ts = params.get("ts", [""])[0]
                sig = params.get("sig", [""])[0]
                if not await verify_host_token(self.env, owner, repo, ts, sig):
                    return json_response({"error": "unauthorized"}, status=401)
            elif host_match.group(3) in ("tree", "blobs", "blob", "raw", "history", "commit", "branches", "search"):
                # Browsing a private repo's files/commits needs a view token as
                # ?ts=&sig= (the host-token query shape): the owner's own
                # (forkmesh-view-v1), or — when ?viewer= names a collaborator the
                # repo was shared with — that grantee's (forkmesh-share-view-v1,
                # issue #9). Public repos remain open to browse.
                if await _repo_is_private(self.env, owner, repo):
                    params = parse_qs(url.query)
                    ts = params.get("ts", [""])[0]
                    sig = params.get("sig", [""])[0]
                    viewer = safe_segment(params.get("viewer", [""])[0])
                    if viewer and viewer != owner:
                        ok = await verify_share_view_token(
                            self.env, viewer, owner, repo, ts, sig)
                    else:
                        ok = await verify_view_token(
                            self.env, owner, repo, ts, sig)
                    if not ok:
                        return json_response({"error": "unauthorized"}, status=401)
                else:
                    # Public repo browse (tree/blob/raw/commits): round-robin the
                    # request across every ONLINE mirror of the logical repo,
                    # including the named source, so consecutive page loads
                    # spread over all live nodes. The chosen node serves IN
                    # PLACE, through the URL that was requested: the request is
                    # dispatched to its host DO with the path rewritten into its
                    # namespace — never a client-visible redirect. The DO tags
                    # the response with servedBy (derived from the rewritten
                    # path), so the website still shows which node answered. An
                    # offline source with an online mirror is covered the same
                    # way, as the clone fallback in _git_host does. `fmserved`
                    # survives as a legacy pin: a URL that carries it skips the
                    # rotation (old redirected links keep working).
                    public_browse = True
                    # A mirror that failed the first browse hop, so the failure
                    # handler below skips it on retry instead of re-picking the
                    # same flapping node and returning its error.
                    failed_mirror = None
                    params = parse_qs(url.query)
                    pinned = safe_segment(params.get("fmserved", [""])[0])
                    if not (pinned and pinned.lower() == owner.lower()):
                        served = await self._select_browse_mirror(owner, repo)
                        if served and served.lower() != owner.lower():
                            # A failed forward (exception, 503/504: the pick's
                            # presence row outlived its tunnel, or 502: its host
                            # answered but couldn't produce the tree — e.g. a
                            # freshly-added mirror whose clone is empty/still
                            # syncing) falls through to the named owner's own
                            # route, whose failure handler below tries the
                            # remaining mirrors — a broken mirror hop must
                            # degrade, never take the page down.
                            forwarded = None
                            try:
                                forwarded = await self._forward_to_node(
                                    request, url, repo, served,
                                    "/api/repo/%s/%s/%s" % (
                                        served, repo, host_match.group(3)))
                                fstatus = int(forwarded.status)
                            except Exception:
                                fstatus = 0
                            if forwarded is not None and \
                                    fstatus not in (0, 502, 503, 504):
                                return forwarded
                            failed_mirror = served
            host_id = self.env.FORKMESH_HOST.idFromName(f"host:{owner}/{repo}")
            host_object = self.env.FORKMESH_HOST.get(host_id)
            response = await host_object.fetch(request)
            if public_browse:
                # The routed node couldn't serve (no host connected: 503, a
                # dead-but-lingering tunnel: 504, or a host that answered but
                # couldn't build the reply: 502 — e.g. a freshly-added mirror
                # whose clone is empty/still syncing, so ls-tree has no ref).
                # Its host_presence row can lag reality for up to
                # HOST_PRESENCE_STALE_MS, during which the rotation above still
                # picks it — so on failure, serve once more in place from an
                # online mirror of the same logical repo, excluding the node
                # that just failed. Best-effort: if that forward fails too,
                # return the named node's original error.
                try:
                    status = int(response.status)
                except Exception:
                    status = 0
                if status in (502, 503, 504):
                    fallback = await self._select_browse_mirror(
                        owner, repo, exclude=[owner, failed_mirror])
                    if fallback and fallback.lower() != owner.lower():
                        try:
                            return await self._forward_to_node(
                                request, url, repo, fallback,
                                "/api/repo/%s/%s/%s" % (
                                    fallback, repo, host_match.group(3)))
                        except Exception:
                            pass
            return response

        room = room_key_from_path(url.path)
        if room:
            room_id = self.env.FORKMESH_MAINNODE_ROOM.idFromName(room["key"])
            room_object = self.env.FORKMESH_MAINNODE_ROOM.get(room_id)
            return await room_object.fetch(request)

        # Dashboard SPA shell: /dashboard and /dashboard/* paths are client-side
        # routes, not real files. The shell is split into HTML partials
        # (public/dashboard/partials/) that we stitch together here so that
        # direct-navigation to /dashboard/owner/repo lands on the assembled SPA.
        # (Handled here rather than via _redirects to avoid Cloudflare's
        # loop-detection false-positive on /dashboard/* → /dashboard/index.html.)
        # Dashboard behaviour script: /dashboard.js is composed from its ordered
        # JS fragments (public/dashboard/js/, see dashboard_bundle.py) rather than
        # served as one monolithic static file. Handled before the /dashboard/*
        # SPA-shell branch below (note: "/dashboard.js" has no trailing slash so
        # it doesn't match that branch's startswith("/dashboard/")).
        if url.path == "/dashboard.js":
            return await self._serve_dashboard_bundle(url)

        if url.path == "/dashboard" or url.path.startswith("/dashboard/"):
            return await self._serve_dashboard_shell(url)

        return json_response({"error": "not_found"}, status=404)

    async def _serve_dashboard_bundle(self, url):
        # Concatenate /dashboard.js from its ordered fragments (see
        # dashboard_bundle.py). Each fragment is a real static asset, so
        # env.ASSETS.fetch returns its raw bytes (bypassing the Worker) even
        # though its /dashboard/js/... path is itself run_worker_first.
        base = url.scheme + "://" + url.netloc + "/"

        async def _asset_text(rel):
            resp = await self.env.ASSETS.fetch(base + rel)
            return await resp.text()

        fragments = []
        for name in DASHBOARD_JS_FRAGMENTS:
            fragments.append(await _asset_text(fragment_path(name)))
        js = assemble_bundle(fragments)
        # The composed script is identical for every visitor, so let the edge
        # cache it and keep the concatenation off the Worker CPU budget on the
        # hot dashboard path (same treatment as the composed shell).
        return Response(
            js,
            status=200,
            headers={
                "content-type": "text/javascript; charset=utf-8",
                "cache-control": "public, max-age=300",
            },
        )

    async def _serve_dashboard_shell(self, url):
        # Compose dashboard/index.html and its <!--#include--> partials into one
        # HTML document (see dashboard_shell.py). Each partial is a real static
        # asset, so env.ASSETS.fetch returns its raw bytes (bypassing the Worker)
        # even though its /dashboard/partials/... path is itself run_worker_first.
        base = url.scheme + "://" + url.netloc + "/"

        async def _asset_text(rel):
            resp = await self.env.ASSETS.fetch(base + rel)
            return await resp.text()

        shell = await _asset_text("dashboard/index.html")
        partials = {}
        for name in included_partials(shell):
            partials[name] = await _asset_text(partial_path(name))
        html = assemble_shell(shell, partials)
        # The shell is identical for every visitor (all per-account data is
        # hydrated client-side), so let the edge cache the composed document and
        # keep this off the Worker CPU budget on the hot dashboard path.
        return Response(
            html,
            status=200,
            headers={
                "content-type": "text/html; charset=utf-8",
                "cache-control": "public, max-age=300",
            },
        )

    async def _select_clone_fallback(self, owner, repo, force=False):
        # When owner/repo's own host is offline, find a healthy online mirror of the
        # same logical repo to redirect a clone to. Best-effort: any failure returns
        # None so the request just falls through to the normal named-owner route.
        # `force` overrides the presence fast-path: the caller has already
        # confirmed the named source can't actually serve right now (its host DO
        # reports no connected host), so a still-fresh-but-stale presence row must
        # not veto the redirect — this is what routes a clone around a source whose
        # tunnel died uncleanly, before host_presence ages out.
        try:
            await ensure_schema(self.env)
            now = int(Date.now())
            repo_bi = await blind_index(self.env, owner + "/" + repo)
            presence_rows = await d1_all(
                self.env, "SELECT repo_bi, ts FROM host_presence")
            presence = {
                str(r.get("repo_bi")): int(r.get("ts") or 0)
                for r in presence_rows
                if r.get("repo_bi")
            }
            source_ts = presence.get(repo_bi) or 0
            source_online = bool(
                source_ts and now - source_ts <= HOST_PRESENCE_STALE_MS) and not force
            # Fast path: the named host is live, so serve it directly (and skip the
            # catalog decrypt entirely) — never redirect away from an online source.
            if source_online:
                return None
            rows = await d1_all(
                self.env,
                "SELECT key_bi, data, is_private FROM repositories WHERE is_private = 0")
            catalog_rows = []
            for row in rows:
                rec = await decrypt_row(self.env, row.get("data"))
                if not rec:
                    continue
                if _is_blocked_catalog_identity(
                        self.env, rec.get("owner"), rec.get("name")):
                    continue
                catalog_rows.append({
                    "key_bi": row.get("key_bi"),
                    "is_private": int(row.get("is_private") or 0),
                    "data": rec,
                })
            rotate = await next_clone_rotation(self.env, repo_bi)
            return select_clone_fallback(
                owner, repo, catalog_rows, presence, now,
                HOST_PRESENCE_STALE_MS, source_online, rotate)
        except Exception:
            return None

    async def _select_browse_mirror(self, owner, repo, exclude=None):
        # Round-robin pick of which online mirror serves this website browse of
        # owner/repo. Spreads consecutive page loads across every live mirror of
        # the logical repo (the named source included) so no single node carries
        # all the browse traffic and the website can show which node served the
        # page. Returns the chosen node's owner (possibly `owner` itself), or None
        # when nothing is online. `exclude` drops one node from consideration —
        # used on retry after that node's host DO failed to serve, since its
        # presence row may not have aged out yet. Best-effort: any failure
        # returns None so the request just falls through to the normal
        # named-owner route. `exclude` may be a single node name or an iterable
        # of names — every one is dropped, so a retry can skip BOTH the named
        # source and a mirror that already failed this request instead of
        # re-picking the flapping node and surfacing its error.
        try:
            await ensure_schema(self.env)
            now = int(Date.now())
            repo_bi = await blind_index(self.env, owner + "/" + repo)
            presence_rows = await d1_all(
                self.env, "SELECT repo_bi, ts FROM host_presence")
            presence = {
                str(r.get("repo_bi")): int(r.get("ts") or 0)
                for r in presence_rows
                if r.get("repo_bi")
            }
            rows = await d1_all(
                self.env,
                "SELECT key_bi, data, is_private FROM repositories WHERE is_private = 0")
            catalog_rows = []
            for row in rows:
                rec = await decrypt_row(self.env, row.get("data"))
                if not rec:
                    continue
                if _is_blocked_catalog_identity(
                        self.env, rec.get("owner"), rec.get("name")):
                    continue
                catalog_rows.append({
                    "key_bi": row.get("key_bi"),
                    "is_private": int(row.get("is_private") or 0),
                    "data": rec,
                })
            candidates = browse_mirror_candidates(
                owner, repo, catalog_rows, presence, now, HOST_PRESENCE_STALE_MS)
            if exclude:
                names = [exclude] if isinstance(exclude, str) else list(exclude)
                excluded = {n.lower() for n in names if n}
                candidates = [
                    c for c in candidates if c.lower() not in excluded]
            if not candidates:
                return None
            if len(candidates) == 1:
                return candidates[0]
            # Only pay the round-robin cursor write when there's more than one live
            # mirror to spread the browse across.
            rotate = await next_clone_rotation(self.env, repo_bi)
            return candidates[rotate % len(candidates)]
        except Exception:
            return None

    async def _git_host(self, request, owner_raw, repo_raw):
        owner = safe_segment(owner_raw)
        repo = safe_segment(repo_raw)
        if not owner or not repo:
            return Response("not found", status=404)
        url = urlparse(request.url)
        is_info = url.path.endswith("/info/refs")
        # Private repos clone only with an owner-key-signed view token carried in
        # HTTP Basic auth; public repos stay open. Challenge with 401 Basic so git
        # supplies credentials from the clone URL or a credential helper.
        if await _repo_is_private(self.env, owner, repo):
            if not await _basic_auth_view_ok(self.env, owner, repo, request):
                return _basic_auth_challenge()
        else:
            # Public repo whose named node can't serve right now: serve the clone
            # from a healthy mirror of the same logical repo THROUGH THIS SAME
            # URL — the request is dispatched to the mirror's host DO with the
            # path rewritten to its namespace, never a client-visible redirect.
            # Liveness is ground truth (a subrequest to the named node's host DO
            # for its connected-host count), not the host_presence row, which
            # lags an unclean tunnel death by up to HOST_PRESENCE_STALE_MS —
            # during that window the old presence-based check kept forwarding
            # clones into the dead tunnel ("no host serving" / isolate crash).
            # A git clone is two requests (info/refs, then the upload-pack POST)
            # that must reach the SAME mirror, so the pick is pinned per repo for
            # CLONE_STICKY_MS (see _sticky_clone_fallback) instead of rotating
            # per request. The integrity gate still applies on the serving node:
            # a mirror must advertise a source-attested state (clone_state_pins),
            # so serving in place never weakens the tamper check.
            # Auto-heal a mirror namespace while the source of truth is online:
            # hand the clone to the authoritative source instead of this mirror. A
            # mirror whose refs fail the integrity pin (stale, diverged, or running
            # an agent) would otherwise reject every clone here; the source is
            # online and canonical, so it serves the request and the mirror clears
            # once it re-syncs — its own unverified bytes are never served. Skipped
            # when the source is offline, so the tamper gate still fully protects
            # clones then. Cheap in the common case (cloning the source itself):
            # _online_source_of_truth returns after a single indexed lookup. Both
            # clone requests (info/refs + upload-pack POST) take this branch while
            # the source stays online, so they reach the same node.
            src_owner, src_repo = await self._online_source_of_truth(owner, repo)
            if src_owner:
                tail = "info/refs" if is_info else "git-upload-pack"
                try:
                    return await self._forward_to_node(
                        request, url, src_repo, src_owner,
                        "/%s/%s/%s" % (src_owner, src_repo, tail))
                except Exception:
                    # Best-effort: fall through to the normal route (which may
                    # still mirror-fallback) rather than take the request down.
                    pass
            if not await self._source_has_live_host(owner, repo):
                serving = await self._sticky_clone_fallback(
                    owner, repo, refresh=is_info)
                if serving and serving.lower() != owner.lower():
                    tail = "info/refs" if is_info else "git-upload-pack"
                    try:
                        return await self._forward_to_node(
                            request, url, repo, serving,
                            "/%s/%s/%s" % (serving, repo, tail))
                    except Exception:
                        # Best-effort: fall through to the named owner's route,
                        # which answers with git's clean "no host" advertisement
                        # instead of taking the whole request down.
                        pass
            elif not is_info:
                # Source's host IS connected, but a paired info/refs that just
                # timed out (below) may have failed over to a mirror and pinned
                # it. The upload-pack POST negotiates against the refs that first
                # request advertised, so it MUST follow that same mirror even
                # though the named source's tunnel now looks live — otherwise the
                # pack is negotiated against a different node's refs and the
                # clone breaks. Read-only pin lookup; no rotation.
                serving = await self._fresh_clone_pin(owner, repo)
                if serving and serving.lower() != owner.lower():
                    try:
                        return await self._forward_to_node(
                            request, url, repo, serving,
                            "/%s/%s/git-upload-pack" % (serving, repo))
                    except Exception:
                        pass
        host_id = self.env.FORKMESH_HOST.idFromName(f"host:{owner}/{repo}")
        host_object = self.env.FORKMESH_HOST.get(host_id)
        response = await host_object.fetch(request)
        # The named source's host is connected but stalled answering the (small,
        # idempotent) info/refs advertisement — GIT_TIMEOUT_MS elapses and its
        # host DO returns 504. The upfront liveness check above sees the live
        # WebSocket and never fails over, so the client's clone dead-ends. Retry
        # the advertisement once from a live mirror and pin it (clone_sticky), so
        # the paired upload-pack POST follows the same node. Only info/refs (a
        # bodyless GET) is safe to replay this way; the POST body is already in
        # flight, so it relies on the pin instead. Private repos are excluded —
        # they never fall a clone over to a mirror.
        if is_info and not await _repo_is_private(self.env, owner, repo):
            try:
                status = int(response.status)
            except Exception:
                status = 0
            if status in (503, 504):
                serving = await self._sticky_clone_fallback(
                    owner, repo, refresh=True)
                if serving and serving.lower() != owner.lower():
                    try:
                        return await self._forward_to_node(
                            request, url, repo, serving,
                            "/%s/%s/info/refs" % (serving, repo))
                    except Exception:
                        pass
        return response

    async def _git_push(self, request, owner_raw, repo_raw):
        # git push (issue #358): receive-pack proxied to the owner's own host DO.
        # Unlike a clone this NEVER falls back to a mirror — a mirror is a
        # read-only copy; a push must reach the working-copy holder that can run
        # receive-pack and re-attest the integrity pin. Gated by an owner-key-
        # signed HTTP Basic token; git sends no credentials until it sees a 401,
        # so both info/refs and the POST challenge when auth is missing/invalid.
        owner = safe_segment(owner_raw)
        repo = safe_segment(repo_raw)
        if not owner or not repo:
            return Response("not found", status=404)
        if not await _basic_auth_push_ok(self.env, owner, repo, request):
            return _basic_auth_challenge()
        host_id = self.env.FORKMESH_HOST.idFromName(f"host:{owner}/{repo}")
        host_object = self.env.FORKMESH_HOST.get(host_id)
        return await host_object.fetch(request)

    async def _source_has_live_host(self, owner, repo):
        # Whether a desktop host is CONNECTED to this repo's host DO right now —
        # the ground truth, not the host_presence row (which lags an unclean
        # disconnect by up to HOST_PRESENCE_STALE_MS). One cheap subrequest to the
        # DO's non-WebSocket /host endpoint, which returns {"hosts": N}. Any
        # failure (including the DO throwing) is treated as "not live" so the
        # caller falls back to a mirror rather than stranding the clone on a dead
        # or flapping source.
        try:
            host_id = self.env.FORKMESH_HOST.idFromName(f"host:{owner}/{repo}")
            host_object = self.env.FORKMESH_HOST.get(host_id)
            resp = await host_object.fetch(
                "https://forkmesh.internal/api/repo/%s/%s/host" % (owner, repo))
            if int(getattr(resp, "status", 0) or 0) != 200:
                return False
            data = await resp.json()
            # workers Response.json() may cross the boundary as a dict or a
            # JsProxy (see _flagship_client_count); accept both shapes.
            hosts = (data.get("hosts", 0) if isinstance(data, dict)
                     else getattr(data, "hosts", 0))
            return int(hosts or 0) > 0
        except Exception:
            return False

    async def _online_source_of_truth(self, owner, repo):
        # If owner/repo is a MIRROR whose logical repo (grouped by root commit,
        # name fallback) has a working-copy holder — its source of truth — with a
        # live host right now, return (source_owner, source_repo); otherwise
        # (None, None). Cloning a mirror namespace while the source is online is
        # routed to the source: the source is canonical, so this auto-heals a
        # mirror whose refs fail the integrity pin (stale, diverged, or running an
        # agent) without ever serving the mirror's own unverified bytes. The
        # tamper gate still fully protects clones when the source is OFFLINE — the
        # caller only consults this while looking for an online node to serve.
        # Cheap for the common case: when THIS namespace itself holds the working
        # copy (the usual clone target), we return after one indexed lookup and
        # never scan the catalog. Best-effort: any failure returns (None, None).
        try:
            owner_l = (owner or "").strip().lower()
            repo_l = (repo or "").strip().lower()
            if not owner_l or not repo_l:
                return None, None
            await ensure_schema(self.env)
            repo_bi = await blind_index(self.env, owner + "/" + repo)
            row = await d1_first(
                self.env,
                "SELECT data, is_private FROM repositories WHERE key_bi=?", repo_bi)
            if not row or int(row.get("is_private") or 0):
                return None, None
            target = await decrypt_row(self.env, row.get("data"))
            if not target:
                return None, None
            # The named namespace holds the working copy — it IS a source of
            # truth, so serve it directly (no catalog scan, no self-redirect).
            if str(target.get("source") or "local-node") == "local-node":
                return None, None
            # Find the freshest-synced source-of-truth record in the same group.
            rows = await d1_all(
                self.env, "SELECT data FROM repositories WHERE is_private = 0")
            best_owner = None
            best_repo = None
            best_sync = -1
            for r in rows:
                rec = await decrypt_row(self.env, r.get("data"))
                if not rec:
                    continue
                if str(rec.get("source") or "local-node") != "local-node":
                    continue
                if not repo_mirror_same_group(target, rec):
                    continue
                rec_owner = str(rec.get("owner") or "").strip()
                rec_name = str(rec.get("name") or "").strip()
                if not rec_owner or not rec_name or rec_owner.lower() == owner_l:
                    continue
                sync = _mirror_ms(rec.get("lastSync")) or 0
                if sync > best_sync:
                    best_sync = sync
                    best_owner = rec_owner
                    best_repo = rec_name
            if best_owner and await self._source_has_live_host(best_owner, best_repo):
                return best_owner, best_repo
            return None, None
        except Exception:
            return None, None

    async def _forward_to_node(self, request, url, repo, node, new_path):
        # Serve THROUGH the original URL: dispatch this request to `node`'s host
        # DO with the path rewritten into its namespace. The client never sees a
        # redirect — the mirror's bytes stream back on the URL that was asked
        # for. The rewritten path is also what the DO derives its repo identity
        # from, so presence marking, the servedBy tag and the clone integrity
        # gate (_state_pins) all evaluate against the node actually serving.
        #
        # The forwarded request is rebuilt from PRIMITIVES only — a bare URL
        # string for GETs (the DO reads everything from the path + query, the
        # same shape _source_has_live_host uses), and url + a plain init dict
        # for the git upload-pack POST. It must never be constructed around the
        # incoming Python-wrapped request object: JsRequest.new(target, request)
        # kills the isolate at the JS boundary (Cloudflare error 1101) before
        # Python can even catch it, which is exactly how the first deploy of
        # this fallback took every forwarded browse/clone down.
        target = url.scheme + "://" + url.netloc + new_path
        if url.query:
            target += "?" + url.query
        host_id = self.env.FORKMESH_HOST.idFromName(f"host:{node}/{repo}")
        host_object = self.env.FORKMESH_HOST.get(host_id)
        if method_name(request) != "POST":
            return await host_object.fetch(target)
        body = bytes(await request.bytes())
        headers = {}
        for name in ("content-type", "content-encoding"):
            value = request.headers.get(name)
            if value:
                headers[name] = value
        return await host_object.fetch(JsRequest.new(target, to_js({
            "method": "POST",
            "headers": headers,
            # Copy the body into a JS-owned buffer: a to_js view into Python's
            # WASM memory read later — after the GIL is released — is a
            # runtime crash ("Attempted to use PyProxy when Python GIL not
            # held"), not an exception.
            "body": Uint8Array.new(_to_js(body)),
        })))

    async def _sticky_clone_fallback(self, owner, repo, refresh):
        # Which mirror serves owner/repo's clones while its named node is down.
        # Sticky per repo for CLONE_STICKY_MS: git's info/refs and upload-pack
        # POST are separate requests that must hit the SAME node (the pack is
        # negotiated against the refs the first request advertised), so per-
        # request rotation is replaced by a pinned pick that rotates only when
        # it expires. `refresh` is True on info/refs (a new clone may re-pick);
        # the POST reuses whatever pick exists, however old, as long as that
        # node is still live. Best-effort: any failure returns None and the
        # request just falls through to the named owner's route.
        try:
            await ensure_schema(self.env)
            now = int(Date.now())
            repo_bi = await blind_index(self.env, owner + "/" + repo)
            row = await d1_first(
                self.env,
                "SELECT owner, ts FROM clone_sticky WHERE repo_bi=?", repo_bi)
            pick = str((row or {}).get("owner") or "")
            fresh = bool(row) and now - int(row.get("ts") or 0) <= CLONE_STICKY_MS
            if pick and (fresh or not refresh):
                if await self._source_has_live_host(pick, repo):
                    return pick
            # No usable pick: choose a live mirror. force=True because the
            # caller already confirmed the named node has no connected host —
            # a fresh-but-stale presence row must not veto the fallback.
            choice = await self._select_clone_fallback(owner, repo, force=True)
            if choice and refresh:
                await d1_run(
                    self.env,
                    "INSERT INTO clone_sticky (repo_bi, owner, ts) VALUES (?,?,?) "
                    "ON CONFLICT(repo_bi) DO UPDATE SET "
                    "owner=excluded.owner, ts=excluded.ts",
                    repo_bi, choice, now,
                )
            return choice
        except Exception:
            return None

    async def _fresh_clone_pin(self, owner, repo):
        # The mirror a recent info/refs failed over to and pinned in clone_sticky,
        # if that pin is still fresh (<= CLONE_STICKY_MS) and the mirror still has
        # a live host. Read-only: never selects, rotates or writes a pin — used by
        # the upload-pack POST to follow whatever info/refs advertised, even when
        # the named source's own tunnel has since come back. Best-effort; any
        # failure (and the common no-pin case) returns None so the POST falls
        # through to the named source.
        try:
            await ensure_schema(self.env)
            now = int(Date.now())
            repo_bi = await blind_index(self.env, owner + "/" + repo)
            row = await d1_first(
                self.env,
                "SELECT owner, ts FROM clone_sticky WHERE repo_bi=?", repo_bi)
            if not row:
                return None
            pick = str(row.get("owner") or "")
            fresh = now - int(row.get("ts") or 0) <= CLONE_STICKY_MS
            if pick and fresh and pick.lower() != owner.lower():
                if await self._source_has_live_host(pick, repo):
                    return pick
            return None
        except Exception:
            return None


async def chat_history_recent(env, room_key):
    # The last few days of retained (encrypted) messages for a room, oldest
    # first, so a joining client can replay them in order.
    await ensure_schema(env)
    cutoff = int(Date.now()) - CHAT_HISTORY_RETAIN_MS
    rows = await d1_all(
        env,
        "SELECT body FROM chat_history WHERE room_key=? AND ts>=? "
        "ORDER BY ts ASC, msg_id ASC LIMIT ?",
        room_key, cutoff, CHAT_HISTORY_MAX_PER_ROOM,
    )
    return [str(r["body"]) for r in rows]


async def chat_history_store(env, room_key, msg_id, ts, body):
    await ensure_schema(env)
    await d1_run(
        env,
        "INSERT INTO chat_history (room_key, msg_id, ts, body) VALUES (?, ?, ?, ?) "
        "ON CONFLICT(room_key, msg_id) DO NOTHING",
        room_key, msg_id, ts, body,
    )


async def chat_history_prune(env, room_key):
    # Drop anything past the retention window, then enforce the per-room cap by
    # keeping only the newest CHAT_HISTORY_MAX_PER_ROOM rows.
    cutoff = int(Date.now()) - CHAT_HISTORY_RETAIN_MS
    await d1_run(env, "DELETE FROM chat_history WHERE room_key=? AND ts<?",
                 room_key, cutoff)
    await d1_run(
        env,
        "DELETE FROM chat_history WHERE room_key=? AND msg_id NOT IN ("
        "SELECT msg_id FROM chat_history WHERE room_key=? "
        "ORDER BY ts DESC LIMIT ?)",
        room_key, room_key, CHAT_HISTORY_MAX_PER_ROOM,
    )


async def chat_history_prune_expired(env):
    # chat_history_prune() above only runs when a client joins that specific
    # room, so a room nobody reconnects to would otherwise retain messages
    # past the 7-day window forever. Sweep every room's expired rows on the
    # per-minute cron (adhoc #49) so retention is enforced regardless of
    # traffic.
    await ensure_schema(env)
    cutoff = int(Date.now()) - CHAT_HISTORY_RETAIN_MS
    await d1_run(env, "DELETE FROM chat_history WHERE ts<?", cutoff)


class ForkMeshRoom(DurableObject):
    # Encrypted chat relay on the WebSocket Hibernation API. Sockets are accepted
    # via ctx.acceptWebSocket and serviced by the webSocketMessage/Close handlers
    # below, so the Durable Object is evicted from memory while a room is idle and
    # bills ~no duration. Connection state lives in the runtime
    # (ctx.getWebSockets("chat")), never in instance attributes — those would not
    # survive eviction. The read-only "observer" count socket was retired: the
    # live count is now served over HTTP via the cached /api/network/stats.
    async def fetch(self, request):
        path = urlparse(request.url).path
        upgrade = request.headers.get("upgrade")
        is_websocket = bool(upgrade) and upgrade.lower() == "websocket"

        if not is_websocket:
            # One-shot snapshot of the live client count (used by network_stats).
            return json_response({"ok": True, "clients": self._client_count()})

        if path.endswith("/clients"):
            return json_response({"error": "observers_removed"}, status=410)

        if self._client_count() >= MAX_CONNECTIONS:
            return json_response({"error": "room_full"}, status=429)

        # The room key identifies this room across hibernation; stash it on the
        # socket so webSocketMessage can scope retained history to this room.
        info = room_key_from_path(path)
        room_key = info["key"] if info else None

        client, server = WebSocketPair.new().object_values()
        self.ctx.acceptWebSocket(server, to_js(["chat"]))
        server.serializeAttachment(to_js({"id": new_socket_id(), "room": room_key}))

        # Replay retained (still-encrypted) history to the joining client so new
        # users see some recent backlog even when no peer is online to send it.
        if room_key:
            try:
                await chat_history_prune(self.env, room_key)
                for body in await chat_history_recent(self.env, room_key):
                    try:
                        server.send(body)
                    except Exception:
                        pass
            except Exception:
                pass

        return JsResponse.new(None, to_js({"status": 101, "webSocket": client}))

    def _client_count(self):
        try:
            return len(self.ctx.getWebSockets("chat"))
        except Exception:
            return 0

    def _rate_ok(self, ws):
        # Per-socket token bucket kept in the hibernation attachment: at most
        # ROOM_MSG_MAX_PER_WINDOW frames per ROOM_MSG_WINDOW_MS. Returns False when
        # the sender is over budget (the frame should be dropped).
        now = int(Date.now())
        try:
            start = int(_ws_attr(ws, "rl_start", 0) or 0)
            count = int(_ws_attr(ws, "rl_count", 0) or 0)
        except (TypeError, ValueError):
            start, count = 0, 0
        if now - start >= ROOM_MSG_WINDOW_MS:
            start, count = now, 0
        count += 1
        try:
            ws.serializeAttachment(to_js({
                "id": _ws_attr(ws, "id"), "room": _ws_attr(ws, "room"),
                "rl_start": start, "rl_count": count,
            }))
        except Exception:
            pass
        return count <= ROOM_MSG_MAX_PER_WINDOW

    async def webSocketMessage(self, ws, message):
        if not isinstance(message, str):
            self._safe_close(ws, 1003, "text frames only")
            return
        if len(message.encode("utf-8")) > MAX_TEXT_BYTES:
            self._safe_close(ws, 1009, "message too large")
            return
        # Drop frames from a socket that is flooding (don't relay or retain them).
        if not self._rate_ok(ws):
            return
        sender_id = _ws_attr(ws, "id")
        for peer in self.ctx.getWebSockets("chat"):
            if _ws_attr(peer, "id") == sender_id:
                continue
            try:
                peer.send(message)
            except Exception:
                pass

        # Retain durable conversation messages (still encrypted) for late joiners.
        await self._maybe_retain(ws, message)

    async def _maybe_retain(self, ws, message):
        room_key = _ws_attr(ws, "room")
        if not room_key:
            return
        if len(message.encode("utf-8")) > CHAT_HISTORY_MAX_BODY:
            return  # don't retain very large frames (e.g. file transfers)
        try:
            envelope = json.loads(message)
        except Exception:
            return
        if not (isinstance(envelope, dict) and envelope.get("persist")):
            return
        try:
            # The message id lives inside the ciphertext, so key retention on a
            # hash of the opaque frame (dedupes accidental re-relays).
            msg_id = (await sha256_hex(message))[:40]
            await chat_history_store(self.env, room_key, msg_id,
                                     int(Date.now()), message)
        except Exception:
            pass

    async def webSocketClose(self, ws, code, reason, was_clean):
        self._safe_close(ws, 1000, "")

    async def webSocketError(self, ws, error):
        return

    # Hibernation events may be dispatched under either naming convention.
    web_socket_message = webSocketMessage
    web_socket_close = webSocketClose
    web_socket_error = webSocketError

    def _safe_close(self, ws, code, reason):
        try:
            ws.close(code, reason)
        except Exception:
            pass


class ForkMeshHost(DurableObject):
    # Live tunnel for a single repository. Desktop clients that mirror the repo
    # connect here as "hosts" and answer tree/blob requests by reading their
    # local Git mirror. The website's /tree and /blob calls are forwarded to the
    # host with the best (lowest round-trip) connection. Nothing is stored: when
    # no host is connected, the repo is simply unavailable.
    # On the WebSocket Hibernation API: host sockets are accepted via
    # ctx.acceptWebSocket(["host"]) and the live set is read from
    # ctx.getWebSockets("host"), so an idle-but-connected host no longer pins the
    # Durable Object in memory. Per-host round-trip time rides in the socket's
    # hibernation attachment. The pending/git_buffers maps are in-memory and only
    # ever hold an *in-flight* request, which keeps the DO active (no eviction
    # mid-request), so they need not survive hibernation.
    def _ensure(self):
        if not hasattr(self, "pending"):
            self.pending = {}  # reqId -> asyncio.Future
        if not hasattr(self, "counter"):
            self.counter = 0
        if not hasattr(self, "git_buffers"):
            self.git_buffers = {}  # reqId -> bytearray for chunked git output
        if not hasattr(self, "git_streams"):
            # reqId -> {"writer": TransformStream writer, "last": ms}. Large
            # transfers (upload-pack packs, release assets, raw blobs) stream
            # each chunk straight into the response body instead of
            # reassembling in git_buffers: buffering a full clone's pack (plus
            # its base64 and bytes() copies) is what blew the Durable Object's
            # 128 MB memory limit and reset the whole isolate — killing every
            # unrelated in-flight request with it.
            self.git_streams = {}
        if not hasattr(self, "_repo_bi"):
            self._repo_bi = None   # blind index of this DO's owner/repo
        if not hasattr(self, "_last_presence"):
            self._last_presence = 0  # ms of last host_presence write (throttle)
        if not hasattr(self, "_req_window"):
            self._req_window = 0   # start ms of the current rate window
        if not hasattr(self, "_req_count"):
            self._req_count = 0    # requests served in the current window

    def _rate_ok(self):
        # Generous per-repo request rate limit on the tunnel/git proxy to blunt
        # floods without breaking real clones (which make a handful of requests).
        now = int(Date.now())
        if now - self._req_window >= HOST_RATE_WINDOW_MS:
            self._req_window = now
            self._req_count = 0
        self._req_count += 1
        return self._req_count <= HOST_RATE_MAX_PER_WINDOW

    async def _repo_blind_index(self, path):
        # This DO is per-repo; derive its blind index from the request path once
        # and cache it. Works for both /api/repo/{o}/{r}/* and git /{o}/{r}/* URLs.
        if self._repo_bi:
            return self._repo_bi
        match = (REPO_HOST_RE.match(path) or GIT_INFO_RE.match(path)
                 or GIT_PACK_RE.match(path) or RELEASE_BLOB_RE.match(path))
        if not match:
            return None
        owner = safe_segment(match.group(1))
        repo = safe_segment(match.group(2))
        if not owner or not repo:
            return None
        # Blocked phantom identities never register host presence (so they can't
        # appear "live" under /network/), but are still routed normally.
        self._blocked_presence = _is_blocked_catalog_identity(
            self.env, owner, repo)
        self._repo_bi = await blind_index(self.env, owner + "/" + repo)
        return self._repo_bi

    async def _mark_present(self, path=None):
        # Refresh this repo's host-presence row, throttled so hot browse traffic
        # doesn't write to D1 on every request. Best-effort; never fails the call.
        #
        # Only ever mark presence while a host WebSocket is actually connected to
        # this DO. Otherwise a repo whose desktop host has gone offline would be
        # kept "live" forever by the very browse/clone traffic that can't be
        # served: each request self-refreshes host_presence, so `liveHost`/
        # `source_online` never age out, `_select_clone_fallback` refuses to
        # redirect ("never redirect away from an online source"), and the online
        # mirror is never used — the repo shows "host online" yet nothing serves
        # its tree. Letting presence lapse when no host is connected is what lets
        # the source of truth age out so a live mirror takes over (adhoc #68).
        if not self._host_count():
            return
        now = int(Date.now())
        if now - self._last_presence < HOST_PRESENCE_REFRESH_MS:
            return
        repo_bi = await self._repo_blind_index(path) if path else self._repo_bi
        if not repo_bi:
            return
        if getattr(self, "_blocked_presence", False):
            return  # blocked phantom identity — never mark live
        self._last_presence = now
        try:
            await touch_host_presence(self.env, repo_bi)
        except Exception:
            pass

    async def fetch(self, request):
        self._ensure()
        url = urlparse(request.url)
        path = url.path
        upgrade = request.headers.get("upgrade")
        is_websocket = bool(upgrade) and upgrade.lower() == "websocket"

        # Rate-limit the request-serving paths (not the host's own WS upgrade,
        # which is already gated by a signed token in _route).
        if not is_websocket and not self._rate_ok():
            return json_response({"error": "rate_limited"}, status=429)

        # Git smart-HTTP clone endpoints proxied to the host's git upload-pack.
        if path.endswith("/info/refs"):
            await self._mark_present(path)
            service = parse_qs(url.query).get("service", [""])[0]
            if service == "git-receive-pack":
                # Push ref advertisement (issue #358); auth already enforced by
                # the router's _git_push before it reached this DO.
                return await self._git(request, "git-receive-info-refs")
            return await self._git(request, "git-info-refs")
        if path.endswith("/git-upload-pack"):
            await self._mark_present(path)
            body = b""
            try:
                raw_body = bytes(await request.bytes())
                body = decode_git_request_body(
                    raw_body, request.headers.get("content-encoding") or ""
                )
            except ValueError as error:
                return Response("Invalid Git request: " + str(error), status=400)
            except Exception:
                return Response("Could not read Git request body.", status=400)
            return await self._git(request, "git-upload-pack", body)
        if path.endswith("/git-receive-pack"):
            # git push (issue #358): the request body IS the pack (client->host),
            # potentially hundreds of MB, so it must never be buffered in the
            # isolate. _git streams it straight through the tunnel to the host's
            # receive-pack stdin (see _stream_receive / _pump_request_body).
            await self._mark_present(path)
            return await self._git(request, "git-receive-pack")

        action = path.rsplit("/", 1)[-1]
        if action == "host":
            if not is_websocket:
                return json_response({"ok": True, "hosts": self._host_count()})
            client, server = WebSocketPair.new().object_values()
            self.ctx.acceptWebSocket(server, to_js(["host"]))
            # Stash the repo blind index on the socket so a "ping" heartbeat can
            # refresh host presence even after the Durable Object hibernated (when
            # the cached self._repo_bi has been reset).
            repo_bi = await self._repo_blind_index(path)
            server.serializeAttachment(to_js({"rtt": None, "repo_bi": repo_bi}))
            await self._mark_present(path)
            return JsResponse.new(
                None, to_js({"status": 101, "webSocket": client})
            )

        release_blob_match = RELEASE_BLOB_RE.match(path)
        if release_blob_match:
            await self._mark_present(path)
            repo_bi = await self._repo_blind_index(path)
            return await self._release_blob(release_blob_match.group(3), repo_bi)

        rel_path = (parse_qs(url.query).get("path", [""])[0] or "").strip()
        ref = (parse_qs(url.query).get("ref", [""])[0] or "").strip()
        if action == "raw":
            await self._mark_present(path)
            return await self._raw_blob(rel_path, ref)
        if action == "blobs":
            # Batched blob read: one HTTP request returns up to MAX_BLOB_BATCH
            # files (repeated ?path= params), fanned out over the live tunnel
            # concurrently inside the DO. The website's issue/PR/discussion
            # lists used to fetch every record's markdown as its own /blob
            # request — 50+ parallel HTTP calls per page view, which tripped
            # the per-repo rate limit and hammered the host.
            await self._mark_present(path)
            paths = [p.strip() for p in parse_qs(url.query).get("path", [])
                     if p and p.strip()][:MAX_BLOB_BATCH]
            if not paths:
                return json_response({"error": "path_required"}, status=400)
            results = await asyncio.gather(
                *[self._tunnel_result("blob", p, ref) for p in paths])
            if results and all(not r.get("ok") for r in results):
                # Nothing could be served (host gone / tunnel dead). Surface it
                # as the request's status so the router's mirror fallback sees
                # the 503/504 and serves from a live mirror, instead of a 200
                # full of nulls pinning traffic to a dead node.
                stats = [int(r.get("_status") or 502) for r in results]
                status = 503 if 503 in stats else 504 if 504 in stats else 502
                return json_response(
                    {"ok": False, "error": "unavailable"}, status=status)
            blobs = {}
            for p, result in zip(paths, results):
                result.pop("_status", None)
                blobs[p] = result if result.get("ok") else None
            payload = {"ok": True, "blobs": blobs}
            served = REPO_HOST_RE.match(path)
            served_by = safe_segment(served.group(1)) if served else ""
            if served_by:
                payload["servedBy"] = served_by
            return json_response(payload)
        if action == "search":
            # Repo-scoped search (issue #360): the host runs one `git grep` over
            # its mirror and returns capped issue/PR/code matches. The query rides
            # as ?q=; the result buckets are already capped host-side, so this is a
            # single tunnel round-trip — no per-record fan-out.
            await self._mark_present(path)
            query = (parse_qs(url.query).get("q", [""])[0] or "").strip()[:100]
            if len(query) < 2:
                return json_response(
                    {"ok": False, "error": "query_too_short"}, status=400)
            served = REPO_HOST_RE.match(path)
            served_by = safe_segment(served.group(1)) if served else ""
            return await self._tunnel("search", query, ref, served_by)
        if action in ("tree", "blob", "history", "commit", "branches"):
            await self._mark_present(path)
            op = "commits" if action == "history" else action
            # This DO is per-repo, so the owner in its path IS the mirror node
            # answering the request (the router round-robins by redirecting to the
            # chosen node's route). Pass it through so the browse response can tell
            # the website which node served it.
            served = REPO_HOST_RE.match(path)
            served_by = safe_segment(served.group(1)) if served else ""
            return await self._tunnel(op, rel_path, ref, served_by)
        return json_response({"error": "not_found"}, status=404)

    def _host_count(self):
        try:
            return len(self.ctx.getWebSockets("host"))
        except Exception:
            return 0

    async def webSocketMessage(self, ws, message):
        self._ensure()
        if not isinstance(message, str):
            return
        try:
            msg = json.loads(message)
        except Exception:
            return
        if not isinstance(msg, dict):
            return
        mtype = msg.get("type")
        if mtype == "heartbeat":
            # Control-frame pings keep the socket alive but do not dispatch a
            # message event. The desktop host therefore sends this lightweight
            # application heartbeat too. Restore the repo key from the socket
            # attachment after DO hibernation, then refresh D1 presence.
            repo_bi = _ws_attr(ws, "repo_bi")
            if repo_bi:
                self._repo_bi = repo_bi
                await self._mark_present()
        elif mtype == "response":
            req_id = msg.get("reqId")
            # A plain error response can arrive for a request we expected to
            # stream (e.g. the host rejects the op) — tear the stream down so
            # the client isn't left waiting on a body that will never come.
            stream = self.git_streams.pop(req_id, None)
            if stream is not None:
                try:
                    await stream["writer"].abort("host error")
                except Exception:
                    pass
            fut = self.pending.pop(req_id, None)
            if fut is not None and not fut.done():
                fut.set_result(msg)
        elif mtype == "git-chunk":
            req_id = msg.get("reqId")
            stream = self.git_streams.get(req_id)
            if stream is not None:
                # First chunk doubles as the "host is answering" signal the
                # request handler awaits before returning the streamed 200.
                fut = self.pending.pop(req_id, None)
                if fut is not None and not fut.done():
                    fut.set_result({"ok": True})
                try:
                    data = base64.b64decode(msg.get("data", ""))
                except Exception:
                    data = b""
                if data:
                    stream["last"] = int(Date.now())
                    try:
                        # Copy into a JS-owned buffer before it crosses into the
                        # response stream: a view into Python's WASM memory read
                        # later, off the GIL, is a runtime crash.
                        await stream["writer"].write(
                            Uint8Array.new(_to_js(data)))
                    except Exception:
                        # Reader hung up (client aborted the download): drop the
                        # stream so remaining chunks fall on the floor.
                        self.git_streams.pop(req_id, None)
                return
            buffer = self.git_buffers.get(req_id)
            if buffer is not None:
                try:
                    buffer += base64.b64decode(msg.get("data", ""))
                except Exception:
                    pass
        elif mtype == "git-end":
            req_id = msg.get("reqId")
            stream = self.git_streams.pop(req_id, None)
            if stream is not None:
                fut = self.pending.pop(req_id, None)
                if fut is not None and not fut.done():
                    fut.set_result({"ok": bool(msg.get("ok")),
                                    "error": msg.get("error")})
                try:
                    if msg.get("ok"):
                        await stream["writer"].close()
                    else:
                        # Failure after bytes already streamed: the 200 headers
                        # are gone, so abort the body — the client sees a
                        # truncated transfer and fails cleanly.
                        await stream["writer"].abort(
                            str(msg.get("error") or "host error"))
                except Exception:
                    pass
                return
            buffer = self.git_buffers.pop(req_id, None)
            fut = self.pending.pop(req_id, None)
            if fut is not None and not fut.done():
                fut.set_result(
                    {
                        "ok": bool(msg.get("ok")),
                        "error": msg.get("error"),
                        "data": bytes(buffer or b""),
                    }
                )

    async def webSocketClose(self, ws, code, reason, was_clean):
        await self._host_disconnected(ws)

    async def webSocketError(self, ws, error):
        await self._host_disconnected(ws)

    async def _host_disconnected(self, ws):
        # The last host socket going away means nothing can serve this repo, so
        # expire its host_presence row NOW instead of letting it linger up to
        # HOST_PRESENCE_STALE_MS — that stale window is what kept browse/clone
        # traffic routed at a dead source while a live mirror sat unused. The
        # offline notification is deduped (kind:repo_bi), so a reconnect blip
        # doesn't spam. Best-effort: an unclean death that skips this handler is
        # still covered by the router's no-host retry to a live mirror.
        self._ensure()
        if self._live_host_count(skip=ws) > 0:
            return
        # Whatever this host was mid-way through streaming can never finish;
        # abort those bodies so downloading clients fail fast instead of
        # hanging until the stall watchdog fires.
        for req_id in list(self.git_streams.keys()):
            stream = self.git_streams.pop(req_id, None)
            if stream is not None:
                try:
                    await stream["writer"].abort("host disconnected")
                except Exception:
                    pass
        repo_bi = _ws_attr(ws, "repo_bi") or self._repo_bi
        if not repo_bi:
            return
        self._last_presence = 0
        try:
            await ensure_schema(self.env)
            await d1_run(
                self.env, "DELETE FROM host_presence WHERE repo_bi=?", repo_bi)
            await notify_host_status(self.env, repo_bi, "host_offline")
        except Exception:
            pass

    def _live_host_count(self, skip=None):
        # Host sockets that are still actually open. During a close/error event
        # the runtime may still list the dying socket, so filter it (and anything
        # no longer OPEN) out rather than trusting the raw tag count.
        count = 0
        for socket in self.ctx.getWebSockets("host"):
            if skip is not None and socket == skip:
                continue
            try:
                if int(socket.readyState) != 1:
                    continue
            except Exception:
                pass
            count += 1
        return count

    # Hibernation events may be dispatched under either naming convention.
    web_socket_message = webSocketMessage
    web_socket_close = webSocketClose
    web_socket_error = webSocketError

    def _best_host(self):
        # Pick the live host with the lowest measured round-trip time (stored in
        # each socket's hibernation attachment); unmeasured hosts sort last.
        best = None
        best_score = None
        for ws in self.ctx.getWebSockets("host"):
            rtt = _ws_attr(ws, "rtt")
            score = rtt if isinstance(rtt, (int, float)) else float("inf")
            if best is None or score < best_score:
                best, best_score = ws, score
        return best

    async def _tunnel_result(self, op, rel_path, ref=""):
        # One request over the live tunnel, returned as a payload dict rather
        # than a Response so callers can batch several reads into one HTTP
        # response (see the /blobs action). Failures carry the HTTP status
        # they'd map to under "_status"; _tunnel pops it before responding.
        self._ensure()
        host = self._best_host()
        if host is None:
            return {"ok": False, "error": "no_host", "_status": 503}

        self.counter += 1
        req_id = "r%d" % self.counter
        loop = asyncio.get_event_loop()
        future = loop.create_future()
        self.pending[req_id] = future
        started = Date.now()
        try:
            host.send(
                json.dumps({"type": "request", "reqId": req_id, "op": op,
                            "path": rel_path, "ref": ref})
            )
        except Exception:
            self.pending.pop(req_id, None)
            self._drop_host(host)
            return {"ok": False, "error": "host_unavailable", "_status": 503}

        try:
            msg = await asyncio.wait_for(future, timeout=TUNNEL_TIMEOUT_MS / 1000)
        except Exception:
            self.pending.pop(req_id, None)
            return {"ok": False, "error": "timeout", "_status": 504}

        self._update_rtt(host, Date.now() - started)

        if not msg.get("ok"):
            return {"ok": False, "error": msg.get("error", "host_error"),
                    "_status": 502}
        payload = {
            key: value
            for key, value in msg.items()
            if key not in ("type", "reqId", "ok")
        }
        payload["ok"] = True
        return payload

    async def _tunnel(self, op, rel_path, ref="", served_by=""):
        result = await self._tunnel_result(op, rel_path, ref)
        status = int(result.pop("_status", 200) or 200)
        if not result.get("ok"):
            return json_response(result, status=status)
        if served_by:
            # The mirror node that answered, so the website can show a
            # "served by <node>" note confirming the round-robin is working.
            result["servedBy"] = served_by
        return json_response(result)

    async def _stream_request(self, host, message, headers):
        # Send a chunked-transfer request to the host and return a Response
        # whose body STREAMS the reply: each git-chunk is written straight
        # through a TransformStream to the client as it arrives, so a full
        # clone's pack or a large release asset never accumulates in DO memory
        # (reassembling one is what used to exceed the 128 MB isolate limit and
        # reset it, killing every in-flight request). Waits GIT_TIMEOUT_MS for
        # the host's FIRST sign of life (chunk, end, or error); after that a
        # per-chunk watchdog aborts a mid-stream stall so a dying host fails
        # the one transfer instead of hanging the client.
        #
        # Returns (response, error_result): exactly one is non-None. A
        # transport failure yields a ready error Response; a host-reported
        # error yields the result dict so each endpoint maps its own status.
        req_id = message["reqId"]
        loop = asyncio.get_event_loop()
        future = loop.create_future()
        self.pending[req_id] = future
        transform = TransformStream.new()
        writer = transform.writable.getWriter()
        self.git_streams[req_id] = {"writer": writer, "last": int(Date.now())}
        try:
            host.send(json.dumps(message))
        except Exception:
            self.pending.pop(req_id, None)
            self.git_streams.pop(req_id, None)
            self._drop_host(host)
            return Response("Host unavailable.", status=503), None
        try:
            result = await asyncio.wait_for(
                future, timeout=GIT_TIMEOUT_MS / 1000)
        except Exception:
            self.pending.pop(req_id, None)
            if self.git_streams.pop(req_id, None) is not None:
                try:
                    await writer.abort("host timed out")
                except Exception:
                    pass
            return Response("Host timed out.", status=504), None
        if not result.get("ok"):
            if self.git_streams.pop(req_id, None) is not None:
                try:
                    await writer.abort("host error")
                except Exception:
                    pass
            return None, result
        asyncio.ensure_future(self._stream_watchdog(req_id))
        return JsResponse.new(
            transform.readable,
            to_js({"status": 200, "headers": headers}),
        ), None

    async def _stream_watchdog(self, req_id):
        # Abort a streamed transfer whose host went quiet mid-body, so the
        # client sees a truncated transfer promptly instead of hanging (and the
        # stream doesn't pin the Durable Object forever). The happy path — the
        # stream already closed/errored and left git_streams — just exits.
        while True:
            await asyncio.sleep(GIT_TIMEOUT_MS / 1000)
            stream = self.git_streams.get(req_id)
            if stream is None:
                return
            if int(Date.now()) - stream["last"] > GIT_TIMEOUT_MS:
                self.git_streams.pop(req_id, None)
                try:
                    await stream["writer"].abort("host stalled mid-stream")
                except Exception:
                    pass
                return

    async def _stream_receive(self, host, message, headers, request):
        # git push (issue #358): the mirror image of _stream_request. There the
        # big body is the REPLY (a clone's pack); here it is the REQUEST (the
        # pushed pack), so the incoming body is pumped to the host chunk by chunk
        # (_pump_request_body) and NEVER reassembled in the isolate — buffering a
        # pack here is exactly what OOM'd the DO once. The host runs receive-pack
        # and streams its small report-status reply back through the same
        # git-chunk/git-end path a clone uses.
        req_id = message["reqId"]
        loop = asyncio.get_event_loop()
        future = loop.create_future()
        self.pending[req_id] = future
        transform = TransformStream.new()
        writer = transform.writable.getWriter()
        self.git_streams[req_id] = {"writer": writer, "last": int(Date.now())}
        try:
            host.send(json.dumps(message))
            # Forward the pushed pack, then tell the host to close stdin so
            # receive-pack can finish and reply. The host must set up the process
            # on the "request" message above before these chunks arrive; WS
            # message order guarantees it.
            await self._pump_request_body(host, req_id, request)
        except Exception:
            self.pending.pop(req_id, None)
            if self.git_streams.pop(req_id, None) is not None:
                try:
                    await writer.abort("host unavailable")
                except Exception:
                    pass
            self._drop_host(host)
            return Response("Host unavailable.", status=503), None
        try:
            result = await asyncio.wait_for(
                future, timeout=GIT_TIMEOUT_MS / 1000)
        except Exception:
            self.pending.pop(req_id, None)
            if self.git_streams.pop(req_id, None) is not None:
                try:
                    await writer.abort("host timed out")
                except Exception:
                    pass
            return Response("Host timed out.", status=504), None
        if not result.get("ok"):
            if self.git_streams.pop(req_id, None) is not None:
                try:
                    await writer.abort("host error")
                except Exception:
                    pass
            return None, result
        asyncio.ensure_future(self._stream_watchdog(req_id))
        return JsResponse.new(
            transform.readable,
            to_js({"status": 200, "headers": headers}),
        ), None

    async def _pump_request_body(self, host, req_id, request):
        # Read the incoming request body as a stream and relay it to the host as
        # git-req-chunk messages (base64, capped at GIT_REQ_CHUNK like the host's
        # own git-chunk replies), ending with git-req-end so the host closes
        # receive-pack's stdin. Reading via getReader() keeps a large push pack
        # from ever sitting whole in DO memory.
        body = getattr(request, "body", None)
        if body is not None:
            reader = body.getReader()
            while True:
                piece = await reader.read()
                if piece.done:
                    break
                value = getattr(piece, "value", None)
                if value is None:
                    continue
                # Copy the JS Uint8Array into Python bytes synchronously (a copy,
                # not a view) before encoding — the reverse direction is the one
                # that must round-trip through Uint8Array.new to stay GIL-safe.
                data = bytes(value.to_py())
                for i in range(0, len(data), GIT_REQ_CHUNK):
                    host.send(json.dumps({
                        "type": "git-req-chunk", "reqId": req_id,
                        "data": base64.b64encode(
                            data[i:i + GIT_REQ_CHUNK]).decode(),
                    }))
        host.send(json.dumps({"type": "git-req-end", "reqId": req_id}))

    async def _release_blob(self, sha256, repo_bi=None):
        # Stream a content-addressed release asset from a serving node: each
        # chunk flows straight to the client (see _stream_request), so
        # arbitrarily large binaries download without the 4 MB inline /blob cap
        # and without ever holding the whole asset in DO memory. Still cached
        # forever at the edge — the URL is content-addressed — and the client
        # verifies the bytes against the manifest sha256 (x-content-sha256).
        if not valid_sha256_hex(sha256):
            return Response("not found", status=404)
        self._ensure()
        host = self._best_host()
        if host is None:
            return Response(
                "No host is currently serving this release.", status=503)
        self.counter += 1
        req_id = "r%d" % self.counter
        response, err = await self._stream_request(
            host,
            {"type": "request", "reqId": req_id,
             "op": "release-blob", "path": sha256.lower()},
            {
                "content-type": "application/octet-stream",
                "cache-control": "public, max-age=31536000, immutable",
                "x-content-sha256": sha256.lower(),
            },
        )
        if response is not None:
            if repo_bi:
                # Fire-and-forget: log the download without delaying the
                # already-streaming response on a D1 round-trip.
                asyncio.ensure_future(
                    record_release_download(self.env, repo_bi, sha256.lower()))
            return response
        status = 404 if str(err.get("error", "")) in (
            "not_found", "bad_hash") else 502
        return Response("Release asset unavailable.", status=status)

    async def _raw_blob(self, rel_path, ref=""):
        # Stream a repository blob from git as bytes so browser-native previews
        # can load media without the capped JSON/base64 /blob response — chunk
        # by chunk (see _stream_request), so a large asset never sits whole in
        # DO memory.
        if not rel_path or "\x00" in rel_path:
            return Response("not found", status=404)
        self._ensure()
        host = self._best_host()
        if host is None:
            return Response("No host is currently serving this file.", status=503)
        self.counter += 1
        req_id = "r%d" % self.counter
        response, err = await self._stream_request(
            host,
            {"type": "request", "reqId": req_id,
             "op": "raw-blob", "path": rel_path, "ref": ref},
            {
                "content-type": repo_blob_content_type(rel_path),
                "cache-control": "no-cache, max-age=0, must-revalidate",
                "content-disposition":
                    'inline; filename="%s"' % repo_blob_filename(rel_path),
                "x-content-type-options": "nosniff",
                "content-security-policy": "sandbox",
            },
        )
        if response is not None:
            return response
        status = 404 if str(err.get("error", "")) in (
            "not_found", "bad_path") else 502
        return Response("File unavailable.", status=status)

    def _drop_host(self, ws):
        # A send failed: force-close so the runtime drops it from getWebSockets.
        try:
            ws.close(1011, "host unavailable")
        except Exception:
            pass

    def _update_rtt(self, ws, elapsed):
        # Exponential moving average of round-trip time, persisted in the socket's
        # hibernation attachment so it survives a Durable Object eviction.
        prev = _ws_attr(ws, "rtt")
        new_rtt = (elapsed if not isinstance(prev, (int, float))
                   else 0.5 * prev + 0.5 * elapsed)
        try:
            # Preserve repo_bi when rewriting the attachment. serializeAttachment
            # replaces the whole blob, so omitting repo_bi here loses it after the
            # first successful tunnel response. The heartbeat handler reads repo_bi
            # from the attachment after DO hibernation to restore self._repo_bi and
            # keep host_presence fresh; without it, heartbeats silently stop
            # refreshing presence and the mirror ages out in HOST_PRESENCE_STALE_MS.
            ws.serializeAttachment(
                to_js({"rtt": new_rtt, "repo_bi": _ws_attr(ws, "repo_bi")}))
        except Exception:
            pass

    async def _state_pins(self, path):
        # The set of acceptable repo-state hashes for the node serving this clone
        # path, or None when the repo is unpinned. A working-copy holder is
        # checked against its own attestations; a MIRROR must match a pin some
        # working-copy holder in its logical-repo group signed — its own
        # self-attested hash proves nothing (a tampered mirror can republish a
        # matching pin at will). Selection logic lives in the pure
        # clone_state_pins(); this wrapper gathers the catalog rows + recent pin
        # history it needs. Best-effort: any failure returns None (fail-open),
        # matching the pre-existing behaviour for unreadable records.
        match = GIT_INFO_RE.match(path) or GIT_PACK_RE.match(path)
        if not match:
            return None
        owner = safe_segment(match.group(1))
        repo = safe_segment(match.group(2))
        if not owner or not repo:
            return None
        try:
            await ensure_schema(self.env)
            key_bi = await blind_index(self.env, owner + "/" + repo)
            row = await d1_first(
                self.env, "SELECT data FROM repositories WHERE key_bi=?", key_bi)
            if not row:
                return None
            target = await decrypt_row(self.env, row["data"])
            if not target:
                return None
            # Fast path: the serving node holds the working copy — no need to
            # scan the catalog for group members.
            source = str(target.get("source") or "local-node")
            catalog_rows = []
            if source != "local-node":
                rows = await d1_all(
                    self.env, "SELECT key_bi, data FROM repositories")
                for r in rows:
                    rec = await decrypt_row(self.env, r.get("data"))
                    if rec:
                        catalog_rows.append(
                            {"key_bi": r.get("key_bi"), "data": rec})
            history = {}
            hist_rows = await d1_all(
                self.env,
                "SELECT key_bi, state_hash FROM repo_state_history")
            for r in hist_rows:
                history.setdefault(str(r.get("key_bi") or ""), []).append(
                    r.get("state_hash"))
            return clone_state_pins(target, key_bi, catalog_rows, history)
        except Exception:
            return None

    async def _git(self, request, op, body=None):
        # Forward a git smart-HTTP request to the hosting client, which runs
        # git upload-pack on its local mirror and streams the result back in
        # chunks. The upload-pack POST reply — the pack itself, hundreds of MB
        # for a big repo — streams straight through to the client
        # (_stream_request); only the small info/refs advertisement is still
        # reassembled here, because the integrity gate must hash the complete
        # advertisement before releasing it.
        # The push ops (issue #358) share this plumbing: git-receive-info-refs is
        # the receive-pack ref advertisement (buffered like info/refs, but no
        # integrity gate — pushing to the source, not cloning from a mirror), and
        # git-receive-pack streams the pushed pack through to the host.
        is_advertise = op in ("git-info-refs", "git-receive-info-refs")
        is_receive = op in ("git-receive-info-refs", "git-receive-pack")
        service = "git-receive-pack" if is_receive else "git-upload-pack"

        self._ensure()
        host = self._best_host()
        if host is None:
            # No desktop client is currently connected. Return a proper git
            # smart-HTTP error so git shows a readable message instead of a
            # confusing protocol error. For info/refs the response MUST use the
            # advertisement content-type and pkt-line encoding.
            err_msg = b"ERR no host is currently serving this repository\n"
            if is_advertise:
                body_out = (
                    pkt_line(("# service=%s\n" % service).encode()) + b"0000" +
                    pkt_line(err_msg)
                )
                return git_bytes_response(
                    body_out,
                    "application/x-%s-advertisement" % service,
                )
            return Response(
                "No client is hosting this repository.", status=503
            )

        self.counter += 1
        req_id = "g%d" % self.counter
        message = {"type": "request", "reqId": req_id, "op": op}
        if body:
            message["body"] = base64.b64encode(body).decode()

        if op == "git-receive-pack":
            # The request body (the pushed pack) streams straight to the host's
            # receive-pack stdin — never buffered in the isolate — and the small
            # report-status reply streams back.
            response, err = await self._stream_receive(
                host, message,
                {
                    "content-type": "application/x-git-receive-pack-result",
                    "cache-control": "no-cache, max-age=0, must-revalidate",
                },
                request,
            )
            if response is not None:
                return response
            return Response(
                "Host error: " + str(err.get("error", "")), status=502
            )

        if op == "git-upload-pack":
            response, err = await self._stream_request(
                host, message,
                {
                    "content-type": "application/x-git-upload-pack-result",
                    "cache-control": "no-cache, max-age=0, must-revalidate",
                },
            )
            if response is not None:
                return response
            return Response(
                "Host error: " + str(err.get("error", "")), status=502
            )

        loop = asyncio.get_event_loop()
        future = loop.create_future()
        self.pending[req_id] = future
        self.git_buffers[req_id] = bytearray()
        try:
            host.send(json.dumps(message))
        except Exception:
            self.pending.pop(req_id, None)
            self.git_buffers.pop(req_id, None)
            self._drop_host(host)
            return Response("Host unavailable.", status=503)

        try:
            result = await asyncio.wait_for(future, timeout=GIT_TIMEOUT_MS / 1000)
        except Exception:
            self.pending.pop(req_id, None)
            self.git_buffers.pop(req_id, None)
            return Response("Host timed out.", status=504)

        if not result.get("ok"):
            return Response(
                "Host error: " + str(result.get("error", "")), status=502
            )

        data = result.get("data", b"")
        # Tamper/rollback gate: the refs this node advertises must hash to a
        # state the repo's working-copy holder attested — a mirror is checked
        # against the SOURCE's signed pins (current + recent history), never
        # just its own self-published hash, so a forged or rolled-back mirror
        # fails here and no clone ever receives it. (Fails open when nothing
        # is pinned, e.g. a not-yet-attested repo; see clone_state_pins.) The
        # push advertisement (git-receive-info-refs) skips it: a push targets
        # the owner's own source node, not a mirror, so there is nothing to
        # attest against yet — the pin is re-attested AFTER the push lands.
        if op == "git-info-refs":
            pinned = await self._state_pins(urlparse(request.url).path)
            if (pinned and await sha256_hex(advertised_refs_canonical(data))
                    not in pinned):
                err = (b"ERR repository failed integrity check "
                       b"(mirror may be tampered or out of date)\n")
                body_out = (
                    pkt_line(b"# service=git-upload-pack\n") + b"0000" +
                    pkt_line(err)
                )
                return git_bytes_response(
                    body_out, "application/x-git-upload-pack-advertisement"
                )
        body_out = (
            pkt_line(("# service=%s\n" % service).encode()) + b"0000" + data
        )
        return git_bytes_response(
            body_out, "application/x-%s-advertisement" % service
        )
