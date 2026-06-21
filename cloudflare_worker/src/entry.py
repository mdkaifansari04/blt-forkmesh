import asyncio
import base64
import hmac
import json
import re
import struct
from urllib.parse import parse_qs, unquote, urlparse

from js import Date
from js import Object
from js import Response as JsResponse
from js import Uint8Array
from js import WebSocketPair
from js import caches as js_caches
from js import crypto as js_crypto
from pyodide.ffi import to_js as _to_js
from workers import DurableObject, Response, WorkerEntrypoint

import bch_wallet

MAX_ROOM_NAME = 80
MAX_REPO_SEGMENT = 80
MAX_CONNECTIONS = 128
MAX_TEXT_BYTES = 96 * 1024 * 1024
# Retained chat history (encrypted) so late-joining nodes see some backlog.
CHAT_HISTORY_RETAIN_MS = 7 * 24 * 60 * 60 * 1000  # keep the last 7 days
CHAT_HISTORY_MAX_PER_ROOM = 500  # hard cap on retained messages per room
CHAT_HISTORY_MAX_BODY = 48 * 1024  # don't retain very large frames (e.g. files)
MAX_CATALOG_REPOS = 200
MAX_ERROR_LOG = 500
MAX_FILES = 5000
# Issue inbox: a single signed event body is small text; cap it and the number
# of un-merged submissions a repo's inbox will hold.
MAX_ISSUE_BYTES = 64 * 1024
MAX_PENDING_ISSUES = 500
# Pull-request inbox: a PR carries a unified diff (text), capped larger than an
# issue body but still bounded.
MAX_PULL_BYTES = 1024 * 1024
MAX_PENDING_PULLS = 200
# Each room exposes a WebSocket (/ws) and a read-only live client count
# (/clients); the Durable Object picks behavior from the upgrade header.
ROOM_RE = re.compile(r"^/api/room/([^/]+)/(?:ws|clients)$")
REPO_ROOM_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/rooms/([^/]+)/(?:ws|clients)$")
# Issue inbox: signed submissions from people without write access to the repo.
REPO_ISSUES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/issues$")
# Pull-request inbox: signed PR submissions from any node.
REPO_PULLS_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/pulls$")
# Live tunnel: desktop clients connect to /host; the website pulls /tree and
# /blob, which the worker forwards to the best-connected host.
REPO_HOST_RE = re.compile(
    r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blob|commits|commit)$")
# Git smart-HTTP clone endpoints: git clone https://host/<node>/<repo>
GIT_INFO_RE = re.compile(r"^/([^/]+)/([^/]+)/info/refs$")
GIT_PACK_RE = re.compile(r"^/([^/]+)/([^/]+)/git-upload-pack$")
ROOM_NAME_RE = re.compile(r"^[A-Za-z0-9._:-]+$")
TUNNEL_TIMEOUT_MS = 20000
GIT_TIMEOUT_MS = 60000
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


# Public node name = username = a single DNS-like label: lowercase letters,
# digits, and hyphens; must start with a letter and end with a letter or digit;
# no underscores, no spaces, <= 63 chars. This is the user's public handle and
# their repo namespace, so it is validated identically in the web and desktop
# clients.
NODE_NAME_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
MAX_NODE_NAME = 63
ACCOUNTS_RE = re.compile(r"^/api/accounts/([^/]+)$")
LOGIN_MAX_SKEW_MS = 5 * 60 * 1000


def valid_node_name(value):
    value = (value or "").strip()
    return (bool(value) and len(value) <= MAX_NODE_NAME and
            bool(NODE_NAME_RE.match(value)))


def b64url_decode(value):
    value = (value or "").strip()
    value += "=" * (-len(value) % 4)
    return base64.urlsafe_b64decode(value.encode())


async def ed25519_verify(pubkey_b64url, sig_b64url, data_bytes):
    # Verify a raw Ed25519 signature using the runtime's WebCrypto, matching the
    # desktop client's identity (raw 32-byte key + 64-byte sig, base64url).
    try:
        raw_key = b64url_decode(pubkey_b64url)
        signature = b64url_decode(sig_b64url)
    except Exception:
        return False
    if len(raw_key) != 32 or len(signature) != 64:
        return False
    try:
        key = await js_crypto.subtle.importKey(
            "raw", _to_js(raw_key), to_js({"name": "Ed25519"}), False,
            _to_js(["verify"])
        )
        ok = await js_crypto.subtle.verify(
            to_js({"name": "Ed25519"}), key, _to_js(signature),
            _to_js(data_bytes)
        )
        return bool(ok)
    except Exception:
        return False


async def sha256_hex(text):
    digest = await js_crypto.subtle.digest("SHA-256", _to_js(text.encode()))
    return bytes(Uint8Array.new(digest).to_py()).hex()


def issue_event_content(ev):
    # Type-specific canonical content; MUST match the client's
    # IssueStore::contentForSigning and issues/README.md. Fields joined by NUL.
    t = ev.get("type", "")
    attachments = ",".join(ev.get("attachments") or [])
    if t == "open":
        return "\x00".join([ev.get("title", ""), ev.get("body", ""), attachments])
    if t in ("comment", "edit"):
        return "\x00".join([ev.get("body", ""), attachments])
    if t == "title":
        return ev.get("title", "")
    if t == "status":
        return ev.get("status", "")
    if t == "labels":
        return ",".join(ev.get("labels") or [])
    if t == "milestone":
        return ev.get("milestone", "") or ""
    if t == "assignees":
        return ",".join(ev.get("assignees") or [])
    if t == "delete":
        return ev.get("target", "")
    if t == "vote":
        return ""  # type+number+author+ts already bind the signed vote
    return ""


async def verify_issue_event(number, ev):
    author = ev.get("author", "")
    signature = ev.get("sig", "")
    event_type = ev.get("type", "")
    if not author or not signature or not event_type:
        return False
    try:
        ts = int(ev.get("ts", 0))
    except (TypeError, ValueError):
        return False
    content_hash = await sha256_hex(issue_event_content(ev))
    canonical = (
        "forkmesh-issue-event-v1\n" + event_type + "\n" + str(int(number)) + "\n" +
        author + "\n" + str(ts) + "\n" + content_hash
    ).encode()
    return await ed25519_verify(author, signature, canonical)


async def verify_pull_event(pr):
    # Mirrors PullStore::canonicalString: the signature commits to
    # title/base/head/patch (not the number, which the owner assigns on merge).
    author = pr.get("author", "")
    signature = pr.get("sig", "")
    if not author or not signature:
        return False
    try:
        ts = int(pr.get("ts", 0))
    except (TypeError, ValueError):
        return False
    content = "\x00".join([
        pr.get("title", ""), pr.get("base", ""), pr.get("head", ""),
        pr.get("patch", ""),
    ])
    content_hash = await sha256_hex(content)
    canonical = (
        "forkmesh-pull-event-v1\n" + author + "\n" + str(ts) + "\n" + content_hash
    ).encode()
    return await ed25519_verify(author, signature, canonical)


def pkt_line(payload):
    return ("%04x" % (len(payload) + 4)).encode() + payload


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


