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
from js import crypto as js_crypto
from pyodide.ffi import create_proxy
from pyodide.ffi import to_js as _to_js
from workers import DurableObject, Response, WorkerEntrypoint

MAX_ROOM_NAME = 80
MAX_REPO_SEGMENT = 80
MAX_CONNECTIONS = 128
MAX_OBSERVERS = 1000
MAX_TEXT_BYTES = 96 * 1024 * 1024
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


NODE_NAME_RE = re.compile(r"^[a-z][a-z0-9]*$")
ACCOUNTS_RE = re.compile(r"^/api/accounts/([^/]+)$")
LOGIN_MAX_SKEW_MS = 5 * 60 * 1000


def valid_node_name(value):
    value = (value or "").strip()
    return bool(value) and len(value) <= 32 and bool(NODE_NAME_RE.match(value))


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


def json_response(data, status=200):
    return Response(
        json.dumps(data, indent=2),
        status=status,
        headers={"content-type": "application/json; charset=utf-8"},
    )


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

_schema_ready = False

SCHEMA_STATEMENTS = [
    "CREATE TABLE IF NOT EXISTS accounts (name_bi TEXT PRIMARY KEY, data TEXT NOT NULL)",
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
        rows = await d1_all(env, "SELECT data FROM repositories")
        repos = []
        for r in rows:
            rec = await decrypt_row(env, r["data"])
            if rec:
                repos.append(rec)
        repos.sort(key=lambda x: x.get("updatedAt", ""), reverse=True)
        return json_response(
            {"ok": True, "repositories": repos[:MAX_CATALOG_REPOS]}
        )

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
        return json_response({"ok": True, "repository": record}, status=201)

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


async def _account_signup(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)

    name = clean_string(data.get("nodeName", ""), 32).lower()
    pubkey = clean_string(data.get("pubkey", ""), 120)
    email = clean_string(data.get("email", ""), 254)
    bch = clean_string(data.get("bch", ""), 160)
    password = (data.get("password", "") or "")[:256]
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)

    if not valid_node_name(name):
        return json_response(
            {"error": "node_name_must_be_lowercase_alphanumeric_starting_with_a_letter"},
            status=400,
        )
    if not pubkey or "@" not in email or not ts:
        return json_response({"error": "pubkey_email_and_ts_required"}, status=400)
    if not BCH_RE.match(bch.lower()):
        return json_response({"error": "valid_bch_address_required"}, status=400)
    if len(password) < 8:
        return json_response({"error": "password_too_short"}, status=400)

    canonical = ("forkmesh-account-v1\n" + name + "\n" + email + "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)

    name_bi, existing = await _account_row(env, name)
    if existing and existing.get("pubkey") != pubkey:
        return json_response({"error": "node_name_taken"}, status=409)

    salt, phash = await hash_password(password)
    # Preserve an existing operator's TOTP/verification across a re-signup.
    totp_secret = existing.get("totp_secret") if existing else gen_totp_secret()
    totp_enrolled = bool(existing.get("totp_enrolled")) if existing else False
    bch_verified = bool(existing.get("bch_verified")) if existing else False
    created = existing.get("created_at") if existing else int(Date.now())

    record = {
        "name": name, "pubkey": pubkey, "email": email, "bch": bch,
        "pass_salt": salt, "pass_hash": phash, "totp_secret": totp_secret,
        "totp_enrolled": totp_enrolled, "bch_verified": bch_verified,
        "created_at": int(created),
    }
    await d1_run(
        env,
        """INSERT INTO accounts (name_bi, data) VALUES (?,?)
           ON CONFLICT(name_bi) DO UPDATE SET data=excluded.data""",
        name_bi, await encrypt_row(env, record),
    )
    resp = {"ok": True, "nodeName": name, "totpEnrolled": totp_enrolled}
    if not totp_enrolled:
        resp["totpSecret"] = totp_secret
        resp["totpUri"] = ("otpauth://totp/ForkMesh:" + name + "?secret=" +
                           totp_secret + "&issuer=ForkMesh&digits=6&period=30")
    return json_response(resp, status=201)


async def _account_login(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)

    name = clean_string(data.get("nodeName", ""), 32).lower()
    password = (data.get("password", "") or "")[:256]
    totp = clean_string(data.get("totp", ""), 10)
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "no_such_account"}, status=404)

    if not await verify_password(password, rec.get("pass_salt", ""),
                                 rec.get("pass_hash", "")):
        return json_response({"error": "bad_password"}, status=401)
    if not await totp_verify(rec.get("totp_secret", ""), totp):
        return json_response({"error": "bad_totp"}, status=401)

    if not rec.get("totp_enrolled"):
        rec["totp_enrolled"] = True
        await d1_run(
            env, "UPDATE accounts SET data=? WHERE name_bi=?",
            await encrypt_row(env, rec), name_bi,
        )
    return json_response({
        "ok": True, "nodeName": name, "email": rec.get("email", ""),
        "bch": rec.get("bch", ""), "bchVerified": bool(rec.get("bch_verified")),
    })