def json_response(data, status=200, cache_seconds=None):
    headers = {"content-type": "application/json; charset=utf-8"}
    if cache_seconds is not None:
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
CATALOG_TTL = 10  # seconds the repositories list is cached at the edge


async def touch_host_presence(env, repo_bi):
    # Mark a repo's tunnel as live (or refresh its timestamp). Called when a host
    # connects and, throttled, while it serves traffic.
    await ensure_schema(env)
    await d1_run(
        env,
        "INSERT INTO host_presence (repo_bi, ts) VALUES (?, ?) "
        "ON CONFLICT(repo_bi) DO UPDATE SET ts=excluded.ts",
        repo_bi, int(Date.now()),
    )


async def _flagship_client_count(env):
    # One internal request to the flagship room's count endpoint (not a WebSocket).
    try:
        room_id = env.FORKMESH_MAINNODE_ROOM.idFromName(FLAGSHIP_ROOM_KEY)
        room = env.FORKMESH_MAINNODE_ROOM.get(room_id)
        resp = await room.fetch(
            "https://forkmesh.internal/api/repo/mainnode/forkmesh/rooms/general/clients"
        )
        data = await resp.json()
        return int(getattr(data, "clients", 0) or 0)
    except Exception:
        return 0


async def network_stats(env):
    # Aggregate homepage stats: repo count (D1), live host count (presence table),
    # and flagship-room client count (one internal request). Cached at the edge.
    cached = await edge_cache_match(NETWORK_STATS_CACHE_KEY)
    if cached is not None:
        return cached

    await ensure_schema(env)
    repo_row = await d1_first(env, "SELECT COUNT(*) AS n FROM repositories")
    repos = int((repo_row or {}).get("n", 0) or 0)

    cutoff = int(Date.now()) - HOST_PRESENCE_STALE_MS
    try:
        await d1_run(env, "DELETE FROM host_presence WHERE ts < ?", cutoff)
    except Exception:
        pass
    host_row = await d1_first(
        env, "SELECT COUNT(*) AS n FROM host_presence WHERE ts >= ?", cutoff
    )
    hosts = int((host_row or {}).get("n", 0) or 0)

    clients = await _flagship_client_count(env)

    resp = json_response(
        {"ok": True, "repos": repos, "hosts": hosts, "clients": clients},
        cache_seconds=NETWORK_STATS_TTL,
    )
    await edge_cache_put(NETWORK_STATS_CACHE_KEY, resp)
    return resp


# --- Online-activity history (24h graph on /network/) -----------------------
# A per-minute cron records how many nodes are online into the current hour's
# bucket, so each hour's value is "node-minutes online" that hour.

ONLINE_SAMPLE_WINDOW_MS = 2 * 60 * 1000  # treat a node seen in the last 2 min as online
ONLINE_HISTORY_RETAIN_MS = 48 * 60 * 60 * 1000


async def record_online_sample(env):
    # Called once a minute by the scheduled (cron) handler.
    await ensure_schema(env)
    now = int(Date.now())
    row = await d1_first(
        env, "SELECT COUNT(*) AS n FROM account_presence WHERE ts >= ?",
        now - ONLINE_SAMPLE_WINDOW_MS,
    )
    online = int((row or {}).get("n", 0) or 0)
    hour_ts = (now // 3600000) * 3600000
    await d1_run(
        env,
        "INSERT INTO online_hourly (hour_ts, node_minutes) VALUES (?, ?) "
        "ON CONFLICT(hour_ts) DO UPDATE SET node_minutes = node_minutes + ?",
        hour_ts, online, online,
    )
    await d1_run(
        env, "DELETE FROM online_hourly WHERE hour_ts < ?",
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
    return json_response({"ok": True, "hours": series}, cache_seconds=30)


def method_name(request):
    method = getattr(request, "method", "GET")
    return str(method).upper()


def safe_segment(value, max_length=MAX_REPO_SEGMENT):
    value = unquote(value).strip()
    if not value or len(value) > max_length:
        return None
    if not ROOM_NAME_RE.match(value):
        return None
    return value


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


def clean_string(value, max_length=240):
    if not isinstance(value, str):
        return ""
    return value.strip()[:max_length]


def safe_catalog_record(data):
    if not isinstance(data, dict):
        return None

    owner = safe_segment(data.get("owner", ""))
    name = safe_segment(data.get("name", ""))
    public_key = clean_string(data.get("maintainer", ""), 120)
    if not owner or not name or not public_key:
        return None

    now = clean_string(data.get("updatedAt", ""), 32)
    return {
        "owner": owner,
        "name": name,
        "description": clean_string(data.get("description", ""), 240),
        "cloneUrl": clean_string(data.get("cloneUrl", ""), 2048),
        "bch": clean_string(data.get("bch", ""), 160),
        "channel": clean_string(data.get("channel", f"#{owner}-{name}"), 120),
        "hostedSince": clean_string(data.get("hostedSince", ""), 32),
        "lastSync": clean_string(data.get("lastSync", ""), 32),
        "updatedAt": now,
        "source": clean_string(data.get("source", "local-node"), 40),
        "maintainer": public_key,
        "signature": clean_string(data.get("signature", ""), 220),
    }


# --- D1 storage --------------------------------------------------------------
# Durable Objects are reserved for transient relaying (chat rooms + the live
# file/git tunnel). Everything that must persist lives in D1, and every row is
# encrypted at rest: each table stores HMAC "blind index" columns (for lookups
# and uniqueness) plus a single AES-GCM-encrypted JSON `data` blob. The worker
# holds DATA_KEY, so this protects data at rest but is not zero-knowledge.

PBKDF2_ITERS = 150000
MIN_ACTIVE_SATS = 100000  # 0.001 BCH — proves the wallet is active/funded
BCH_RE = re.compile(r"^(bitcoincash:)?[qp][a-z0-9]{41}$")

# Donation funnel: minimum "join" donation (~$1). Each signup gets a freshly
# generated address; when it receives at least this much the account can be
# finalized, the balance is split (see below), and the ephemeral key is forgotten.
MIN_JOIN_SATS = 500000  # 0.005 BCH
DONATION_ADDRESS_TTL_MS = 60 * 60 * 1000
DONATION_ADDRESS_DELETE_GRACE_MS = 5 * 60 * 1000
# Rough sat/byte fee model for the sweep/payout tx: a base plus a bit per output.
BCH_BASE_FEE_SATS = 300
BCH_FEE_PER_OUTPUT_SATS = 50
BCH_DUST_SATS = 546  # below this an output is unspendable dust
# Foundational reward split: half of each confirmed donation goes to the
# treasury, the other half is divided equally among nodes that are currently
# online and have a payout BCH address on file. Capped so the payout tx stays a
# sane size; the split is intentionally simple and will be refined later.
TREASURY_SPLIT_NUMERATOR = 1
TREASURY_SPLIT_DENOMINATOR = 2
MAX_PAYEES = 50
# A node counts as online for payouts if it has sent a heartbeat within this
# window (reuses the host-presence staleness window).
ACCOUNT_PRESENCE_STALE_MS = 10 * 60 * 1000

_schema_ready = False

SCHEMA_STATEMENTS = [
    # email_bi (blind index of the email) lets users log in by email, not just
    # node name (migration 0003). is_admin is an operator-settable flag and name
    # is the public node name in plaintext, so an admin can be granted directly
    # in the DB: UPDATE accounts SET is_admin=1 WHERE name='alice' (migration 0006).
    "CREATE TABLE IF NOT EXISTS accounts (name_bi TEXT PRIMARY KEY, data TEXT NOT NULL, "
    "email_bi TEXT, name TEXT, is_admin INTEGER NOT NULL DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS idx_accounts_email ON accounts(email_bi)",
    """CREATE TABLE IF NOT EXISTS repositories (
        key_bi TEXT PRIMARY KEY, owner_bi TEXT NOT NULL, data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_repos_owner ON repositories(owner_bi)",
    """CREATE TABLE IF NOT EXISTS issue_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_issue_inbox_repo ON issue_inbox(repo_bi)",
    """CREATE TABLE IF NOT EXISTS pull_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, repo_bi TEXT NOT NULL,
        data TEXT NOT NULL)""",
    "CREATE INDEX IF NOT EXISTS idx_pull_inbox_repo ON pull_inbox(repo_bi)",
    # Server-side 5xx / error log surfaced on the admin dashboard.
    """CREATE TABLE IF NOT EXISTS error_log (
        id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL,
        status INTEGER NOT NULL, method TEXT, path TEXT, message TEXT, ray TEXT)""",
    "CREATE INDEX IF NOT EXISTS idx_error_log_ts ON error_log(ts)",
    # Live host presence: lets /api/network/stats report "hosts online" without
    # probing every repo's tunnel Durable Object on every page view. repo_bi is a
    # blind index (no plaintext repo name), ts is refreshed while a host is active
    # and a staleness window self-heals rows left behind by a missed disconnect.
    "CREATE TABLE IF NOT EXISTS host_presence (repo_bi TEXT PRIMARY KEY, ts INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS idx_host_presence_ts ON host_presence(ts)",
    # Account-level presence: a node heartbeats here while it is online so it can
    # be included in the reward split. Keyed by the account blind index.
    "CREATE TABLE IF NOT EXISTS account_presence (name_bi TEXT PRIMARY KEY, ts INTEGER NOT NULL)",
    "CREATE INDEX IF NOT EXISTS idx_account_presence_ts ON account_presence(ts)",
    # Email-verification queue: newly finalized accounts land here until an admin
    # manually verifies them (placeholder until a real email service like SES is
    # wired up). data = encrypted {name, email, joinedAt}.
    "CREATE TABLE IF NOT EXISTS pending_verifications (name_bi TEXT PRIMARY KEY, data TEXT NOT NULL)",
    # Hourly online-activity samples for the /network/ graph. A per-minute cron
    # adds the current online-node count into the current hour's bucket, so
    # node_minutes is "node-minutes online" that hour (one node online all hour
    # = 60). hour_ts is the epoch-ms start of the hour.
    "CREATE TABLE IF NOT EXISTS online_hourly (hour_ts INTEGER PRIMARY KEY, node_minutes INTEGER NOT NULL DEFAULT 0)",
    # Retained chat history: the relay keeps the last few days of *encrypted*
    # durable messages per room so a node joining later sees some history even
    # when no peer is online to replay it. body is the opaque encrypted envelope
    # exactly as relayed; the server never sees plaintext. room_key is the same
    # key used to address the room Durable Object.
    """CREATE TABLE IF NOT EXISTS chat_history (
        room_key TEXT NOT NULL, msg_id TEXT NOT NULL, ts INTEGER NOT NULL,
        body TEXT NOT NULL, PRIMARY KEY (room_key, msg_id))""",
    "CREATE INDEX IF NOT EXISTS idx_chat_history_room_ts ON chat_history(room_key, ts)",
]


async def ensure_schema(env):
    global _schema_ready
    if _schema_ready:
        return
    for sql in SCHEMA_STATEMENTS:
        await env.DB.prepare(sql).run()
    _schema_ready = True


async def d1_all(env, sql, *args):
    stmt = env.DB.prepare(sql)
    if args:
        stmt = stmt.bind(*args)
    result = await stmt.all()
    out = []
    for row in (result.results or []):
        out.append(row.to_py() if hasattr(row, "to_py") else dict(row))
    return out


async def d1_first(env, sql, *args):
    stmt = env.DB.prepare(sql)
    if args:
        stmt = stmt.bind(*args)
    row = await stmt.first()
    if row is None:
        return None
    return row.to_py() if hasattr(row, "to_py") else dict(row)


async def d1_run(env, sql, *args):
    stmt = env.DB.prepare(sql)
    if args:
        stmt = stmt.bind(*args)
    await stmt.run()


# --- Encryption-at-rest + blind index ---------------------------------------

async def _data_key(env):
    secret = getattr(env, "DATA_KEY", "forkmesh-dev-data-key")
    digest = await js_crypto.subtle.digest("SHA-256", _to_js(secret.encode()))
    return await js_crypto.subtle.importKey(
        "raw", digest, to_js({"name": "AES-GCM"}), False,
        _to_js(["encrypt", "decrypt"])
    )


async def encrypt_row(env, obj):
    key = await _data_key(env)
    iv = js_crypto.getRandomValues(Uint8Array.new(12))
    plaintext = json.dumps(obj).encode()
    cipher = await js_crypto.subtle.encrypt(
        to_js({"name": "AES-GCM", "iv": iv}), key, _to_js(plaintext)
    )
    blob = bytes(iv.to_py()) + bytes(Uint8Array.new(cipher).to_py())
    return base64.b64encode(blob).decode()


async def decrypt_row(env, stored):
    try:
        blob = base64.b64decode(stored)
        iv = _to_js(blob[:12])
        cipher = _to_js(blob[12:])
        key = await _data_key(env)
        plain = await js_crypto.subtle.decrypt(
            to_js({"name": "AES-GCM", "iv": iv}), key, cipher
        )
        return json.loads(bytes(Uint8Array.new(plain).to_py()).decode())
    except Exception:
        return None


async def _hmac_key(env):
    # A distinct key context so the blind-index HMAC key isn't the AES key.
    secret = getattr(env, "DATA_KEY", "forkmesh-dev-data-key") + ":blind-index"
    digest = await js_crypto.subtle.digest("SHA-256", _to_js(secret.encode()))
    return await js_crypto.subtle.importKey(
        "raw", digest, to_js({"name": "HMAC", "hash": "SHA-256"}), False,
        _to_js(["sign"])
    )


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

async def catalog_handler(env, request):
    await ensure_schema(env)
    method = method_name(request)
    if method == "GET":
        cached = await edge_cache_match(CATALOG_CACHE_KEY)
        if cached is not None:
            return cached
        rows = await d1_all(env, "SELECT key_bi, data FROM repositories")
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
                rec["liveHost"] = r["key_bi"] in live
                repos.append(rec)
        repos.sort(key=lambda x: x.get("updatedAt", ""), reverse=True)
        resp = json_response(
            {"ok": True, "repositories": repos[:MAX_CATALOG_REPOS]},
            cache_seconds=CATALOG_TTL,
        )
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
        # Repos are namespaced under a registered account, and only that
        # account's key holder may write its namespace. This ties repo identity
        # to the account (fixes duplicate forks) and prevents impersonation.
        #
        # Account registration is temporarily disabled client-side, so when an
        # owner has no registered account we fall back to verifying the publish
        # against its own self-asserted maintainer key (the pre-accounts
        # behaviour). Re-enable strictness by registering accounts again — the
        # registered-owner branch below already enforces it.
        owner_pub = await _owner_pubkey(env, owner)
        verify_pub = owner_pub or record["maintainer"]
        if owner_pub and record["maintainer"] != owner_pub:
            return json_response({"error": "maintainer_mismatch"}, status=403)
        catalog_sig = clean_string(data.get("catalogSig", ""), 200)
        canonical = ("forkmesh-catalog-v1\n" + owner + "\n" + record["name"] +
                     "\n" + record["updatedAt"]).encode()
        if not await ed25519_verify(verify_pub, catalog_sig, canonical):
            return json_response({"error": "bad_signature"}, status=401)

        key_bi = await blind_index(env, owner + "/" + record["name"])
        owner_bi = await blind_index(env, owner)
        enc = await encrypt_row(env, record)
        await d1_run(
            env,
            """INSERT INTO repositories (key_bi, owner_bi, data) VALUES (?,?,?)
               ON CONFLICT(key_bi) DO UPDATE SET
                 owner_bi=excluded.owner_bi, data=excluded.data""",
            key_bi, owner_bi, enc,
        )
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
        verify_pub = owner_pub or clean_string(existing.get("maintainer", ""), 120)
        canonical = ("forkmesh-catalog-delete-v1\n" + owner + "\n" + name +
                     "\n" + ts).encode()
        if not verify_pub or not await ed25519_verify(verify_pub, sig, canonical):
            return json_response({"error": "bad_signature"}, status=401)
        await d1_run(env, "DELETE FROM repositories WHERE key_bi=?", key_bi)
        await edge_cache_delete(CATALOG_CACHE_KEY)
        return json_response({"ok": True, "deleted": True})

    return json_response({"error": "method_not_allowed"}, status=405)


# --- Accounts (accounts table) — name + bch + password + TOTP ---------------

async def _account_row(env, name):
    name_bi = await blind_index(env, name)
    row = await d1_first(env, "SELECT data FROM accounts WHERE name_bi=?", name_bi)
    if not row:
        return name_bi, None
    return name_bi, await decrypt_row(env, row["data"])


async def _owner_pubkey(env, owner):
    _, rec = await _account_row(env, owner)
    return rec.get("pubkey", "") if rec else ""


def _ts_ok(ts):
    try:
        return abs(int(Date.now()) - int(ts)) <= LOGIN_MAX_SKEW_MS
    except (TypeError, ValueError):
        return False


async def _save_account(env, name_bi, rec, email_bi=None):
    # Persist the encrypted record; pass email_bi to (re)index for email login.
    # The plaintext `name` column mirrors the (public) node name so an operator
    # can grant admin in the DB by name; is_admin is never written here, so a
    # value set directly in the DB survives ordinary account updates.
    enc = await encrypt_row(env, rec)
    name = rec.get("name", "")
    if email_bi is None:
        await d1_run(
            env,
            """INSERT INTO accounts (name_bi, data, name) VALUES (?,?,?)
               ON CONFLICT(name_bi) DO UPDATE SET
                 data=excluded.data, name=excluded.name""",
            name_bi, enc, name,
        )
    else:
        await d1_run(
            env,
            """INSERT INTO accounts (name_bi, data, email_bi, name) VALUES (?,?,?,?)
               ON CONFLICT(name_bi) DO UPDATE SET
                 data=excluded.data, email_bi=excluded.email_bi, name=excluded.name""",
            name_bi, enc, email_bi, name,
        )


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
        "donation_address", "donation_privkey", "donation_required_sats",
        "donation_confirmed", "swept", "donation_created_at",
        "donation_expires_at", "donation_delete_after",
    ):
        rec.pop(key, None)
    if rec.get("status") != "active":
        rec["status"] = "reserved"


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
        if ex_pub and (not pubkey or ex_pub != pubkey):
            return json_response({"error": "node_name_taken"}, status=409)
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


# Step 2 (the "Join" step): mint a fresh, single-use BCH address for this
# signup. The private key is generated here, stored encrypted in the row, and
# swept + forgotten once the donation lands (see _account_donation_status).
async def _account_donation_address(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    try:
        amount = int(data.get("amountSats", 0) or 0)
    except (TypeError, ValueError):
        amount = 0
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "reserve_node_name_first"}, status=404)
    if rec.get("status") == "active":
        return json_response({"error": "already_active"}, status=409)

    now = int(Date.now())
    renew_expired = bool(data.get("renewExpired") or data.get("generateNew"))
    if rec.get("donation_address") and not rec.get("donation_confirmed"):
        changed = _ensure_donation_expiry_fields(rec, now)
        required = int(rec.get("donation_required_sats", MIN_JOIN_SATS))
        _, expires_at, delete_at = _donation_expiry_fields(rec, now)
        if now >= expires_at:
            state = await _bch_address_state(rec["donation_address"])
            if state is None:
                return json_response({"error": "explorer_unavailable"}, status=502)
            received = int(state.get("received", 0) or 0)
            if received == 0:
                if renew_expired or now >= delete_at:
                    _clear_donation_address(rec)
                    changed = True
                else:
                    if changed:
                        await _save_account(env, name_bi, rec)
                    return json_response({
                        "ok": True, "expired": True, "hidden": True,
                        "canRenew": True, "receivedSats": 0,
                        "requiredSats": required,
                        "amountBch": "%.8f" % (required / 1e8),
                        "expiresAt": expires_at, "deleteAt": delete_at,
                    })
            elif received >= required:
                rec["donation_confirmed"] = True
                changed = True
        if changed:
            await _save_account(env, name_bi, rec)

    if not rec.get("donation_address"):
        priv_int, priv_hex = bch_wallet.gen_privkey(_random_bytes(32))
        addr = bch_wallet.pubkey_to_cashaddr(
            bch_wallet.privkey_to_pubkey(priv_int))
        rec["donation_address"] = addr
        rec["donation_privkey"] = priv_hex
        rec["donation_required_sats"] = max(amount, MIN_JOIN_SATS)
        rec["donation_confirmed"] = False
        rec["swept"] = False
        rec["donation_created_at"] = now
        rec["donation_expires_at"] = now + DONATION_ADDRESS_TTL_MS
        rec["donation_delete_after"] = (
            rec["donation_expires_at"] + DONATION_ADDRESS_DELETE_GRACE_MS)
        rec["status"] = "pending_payment"
        await _save_account(env, name_bi, rec)

    addr = rec["donation_address"]
    required = int(rec.get("donation_required_sats", MIN_JOIN_SATS))
    _, expires_at, delete_at = _donation_expiry_fields(rec, int(Date.now()))
    amount_bch = "%.8f" % (required / 1e8)
    return json_response({
        "ok": True, "address": addr, "uri": addr + "?amount=" + amount_bch,
        "requiredSats": required, "amountBch": amount_bch,
        "expiresAt": expires_at, "deleteAt": delete_at,
    })