async def _bch_total_received(bch):
    from js import fetch as js_fetch
    addr = bch.split(":")[-1]
    try:
        resp = await js_fetch(
            "https://api.blockchair.com/bitcoin-cash/dashboards/address/" + addr
        )
        obj = json.loads(await resp.text())
        for _, value in (obj.get("data", {}) or {}).items():
            address = value.get("address", {}) if isinstance(value, dict) else {}
            if "received" in address:
                return int(address.get("received", 0))
        return 0
    except Exception:
        return None


async def _account_verify_bch(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)
    name = clean_string(data.get("nodeName", ""), 32).lower()
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    name_bi, rec = await _account_row(env, name)
    if not rec:
        return json_response({"error": "no_such_account"}, status=404)
    try:
        skew = abs(int(Date.now()) - int(ts))
    except (TypeError, ValueError):
        skew = LOGIN_MAX_SKEW_MS + 1
    if skew > LOGIN_MAX_SKEW_MS:
        return json_response({"error": "stale_timestamp"}, status=401)
    canonical = ("forkmesh-verify-bch-v1\n" + name + "\n" + ts).encode()
    if not await ed25519_verify(rec.get("pubkey", ""), signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)

    bch = rec.get("bch", "")
    if not bch:
        return json_response({"error": "no_bch_address"}, status=400)
    received = await _bch_total_received(bch)
    if received is None:
        return json_response({"error": "explorer_unavailable"}, status=502)
    verified = received >= MIN_ACTIVE_SATS
    if verified and not rec.get("bch_verified"):
        rec["bch_verified"] = True
        await d1_run(
            env, "UPDATE accounts SET data=? WHERE name_bi=?",
            await encrypt_row(env, rec), name_bi,
        )
    return json_response({"ok": True, "bchVerified": verified,
                          "receivedSats": received,
                          "requiredSats": MIN_ACTIVE_SATS})