async def _reward_outputs(env, treasury, total_in):
    """Compute the payout outputs for a confirmed donation.

    Foundational reward split: ~50% to the treasury, the rest divided equally
    among online nodes with a payout address. If there are no eligible nodes (or
    each share would be dust), everything goes to the treasury. Returns a list of
    (address, sats) for bch_wallet.build_tx, or None if the inputs can't even
    cover the fee.
    """
    payees = await _online_payout_addresses(env)
    num_outputs = 1 + len(payees)
    fee = BCH_BASE_FEE_SATS + BCH_FEE_PER_OUTPUT_SATS * num_outputs
    distributable = total_in - fee
    if distributable <= 0:
        return None

    treasury_share = (distributable * TREASURY_SPLIT_NUMERATOR
                      ) // TREASURY_SPLIT_DENOMINATOR
    remainder = distributable - treasury_share
    if payees and remainder // len(payees) >= BCH_DUST_SATS:
        per = remainder // len(payees)
        leftover = remainder - per * len(payees)  # rounding dust → treasury
        outputs = [(treasury, treasury_share + leftover)]
        outputs += [(addr, per) for addr in payees]
        return outputs
    # No eligible payees (or shares too small): treasury takes the lot. Recompute
    # the fee for the single-output case so we don't overpay miners.
    fee = BCH_BASE_FEE_SATS + BCH_FEE_PER_OUTPUT_SATS
    distributable = total_in - fee
    if distributable <= 0:
        return None
    return [(treasury, distributable)]