async def accounts_handler(env, request):
    await ensure_schema(env)
    url = urlparse(request.url)
    method = method_name(request)
    if url.path == "/api/accounts/signup" and method == "POST":
        return await _account_signup(env, request)
    if url.path == "/api/accounts/login" and method == "POST":
        return await _account_login(env, request)
    if url.path == "/api/accounts/verify-bch" and method == "POST":
        return await _account_verify_bch(env, request)
    match = ACCOUNTS_RE.match(url.path)
    if match and method == "GET":
        name = match.group(1)
        _, rec = await _account_row(env, clean_string(name, 32).lower())
        if not rec:
            return json_response({"ok": True, "exists": False, "name": name})
        # Public lookup never returns email/secret material.
        return json_response(
            {"ok": True, "exists": True, "name": rec.get("name", name),
             "pubkey": rec.get("pubkey", ""),
             "createdAt": rec.get("created_at", 0),
             "bchVerified": bool(rec.get("bch_verified"))}
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
</style></head><body>
<header><h1>forkmesh · server error log</h1>
<div class="meta">%(count)d most recent error(s). True Cloudflare edge errors
(520–526) never reach the worker — cross-reference the CF-Ray
in the Cloudflare dashboard.</div></header>
%(table)s
<script>
 for (const el of document.querySelectorAll('[data-ts]')) {
   const ms = Number(el.getAttribute('data-ts'));
   if (ms) el.textContent = new Date(ms).toLocaleString();
 }
</script>
</body></html>"""


def render_admin_html(rows):
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
    return ADMIN_TEMPLATE % {"count": len(rows), "table": table}


class Default(WorkerEntrypoint):
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
        return Response(
            render_admin_html(rows),
            status=200,
            headers={"content-type": "text/html; charset=utf-8"},
        )

    async def _route(self, request, url):
        # Git smart-HTTP clone, proxied to the hosting client over the tunnel.
        git_info = GIT_INFO_RE.match(url.path)
        if git_info and parse_qs(url.query).get("service", [""])[0] == "git-upload-pack":
            return await self._git_host(request, git_info.group(1), git_info.group(2))
        git_pack = GIT_PACK_RE.match(url.path)
        if git_pack and method_name(request) == "POST":
            return await self._git_host(request, git_pack.group(1), git_pack.group(2))

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

        # Persistent data lives in D1, not Durable Objects.
        if url.path in ("/api/repositories", "/api/repositories/"):
            return await catalog_handler(self.env, request)

        if url.path in ("/api/accounts/signup", "/api/accounts/login") or \
                ACCOUNTS_RE.match(url.path):
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


class ForkMeshRoom(DurableObject):
    async def fetch(self, request):
        self._ensure_state()
        path = urlparse(request.url).path
        is_watch = path.endswith("/clients")
        upgrade = request.headers.get("upgrade")
        is_websocket = bool(upgrade) and upgrade.lower() == "websocket"

        if not is_websocket:
            # Non-WebSocket request: one-shot snapshot of the live client count.
            return json_response({"ok": True, "clients": len(self.sockets)})

        if is_watch:
            # A /clients WebSocket is a read-only observer (e.g. the website):
            # it never joins the chat and is not counted as a client. The room
            # pushes it the live client count now and on every join/leave.
            if len(self.observers) >= MAX_OBSERVERS:
                return json_response({"error": "observers_full"}, status=429)
        elif len(self.sockets) >= MAX_CONNECTIONS:
            return json_response({"error": "room_full"}, status=429)

        client, server = WebSocketPair.new().object_values()
        if is_watch:
            self._accept_observer(server)
        else:
            self._accept(server)

        return JsResponse.new(
            None,
            to_js(
                {
                    "status": 101,
                    "webSocket": client,
                }
            ),
        )

    def _ensure_state(self):
        if not hasattr(self, "sockets"):
            self.sockets = []
        if not hasattr(self, "observers"):
            self.observers = []
        if not hasattr(self, "callbacks"):
            self.callbacks = []

    def _accept(self, socket):
        self._ensure_state()
        socket.accept()
        self.sockets.append(socket)
        self._notify_observers()

        def on_message(event):
            data = event.data
            if not isinstance(data, str):
                socket.close(1003, "text frames only")
                return

            if len(data.encode("utf-8")) > MAX_TEXT_BYTES:
                socket.close(1009, "message too large")
                return

            self._broadcast(data, socket)

        def forget(_event=None):
            self._forget(socket)

        message_proxy = create_proxy(on_message)
        close_proxy = create_proxy(forget)
        error_proxy = create_proxy(forget)
        self.callbacks.extend([message_proxy, close_proxy, error_proxy])

        socket.addEventListener("message", message_proxy)
        socket.addEventListener("close", close_proxy)
        socket.addEventListener("error", error_proxy)

    def _broadcast(self, payload, sender):
        self._ensure_state()
        stale = []
        for socket in list(self.sockets):
            if socket is sender:
                continue
            try:
                socket.send(payload)
            except Exception:
                stale.append(socket)

        for socket in stale:
            self._forget(socket)

    def _forget(self, socket):
        self._ensure_state()
        self.sockets = [existing for existing in self.sockets if existing is not socket]
        self._notify_observers()

    def _accept_observer(self, socket):
        self._ensure_state()
        socket.accept()
        self.observers.append(socket)

        def forget(_event=None):
            self._forget_observer(socket)

        close_proxy = create_proxy(forget)
        error_proxy = create_proxy(forget)
        self.callbacks.extend([close_proxy, error_proxy])
        socket.addEventListener("close", close_proxy)
        socket.addEventListener("error", error_proxy)

        # Send the current count immediately so the page renders without a wait.
        self._send_count(socket)

    def _forget_observer(self, socket):
        self._ensure_state()
        self.observers = [
            existing for existing in self.observers if existing is not socket
        ]

    def _send_count(self, socket):
        try:
            socket.send(json.dumps({"clients": len(self.sockets)}))
        except Exception:
            self._forget_observer(socket)

    def _notify_observers(self):
        self._ensure_state()
        payload = json.dumps({"clients": len(self.sockets)})
        stale = []
        for socket in list(self.observers):
            try:
                socket.send(payload)
            except Exception:
                stale.append(socket)
        for socket in stale:
            self._forget_observer(socket)


class ForkMeshHost(DurableObject):
    # Live tunnel for a single repository. Desktop clients that mirror the repo
    # connect here as "hosts" and answer tree/blob requests by reading their
    # local Git mirror. The website's /tree and /blob calls are forwarded to the
    # host with the best (lowest round-trip) connection. Nothing is stored: when
    # no host is connected, the repo is simply unavailable.
    def _ensure(self):
        if not hasattr(self, "hosts"):
            self.hosts = []  # [{socket, id, rtt, alive}]
        if not hasattr(self, "pending"):
            self.pending = {}  # reqId -> asyncio.Future
        if not hasattr(self, "callbacks"):
            self.callbacks = []
        if not hasattr(self, "counter"):
            self.counter = 0
        if not hasattr(self, "git_buffers"):
            self.git_buffers = {}  # reqId -> bytearray for chunked git output

    async def fetch(self, request):
        self._ensure()
        url = urlparse(request.url)
        path = url.path
        upgrade = request.headers.get("upgrade")
        is_websocket = bool(upgrade) and upgrade.lower() == "websocket"

        # Git smart-HTTP clone endpoints proxied to the host's git upload-pack.
        if path.endswith("/info/refs"):
            return await self._git(request, "git-info-refs")
        if path.endswith("/git-upload-pack"):
            body = b""
            try:
                body = bytes(await request.bytes())
            except Exception:
                body = b""
            return await self._git(request, "git-upload-pack", body)

        action = path.rsplit("/", 1)[-1]
        if action == "host":
            if not is_websocket:
                return json_response({"ok": True, "hosts": len(self.hosts)})
            client, server = WebSocketPair.new().object_values()
            self._accept_host(server)
            return JsResponse.new(
                None, to_js({"status": 101, "webSocket": client})
            )

        rel_path = (parse_qs(url.query).get("path", [""])[0] or "").strip()
        if action in ("tree", "blob", "commits", "commit"):
            return await self._tunnel(action, rel_path)
        return json_response({"error": "not_found"}, status=404)

    def _accept_host(self, socket):
        self._ensure()
        socket.accept()
        self.counter += 1
        entry = {"socket": socket, "id": self.counter, "rtt": float("inf"),
                 "alive": True}
        self.hosts.append(entry)

        def on_message(event):
            data = event.data
            if not isinstance(data, str):
                return
            try:
                msg = json.loads(data)
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

        def forget(_event=None):
            self._forget_host(entry)

        message_proxy = create_proxy(on_message)
        close_proxy = create_proxy(forget)
        error_proxy = create_proxy(forget)
        self.callbacks.extend([message_proxy, close_proxy, error_proxy])
        socket.addEventListener("message", message_proxy)
        socket.addEventListener("close", close_proxy)
        socket.addEventListener("error", error_proxy)

    def _forget_host(self, entry):
        self._ensure()
        entry["alive"] = False
        self.hosts = [h for h in self.hosts if h is not entry]

    def _best_host(self):
        live = [h for h in self.hosts if h.get("alive")]
        if not live:
            return None
        # Prefer the lowest measured round-trip time; unmeasured hosts (inf)
        # fall to the back until they've served at least one request.
        return min(live, key=lambda h: h.get("rtt", float("inf")))

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
            host["socket"].send(
                json.dumps({"type": "request", "reqId": req_id, "op": op,
                            "path": rel_path})
            )
        except Exception:
            self.pending.pop(req_id, None)
            self._forget_host(host)
            return json_response(
                {"ok": False, "error": "host_unavailable"}, status=503
            )

        try:
            msg = await asyncio.wait_for(future, timeout=TUNNEL_TIMEOUT_MS / 1000)
        except Exception:
            self.pending.pop(req_id, None)
            return json_response({"ok": False, "error": "timeout"}, status=504)

        # Update the host's round-trip estimate (exponential moving average).
        elapsed = Date.now() - started
        host["rtt"] = (
            elapsed
            if host.get("rtt", float("inf")) == float("inf")
            else 0.5 * host["rtt"] + 0.5 * elapsed
        )

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
            host["socket"].send(json.dumps(message))
        except Exception:
            self.pending.pop(req_id, None)
            self.git_buffers.pop(req_id, None)
            self._forget_host(host)
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