# Polled while the user waits to pay. When the address has received enough, the
# account is marked confirmed; we then split the balance (treasury + online
# nodes) and, once the balance is zero, delete the ephemeral private key.
async def _account_donation_status(env, request):
    params = parse_qs(urlparse(request.url).query)
    name = clean_string(params.get("nodeName", [""])[0], MAX_NODE_NAME).lower()
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "no_such_account"}, status=404)
    addr = rec.get("donation_address", "")
    if not addr:
        return json_response({"error": "no_donation_address"}, status=400)
    required = int(rec.get("donation_required_sats", MIN_JOIN_SATS))
    now = int(Date.now())
    changed = _ensure_donation_expiry_fields(rec, now)
    _, expires_at, delete_at = _donation_expiry_fields(rec, now)

    state = await _bch_address_state(addr)
    if state is None:
        return json_response({"error": "explorer_unavailable"}, status=502)
    received = state["received"]
    if (not rec.get("donation_confirmed") and received == 0 and
            now >= delete_at):
        _clear_donation_address(rec)
        await _save_account(env, name_bi, rec)
        return json_response({
            "ok": True, "paid": False, "expired": True, "hidden": True,
            "deleted": True, "canRenew": True, "receivedSats": 0,
            "requiredSats": required, "expiresAt": expires_at,
            "deleteAt": delete_at,
        })
    if received >= required and not rec.get("donation_confirmed"):
        rec["donation_confirmed"] = True
        changed = True

    swept = bool(rec.get("swept"))
    treasury = _treasury_address(env)
    if (rec.get("donation_confirmed") and not swept and
            rec.get("donation_privkey") and treasury and state["utxos"]):
        try:
            outputs = await _reward_outputs(env, treasury, sum(
                u["value"] for u in state["utxos"]))
            if outputs:
                priv_int = int(rec["donation_privkey"], 16)
                raw_hex, _, _ = bch_wallet.build_tx(
                    priv_int, state["utxos"], outputs)
                if await _bch_broadcast(raw_hex):
                    rec["swept"] = True
                    swept = True
                    changed = True
        except Exception:
            pass  # retried on the next poll

    # Forget the spendable key once the swept balance has confirmed to zero.
    if swept and rec.get("donation_privkey") and state["balance"] == 0:
        rec.pop("donation_privkey", None)
        changed = True

    if changed:
        await _save_account(env, name_bi, rec)
    return json_response({
        "ok": True, "paid": bool(rec.get("donation_confirmed")),
        "receivedSats": received, "requiredSats": required,
        "swept": bool(rec.get("swept")),
        "expired": (not rec.get("donation_confirmed") and received == 0 and
                    now >= expires_at),
        "hidden": (not rec.get("donation_confirmed") and received == 0 and
                   now >= expires_at),
        "canRenew": (not rec.get("donation_confirmed") and received == 0 and
                     now >= expires_at),
        "expiresAt": expires_at, "deleteAt": delete_at,
    })


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
    bch = clean_string(data.get("bch", ""), 160)
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)

    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "no_such_account"}, status=404)
    if not rec.get("donation_confirmed"):
        return json_response({"error": "donation_required"}, status=402)
    if "@" not in email or len(email) < 3:
        return json_response({"error": "valid_email_required"}, status=400)
    if len(password) < 8:
        return json_response({"error": "password_too_short"}, status=400)

    # A key-bound (desktop) account must prove ownership to finalize.
    if rec.get("pubkey"):
        if not _ts_ok(ts):
            return json_response({"error": "stale_request"}, status=401)
        canonical = ("forkmesh-finalize-v1\n" + name + "\n" + email + "\n" +
                     ts).encode()
        if not await ed25519_verify(rec["pubkey"], signature, canonical):
            return json_response({"error": "bad_signature"}, status=401)
    elif pubkey:
        rec["pubkey"] = pubkey  # bind a key now if a web user supplied one

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
    if bch and BCH_RE.match(bch.lower()):
        rec["bch"] = bch
    rec["email_verified"] = bool(rec.get("email_verified", False))
    rec.setdefault("created_at", int(Date.now()))
    await _save_account(env, name_bi, rec, email_bi=email_bi)
    # Until a real email service exists, an admin verifies the email by hand:
    # enqueue the new account so admins are notified and can verify it.
    if not rec["email_verified"]:
        await _enqueue_verification(env, name_bi, name, email)
    return json_response(
        {"ok": True, "nodeName": name, "email": email, "status": "active",
         "emailVerified": rec["email_verified"],
         "isAdmin": await _is_admin(env, name)},
        status=201)


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

    rec = None
    if "@" in identifier:
        email_bi = await blind_index(env, identifier)
        row = await d1_first(
            env, "SELECT data FROM accounts WHERE email_bi=?", email_bi)
        if row:
            rec = await decrypt_row(env, row["data"])
    elif valid_node_name(identifier):
        _, rec = await _account_row(env, identifier)
    if not rec:
        return json_response({"error": "no_such_account"}, status=404)
    if rec.get("status") != "active" or not rec.get("pass_hash"):
        return json_response({"error": "account_not_active"}, status=403)
    if not await verify_password(password, rec.get("pass_salt", ""),
                                 rec.get("pass_hash", "")):
        return json_response({"error": "bad_password"}, status=401)
    # TOTP is only enforced for accounts that have enrolled it.
    if rec.get("totp_enrolled"):
        if not await totp_verify(rec.get("totp_secret", ""), totp):
            return json_response({"error": "bad_totp"}, status=401)
    return json_response({
        "ok": True, "nodeName": rec.get("name", ""),
        "email": rec.get("email", ""), "status": rec.get("status", "active"),
        "pubkey": rec.get("pubkey", ""),
        "emailVerified": bool(rec.get("email_verified")),
        "isAdmin": await _is_admin(env, rec.get("name", "")),
    })


def _random_bytes(n):
    return bytes(js_crypto.getRandomValues(Uint8Array.new(n)).to_py())


def _treasury_address(env):
    # Receives ~50% of each confirmed donation; the rest is split among online
    # nodes (see _reward_outputs). Falls back to NODE_BCH_ADDRESS.
    addr = (getattr(env, "TREASURY_BCH_ADDRESS", "") or
            getattr(env, "NODE_BCH_ADDRESS", "") or "").strip()
    return addr if BCH_RE.match(addr.lower()) else ""


async def _bch_address_state(addr):
    print("calling bch_address state")
    # One Blockchair call → cumulative received, current balance, and the UTXO
    # set used to build the sweep. Returns None if the explorer is unreachable.
    from js import fetch as js_fetch
    a = addr.split(":")[-1]
    try:
        resp = await js_fetch(
            "https://api.blockchair.com/bitcoin-cash/dashboards/address/" + a +
            "?limit=1000"
        )
        print("response is")
        print(resp)
        obj = json.loads(await resp.text())
        print(obj)
        data = obj.get("data", {}) or {}
        print(data)
        entry = data.get(a)
        if entry is None and data:
            entry = next(iter(data.values()))
        entry = entry or {}
        address = entry.get("address", {}) if isinstance(entry, dict) else {}
        utxos = []
        for u in (entry.get("utxo", []) or []):
            utxos.append({
                "txid": u.get("transaction_hash", ""),
                "vout": int(u.get("index", 0) or 0),
                "value": int(u.get("value", 0) or 0),
            })
        return {
            "received": int(address.get("received", 0) or 0),
            "balance": int(address.get("balance", 0) or 0),
            "utxos": utxos,
        }
    except Exception as e:
        print("error"+e)
        return None


async def _bch_broadcast(raw_hex):
    from js import fetch as js_fetch
    try:
        resp = await js_fetch(
            "https://api.blockchair.com/bitcoin-cash/push/transaction",
            to_js({
                "method": "POST",
                "headers": {"content-type": "application/x-www-form-urlencoded"},
                "body": "data=" + raw_hex,
            }),
        )
        return 200 <= int(getattr(resp, "status", 0)) < 300
    except Exception:
        return False


# A running node calls this on an interval to stay eligible for the reward
# split. Signed with the account's key so only the key holder can mark its node
# online; optionally updates the payout BCH address.
async def _account_heartbeat(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), MAX_NODE_NAME).lower()
    bch = clean_string(data.get("bch", ""), 160)
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    name_bi, rec = await _account_row(env, name)
    if not rec or rec.get("status") != "active":
        return json_response({"ok": True, "online": False})
    pubkey = rec.get("pubkey", "")
    if not pubkey or not _ts_ok(ts):
        return json_response({"error": "unauthorized"}, status=401)
    canonical = ("forkmesh-heartbeat-v1\n" + name + "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)

    # Keep the payout address current if the node sent a valid one.
    if bch and BCH_RE.match(bch.lower()) and rec.get("bch") != bch:
        rec["bch"] = bch
        await _save_account(env, name_bi, rec)

    await d1_run(
        env,
        "INSERT INTO account_presence (name_bi, ts) VALUES (?, ?) "
        "ON CONFLICT(name_bi) DO UPDATE SET ts=excluded.ts",
        name_bi, int(Date.now()),
    )
    return json_response({"ok": True, "online": True,
                          "hasPayoutAddress": bool(rec.get("bch")),
                          "isAdmin": await _is_admin(env, name)})


async def _online_payout_addresses(env):
    # Deduplicated payout addresses of currently-online, active accounts. Stale
    # presence rows self-heal here so the table stays bounded.
    cutoff = int(Date.now()) - ACCOUNT_PRESENCE_STALE_MS
    try:
        await d1_run(env, "DELETE FROM account_presence WHERE ts < ?", cutoff)
    except Exception:
        pass
    rows = await d1_all(
        env, "SELECT name_bi FROM account_presence WHERE ts >= ? ORDER BY ts DESC",
        cutoff,
    )
    seen = set()
    addresses = []
    for r in rows:
        row = await d1_first(
            env, "SELECT data FROM accounts WHERE name_bi=?", r["name_bi"])
        if not row:
            continue
        rec = await decrypt_row(env, row["data"])
        if not rec or rec.get("status") != "active":
            continue
        bch = (rec.get("bch") or "").strip()
        if not bch or not BCH_RE.match(bch.lower()) or bch in seen:
            continue
        seen.add(bch)
        addresses.append(bch)
        if len(addresses) >= MAX_PAYEES:
            break
    return addresses


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


async def accounts_handler(env, request):
    await ensure_schema(env)
    url = urlparse(request.url)
    method = method_name(request)
    if url.path == "/api/accounts/reserve" and method == "POST":
        return await _account_reserve(env, request)
    if url.path == "/api/accounts/donation-address" and method == "POST":
        return await _account_donation_address(env, request)
    if url.path == "/api/accounts/donation-status" and method == "GET":
        return await _account_donation_status(env, request)
    if url.path == "/api/accounts/finalize" and method == "POST":
        return await _account_finalize(env, request)
    if url.path == "/api/accounts/heartbeat" and method == "POST":
        return await _account_heartbeat(env, request)
    if url.path == "/api/accounts/admin-pending" and method == "GET":
        return await _admin_pending(env, request)
    if url.path == "/api/accounts/admin-verify-email" and method == "POST":
        return await _admin_verify_email(env, request)
    if url.path == "/api/accounts/login" and method == "POST":
        return await _account_login(env, request)
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
        return json_response(
            {"ok": True, "exists": True, "available": not taken,
             "name": rec.get("name", name), "status": rec.get("status", ""),
             "pubkey": rec.get("pubkey", ""),
             "createdAt": rec.get("created_at", 0)}
        )
    return json_response({"error": "not_found"}, status=404)


# --- Issue & pull submission inboxes (encrypted) ----------------------------

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
        item = {
            "number": number,
            "titleIfNew": clean_string(data.get("titleIfNew", ""), 240),
            "event": event,
            "submitter": clean_string(event.get("author", ""), 120),
            "submittedAt": int(Date.now()),
        }
        await d1_run(
            env, "INSERT INTO issue_inbox (repo_bi, data) VALUES (?,?)",
            repo_bi, await encrypt_row(env, item),
        )
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
        pull = data.get("pull")
        if not isinstance(pull, dict):
            return json_response({"error": "pull_required"}, status=400)
        if len((pull.get("patch", "") or "").encode("utf-8")) > MAX_PULL_BYTES:
            return json_response({"error": "pull_too_large"}, status=413)
        if not await verify_pull_event(pull):
            return json_response({"error": "bad_signature"}, status=401)
        count = await d1_first(
            env, "SELECT COUNT(*) AS c FROM pull_inbox WHERE repo_bi=?", repo_bi
        )
        if count and count.get("c", 0) >= MAX_PENDING_PULLS:
            return json_response({"error": "inbox_full"}, status=429)
        item = {
            "pull": pull,
            "submitter": clean_string(pull.get("author", ""), 120),
            "submittedAt": int(Date.now()),
        }
        await d1_run(
            env, "INSERT INTO pull_inbox (repo_bi, data) VALUES (?,?)",
            repo_bi, await encrypt_row(env, item),
        )
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
    err_row = await d1_first(
        env, "SELECT COUNT(*) AS n FROM error_log WHERE ts >= ?",
        int(Date.now()) - 24 * 60 * 60 * 1000,
    )
    return {
        "repos": int((repo_row or {}).get("n", 0) or 0),
        "hosts": int((host_row or {}).get("n", 0) or 0),
        "clients": await _flagship_client_count(env),
        "errors_24h": int((err_row or {}).get("n", 0) or 0),
    }


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


ADMIN_TEMPLATE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>forkmesh · error log</title>
<style>
 body{font:14px/1.5 system-ui,sans-serif;margin:0;background:#0d1117;color:#c9d1d9}
 header{padding:16px 24px;border-bottom:1px solid #21262d}
 h1{font-size:18px;margin:0}
 .meta{color:#8b949e;font-size:13px;margin-top:4px}
 table{border-collapse:collapse;width:100%%}
 th,td{text-align:left;padding:8px 12px;border-bottom:1px solid #21262d;vertical-align:top}
 th{position:sticky;top:0;background:#161b22;color:#8b949e;font-weight:600}
 td.msg{font-family:ui-monospace,monospace;white-space:pre-wrap;word-break:break-word;max-width:520px}
 .s5{color:#f85149;font-weight:600}
 tr:hover{background:#161b22}
 .empty{padding:32px 24px;color:#8b949e}
 .cards{display:flex;gap:12px;flex-wrap:wrap;padding:16px 24px}
 .card{background:#161b22;border:1px solid #21262d;border-radius:8px;padding:12px 18px;min-width:120px}
 .card .n{font-size:24px;font-weight:600}
 .card .l{color:#8b949e;font-size:12px;margin-top:2px}
 .card.warn .n{color:#f85149}
</style></head><body>
<header><h1>forkmesh · admin</h1>
<div class="meta">Live Durable Object load (hosts + clients are what hold a DO
open). %(count)d most recent error(s) below. True Cloudflare edge errors
(520–526) never reach the worker — cross-reference the CF-Ray
in the Cloudflare dashboard.</div></header>
%(stats)s
%(table)s
<script>
 for (const el of document.querySelectorAll('[data-ts]')) {
   const ms = Number(el.getAttribute('data-ts'));
   if (ms) el.textContent = new Date(ms).toLocaleString();
 }
</script>
</body></html>"""


def _render_admin_stats(stats):
    if not stats:
        return ""
    cards = [
        ("hosts", "live hosts", False),
        ("clients", "chat clients", False),
        ("repos", "catalog repos", False),
        ("errors_24h", "errors (24h)", True),
    ]
    out = []
    for key, label, warn in cards:
        value = stats.get(key, 0)
        cls = "card warn" if warn and value else "card"
        out.append('<div class="%s"><div class="n">%s</div><div class="l">%s</div></div>'
                    % (cls, _html_escape(value), label))
    return '<div class="cards">' + "".join(out) + "</div>"


def render_admin_html(rows, stats=None):
    if not rows:
        table = '<div class="empty">No errors recorded yet.</div>'
    else:
        body = []
        for r in rows:
            status = r.get("status", "")
            cls = "s5" if str(status).startswith("5") else ""
            body.append(
                "<tr>"
                '<td data-ts="%s">%s</td>'
                '<td class="%s">%s</td>'
                "<td>%s</td><td>%s</td>"
                '<td class="msg">%s</td><td>%s</td>'
                "</tr>"
                % (
                    _html_escape(r.get("ts", "")), _html_escape(r.get("ts", "")),
                    cls, _html_escape(status),
                    _html_escape(r.get("method", "")), _html_escape(r.get("path", "")),
                    _html_escape(r.get("message", "")), _html_escape(r.get("ray", "")),
                )
            )
        table = (
            "<table><thead><tr><th>Time</th><th>Status</th><th>Method</th>"
            "<th>Path</th><th>Message</th><th>CF-Ray</th></tr></thead><tbody>"
            + "".join(body)
            + "</tbody></table>"
        )
    return ADMIN_TEMPLATE % {
        "count": len(rows),
        "table": table,
        "stats": _render_admin_stats(stats),
    }


class Default(WorkerEntrypoint):
    async def scheduled(self, controller, env, ctx):
        # Cron trigger (every minute, see [triggers] in wrangler.toml): sample how
        # many nodes are online and fold it into the current hour's bucket for the
        # /network/ activity graph. Best-effort — never raise from the cron.
        try:
            await record_online_sample(self.env)
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
                repr(error), request.headers.get("cf-ray") or "",
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
        rows = await d1_all(
            self.env,
            "SELECT ts, status, method, path, message, ray FROM error_log "
            "ORDER BY id DESC LIMIT ?",
            MAX_ERROR_LOG,
        )
        stats = await admin_stats(self.env)
        return Response(
            render_admin_html(rows, stats),
            status=200,
            headers={"content-type": "text/html; charset=utf-8"},
        )

    async def _route(self, request, url):
        # TEMP DIAGNOSTIC (remove after debugging the admin 404): reports whether
        # the live worker sees ADMIN_PATH and whether it equals a candidate, with
        # no secret leak (booleans + length only).
        if url.path == "/api/_admincheck":
            ap = _admin_path(self.env)
            cand = (parse_qs(url.query).get("p", [""])[0] or "").strip("/")
            return json_response({
                "configured": bool(ap),
                "length": len(ap),
                "match": bool(ap) and ap == cand,
            })

        # Git smart-HTTP clone, proxied to the hosting client over the tunnel.
        git_info = GIT_INFO_RE.match(url.path)
        if git_info and parse_qs(url.query).get("service", [""])[0] == "git-upload-pack":
            return await self._git_host(request, git_info.group(1), git_info.group(2))
        git_pack = GIT_PACK_RE.match(url.path)
        if git_pack and method_name(request) == "POST":
            return await self._git_host(request, git_pack.group(1), git_pack.group(2))

        # Static docs/network directories are canonical with a trailing slash.
        # Keep this as routing support only; all persistent v0.3.0 APIs stay below.
        if url.path == "/network":
            return Response("", status=308, headers={"location": "/network/"})
        if url.path == "/docs":
            return Response("", status=308, headers={"location": "/docs/"})

        if url.path in ("/health", "/api/mainnode"):
            return json_response(
                {
                    "ok": True,
                    "service": "forkmesh-mainnode",
                    "node": getattr(self.env, "NODE_NAME", "forkmesh"),
                    "nodeBchAddress": getattr(self.env, "NODE_BCH_ADDRESS", ""),
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

        # Cached aggregate stats for the homepage/network page. Served before the
        # per-repo handlers so a burst of visitors collapses to one computation
        # per colo per TTL instead of a Durable Object fan-out per page view.
        if url.path in ("/api/network/stats", "/api/network/stats/"):
            return await network_stats(self.env)

        # 24-hour online-activity series for the /network/ graph.
        if url.path in ("/api/network/online-history", "/api/network/online-history/"):
            return await online_history(self.env)

        # Persistent data lives in D1, not Durable Objects.
        if url.path in ("/api/repositories", "/api/repositories/"):
            return await catalog_handler(self.env, request)

        # All /api/accounts/* paths (reserve, donation-address, donation-status,
        # finalize, login, and GET /api/accounts/{name}) are single-segment, so
        # ACCOUNTS_RE matches them and accounts_handler dispatches by path.
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

        host_match = REPO_HOST_RE.match(url.path)
        if host_match:
            owner = safe_segment(host_match.group(1))
            repo = safe_segment(host_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            host_id = self.env.FORKMESH_HOST.idFromName(f"host:{owner}/{repo}")
            host_object = self.env.FORKMESH_HOST.get(host_id)
            return await host_object.fetch(request)

        room = room_key_from_path(url.path)
        if room:
            room_id = self.env.FORKMESH_MAINNODE_ROOM.idFromName(room["key"])
            room_object = self.env.FORKMESH_MAINNODE_ROOM.get(room_id)
            return await room_object.fetch(request)

        return json_response({"error": "not_found"}, status=404)

    async def _git_host(self, request, owner_raw, repo_raw):
        owner = safe_segment(owner_raw)
        repo = safe_segment(repo_raw)
        if not owner or not repo:
            return Response("not found", status=404)
        host_id = self.env.FORKMESH_HOST.idFromName(f"host:{owner}/{repo}")
        host_object = self.env.FORKMESH_HOST.get(host_id)
        return await host_object.fetch(request)


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

    async def webSocketMessage(self, ws, message):
        if not isinstance(message, str):
            self._safe_close(ws, 1003, "text frames only")
            return
        if len(message.encode("utf-8")) > MAX_TEXT_BYTES:
            self._safe_close(ws, 1009, "message too large")
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
        if not hasattr(self, "_repo_bi"):
            self._repo_bi = None   # blind index of this DO's owner/repo
        if not hasattr(self, "_last_presence"):
            self._last_presence = 0  # ms of last host_presence write (throttle)

    async def _repo_blind_index(self, path):
        # This DO is per-repo; derive its blind index from the request path once
        # and cache it. Works for both /api/repo/{o}/{r}/* and git /{o}/{r}/* URLs.
        if self._repo_bi:
            return self._repo_bi
        match = (REPO_HOST_RE.match(path) or GIT_INFO_RE.match(path)
                 or GIT_PACK_RE.match(path))
        if not match:
            return None
        owner = safe_segment(match.group(1))
        repo = safe_segment(match.group(2))
        if not owner or not repo:
            return None
        self._repo_bi = await blind_index(self.env, owner + "/" + repo)
        return self._repo_bi

    async def _mark_present(self, path=None):
        # Refresh this repo's host-presence row, throttled so hot browse traffic
        # doesn't write to D1 on every request. Best-effort; never fails the call.
        now = int(Date.now())
        if now - self._last_presence < HOST_PRESENCE_REFRESH_MS:
            return
        repo_bi = await self._repo_blind_index(path) if path else self._repo_bi
        if not repo_bi:
            return
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

        # Git smart-HTTP clone endpoints proxied to the host's git upload-pack.
        if path.endswith("/info/refs"):
            await self._mark_present(path)
            return await self._git(request, "git-info-refs")
        if path.endswith("/git-upload-pack"):
            await self._mark_present(path)
            body = b""
            try:
                body = bytes(await request.bytes())
            except Exception:
                body = b""
            return await self._git(request, "git-upload-pack", body)

        action = path.rsplit("/", 1)[-1]
        if action == "host":
            if not is_websocket:
                return json_response({"ok": True, "hosts": self._host_count()})
            client, server = WebSocketPair.new().object_values()
            self.ctx.acceptWebSocket(server, to_js(["host"]))
            server.serializeAttachment(to_js({"rtt": None}))
            await self._mark_present(path)
            return JsResponse.new(
                None, to_js({"status": 101, "webSocket": client})
            )

        rel_path = (parse_qs(url.query).get("path", [""])[0] or "").strip()
        if action in ("tree", "blob", "commits", "commit"):
            await self._mark_present(path)
            return await self._tunnel(action, rel_path)
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
        if mtype == "response":
            fut = self.pending.pop(msg.get("reqId"), None)
            if fut is not None and not fut.done():
                fut.set_result(msg)
        elif mtype == "git-chunk":
            buffer = self.git_buffers.get(msg.get("reqId"))
            if buffer is not None:
                try:
                    buffer += base64.b64decode(msg.get("data", ""))
                except Exception:
                    pass
        elif mtype == "git-end":
            req_id = msg.get("reqId")
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
        return

    async def webSocketError(self, ws, error):
        return

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

    async def _tunnel(self, op, rel_path):
        self._ensure()
        host = self._best_host()
        if host is None:
            return json_response(
                {"ok": False, "error": "no_host"}, status=503
            )

        self.counter += 1
        req_id = "r%d" % self.counter
        loop = asyncio.get_event_loop()
        future = loop.create_future()
        self.pending[req_id] = future
        started = Date.now()
        try:
            host.send(
                json.dumps({"type": "request", "reqId": req_id, "op": op,
                            "path": rel_path})
            )
        except Exception:
            self.pending.pop(req_id, None)
            self._drop_host(host)
            return json_response(
                {"ok": False, "error": "host_unavailable"}, status=503
            )

        try:
            msg = await asyncio.wait_for(future, timeout=TUNNEL_TIMEOUT_MS / 1000)
        except Exception:
            self.pending.pop(req_id, None)
            return json_response({"ok": False, "error": "timeout"}, status=504)

        self._update_rtt(host, Date.now() - started)

        if not msg.get("ok"):
            return json_response(
                {"ok": False, "error": msg.get("error", "host_error")},
                status=502,
            )
        payload = {
            key: value
            for key, value in msg.items()
            if key not in ("type", "reqId", "ok")
        }
        payload["ok"] = True
        return json_response(payload)

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
            ws.serializeAttachment(to_js({"rtt": new_rtt}))
        except Exception:
            pass

    async def _git(self, request, op, body=None):
        # Forward a git smart-HTTP request to the hosting client, which runs
        # git upload-pack on its local mirror and streams the result back in
        # chunks (reassembled here).
        self._ensure()
        host = self._best_host()
        if host is None:
            return Response("No client is hosting this repository.", status=503)

        self.counter += 1
        req_id = "g%d" % self.counter
        loop = asyncio.get_event_loop()
        future = loop.create_future()
        self.pending[req_id] = future
        self.git_buffers[req_id] = bytearray()
        message = {"type": "request", "reqId": req_id, "op": op}
        if body:
            message["body"] = base64.b64encode(body).decode()
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
        if op == "git-info-refs":
            body_out = (
                pkt_line(b"# service=git-upload-pack\n") + b"0000" + data
            )
            return git_bytes_response(
                body_out, "application/x-git-upload-pack-advertisement"
            )
        return git_bytes_response(data, "application/x-git-upload-pack-result")
