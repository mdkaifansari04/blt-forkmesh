import asyncio
import base64
import json
import re
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
# file/git tunnel). Everything that must persist — the repo catalog, accounts,
# and the issue/pull submission inboxes — lives in the worker's D1 database.

_schema_ready = False

SCHEMA_STATEMENTS = [
    """CREATE TABLE IF NOT EXISTS repositories (
        key TEXT PRIMARY KEY, owner TEXT NOT NULL, name TEXT NOT NULL,
        description TEXT, clone_url TEXT, bch TEXT, channel TEXT,
        hosted_since TEXT, last_sync TEXT, updated_at TEXT, source TEXT,
        maintainer TEXT NOT NULL, signature TEXT)""",
    "CREATE INDEX IF NOT EXISTS idx_repos_updated ON repositories(updated_at DESC)",
    """CREATE TABLE IF NOT EXISTS accounts (
        name TEXT PRIMARY KEY, pubkey TEXT NOT NULL, email_enc TEXT,
        created_at INTEGER)""",
    """CREATE TABLE IF NOT EXISTS issue_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, owner TEXT NOT NULL,
        repo TEXT NOT NULL, number INTEGER, title_if_new TEXT,
        event TEXT NOT NULL, submitter TEXT, submitted_at INTEGER)""",
    "CREATE INDEX IF NOT EXISTS idx_issue_inbox_repo ON issue_inbox(owner, repo)",
    """CREATE TABLE IF NOT EXISTS pull_inbox (
        id INTEGER PRIMARY KEY AUTOINCREMENT, owner TEXT NOT NULL,
        repo TEXT NOT NULL, pull TEXT NOT NULL, submitter TEXT,
        submitted_at INTEGER)""",
    "CREATE INDEX IF NOT EXISTS idx_pull_inbox_repo ON pull_inbox(owner, repo)",
]


async def ensure_schema(env):
    # Create the tables on first use per isolate (idempotent CREATE IF NOT EXISTS).
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


# --- Catalog (repositories table) -------------------------------------------

def _repo_row_to_json(r):
    return {
        "owner": r.get("owner", ""),
        "name": r.get("name", ""),
        "description": r.get("description", "") or "",
        "cloneUrl": r.get("clone_url", "") or "",
        "bch": r.get("bch", "") or "",
        "channel": r.get("channel", "") or "",
        "hostedSince": r.get("hosted_since", "") or "",
        "lastSync": r.get("last_sync", "") or "",
        "updatedAt": r.get("updated_at", "") or "",
        "source": r.get("source", "local-node") or "local-node",
        "maintainer": r.get("maintainer", ""),
        "signature": r.get("signature", "") or "",
    }


async def catalog_handler(env, request):
    await ensure_schema(env)
    method = method_name(request)
    if method == "GET":
        rows = await d1_all(
            env,
            "SELECT * FROM repositories ORDER BY updated_at DESC LIMIT ?",
            MAX_CATALOG_REPOS,
        )
        return json_response(
            {"ok": True, "repositories": [_repo_row_to_json(r) for r in rows]}
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
        key = record["maintainer"] + ":" + record["owner"] + "/" + record["name"]
        await d1_run(
            env,
            """INSERT INTO repositories
               (key, owner, name, description, clone_url, bch, channel,
                hosted_since, last_sync, updated_at, source, maintainer, signature)
               VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)
               ON CONFLICT(key) DO UPDATE SET
                 description=excluded.description, clone_url=excluded.clone_url,
                 bch=excluded.bch, channel=excluded.channel,
                 hosted_since=excluded.hosted_since, last_sync=excluded.last_sync,
                 updated_at=excluded.updated_at, source=excluded.source,
                 signature=excluded.signature""",
            key, record["owner"], record["name"], record["description"],
            record["cloneUrl"], record["bch"], record["channel"],
            record["hostedSince"], record["lastSync"], record["updatedAt"],
            record["source"], record["maintainer"], record["signature"],
        )
        # Keep only the most-recent MAX_CATALOG_REPOS records.
        await d1_run(
            env,
            """DELETE FROM repositories WHERE key NOT IN
               (SELECT key FROM repositories ORDER BY updated_at DESC LIMIT ?)""",
            MAX_CATALOG_REPOS,
        )
        return json_response({"ok": True, "repository": record}, status=201)

    return json_response({"error": "method_not_allowed"}, status=405)


# --- Accounts (accounts table) ----------------------------------------------

async def _email_key(env):
    secret = getattr(env, "ACCOUNTS_KEY", "forkmesh-dev-accounts-key")
    digest = await js_crypto.subtle.digest("SHA-256", _to_js(secret.encode()))
    return await js_crypto.subtle.importKey(
        "raw", digest, to_js({"name": "AES-GCM"}), False,
        _to_js(["encrypt", "decrypt"])
    )


async def _encrypt_email(env, email):
    key = await _email_key(env)
    iv = js_crypto.getRandomValues(Uint8Array.new(12))
    cipher = await js_crypto.subtle.encrypt(
        to_js({"name": "AES-GCM", "iv": iv}), key, _to_js(email.encode())
    )
    blob = bytes(iv.to_py()) + bytes(Uint8Array.new(cipher).to_py())
    return base64.b64encode(blob).decode()


async def _decrypt_email(env, stored):
    try:
        blob = base64.b64decode(stored)
        iv = _to_js(blob[:12])
        cipher = _to_js(blob[12:])
        key = await _email_key(env)
        plain = await js_crypto.subtle.decrypt(
            to_js({"name": "AES-GCM", "iv": iv}), key, cipher
        )
        return bytes(Uint8Array.new(plain).to_py()).decode()
    except Exception:
        return ""


async def _account_signup(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)

    name = clean_string(data.get("nodeName", ""), 32).lower()
    pubkey = clean_string(data.get("pubkey", ""), 120)
    email = clean_string(data.get("email", ""), 254)
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    if not valid_node_name(name):
        return json_response(
            {"error": "node_name_must_be_lowercase_alphanumeric_starting_with_a_letter"},
            status=400,
        )
    if not pubkey or "@" not in email or not ts:
        return json_response({"error": "pubkey_email_and_ts_required"}, status=400)

    canonical = ("forkmesh-account-v1\n" + name + "\n" + email + "\n" + ts).encode()
    if not await ed25519_verify(pubkey, signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)

    existing = await d1_first(
        env, "SELECT pubkey, created_at FROM accounts WHERE name = ?", name
    )
    if existing and existing.get("pubkey") != pubkey:
        return json_response({"error": "node_name_taken"}, status=409)

    email_enc = await _encrypt_email(env, email)
    created = existing.get("created_at") if existing else int(Date.now())
    await d1_run(
        env,
        """INSERT INTO accounts (name, pubkey, email_enc, created_at)
           VALUES (?,?,?,?)
           ON CONFLICT(name) DO UPDATE SET
             pubkey=excluded.pubkey, email_enc=excluded.email_enc""",
        name, pubkey, email_enc, int(created),
    )
    return json_response({"ok": True, "nodeName": name}, status=201)


async def _account_login(env, request):
    try:
        data = await request.json()
    except Exception:
        return json_response({"error": "invalid_json"}, status=400)

    name = clean_string(data.get("nodeName", ""), 32).lower()
    ts = clean_string(data.get("ts", ""), 20)
    signature = clean_string(data.get("sig", ""), 200)
    row = await d1_first(
        env, "SELECT pubkey, email_enc FROM accounts WHERE name = ?", name
    )
    if not row:
        return json_response({"error": "no_such_account"}, status=404)

    try:
        skew = abs(int(Date.now()) - int(ts))
    except (TypeError, ValueError):
        skew = LOGIN_MAX_SKEW_MS + 1
    if skew > LOGIN_MAX_SKEW_MS:
        return json_response({"error": "stale_timestamp"}, status=401)

    canonical = ("forkmesh-login-v1\n" + name + "\n" + ts).encode()
    if not await ed25519_verify(row.get("pubkey", ""), signature, canonical):
        return json_response({"error": "bad_signature"}, status=401)

    return json_response(
        {"ok": True, "nodeName": name,
         "email": await _decrypt_email(env, row.get("email_enc", ""))}
    )


async def accounts_handler(env, request):
    await ensure_schema(env)
    url = urlparse(request.url)
    method = method_name(request)
    if url.path == "/api/accounts/signup" and method == "POST":
        return await _account_signup(env, request)
    if url.path == "/api/accounts/login" and method == "POST":
        return await _account_login(env, request)
    match = ACCOUNTS_RE.match(url.path)
    if match and method == "GET":
        name = match.group(1)
        row = await d1_first(
            env, "SELECT pubkey, created_at FROM accounts WHERE name = ?", name
        )
        if not row:
            return json_response({"ok": True, "exists": False, "name": name})
        # Public lookup never returns the email.
        return json_response(
            {"ok": True, "exists": True, "name": name,
             "pubkey": row.get("pubkey", ""), "createdAt": row.get("created_at", 0)}
        )
    return json_response({"error": "not_found"}, status=404)


# --- Issue & pull submission inboxes (issue_inbox / pull_inbox tables) -------

async def _owner_pubkey(env, owner):
    row = await d1_first(env, "SELECT pubkey FROM accounts WHERE name = ?", owner)
    return row.get("pubkey", "") if row else ""


async def _authorize_owner(env, request, owner):
    # The repo owner pulls/acks their inbox by signing forkmesh-issues-pull-v1
    # with the node key registered to their account.
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
            env, "SELECT COUNT(*) AS c FROM issue_inbox WHERE owner=? AND repo=?",
            owner, repo,
        )
        if count and count.get("c", 0) >= MAX_PENDING_ISSUES:
            return json_response({"error": "inbox_full"}, status=429)
        await d1_run(
            env,
            """INSERT INTO issue_inbox
               (owner, repo, number, title_if_new, event, submitter, submitted_at)
               VALUES (?,?,?,?,?,?,?)""",
            owner, repo, number, clean_string(data.get("titleIfNew", ""), 240),
            json.dumps(event), clean_string(event.get("author", ""), 120),
            int(Date.now()),
        )
        return json_response({"ok": True}, status=201)

    if method == "GET":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        rows = await d1_all(
            env,
            """SELECT number, title_if_new, event, submitter, submitted_at
               FROM issue_inbox WHERE owner=? AND repo=? ORDER BY id ASC""",
            owner, repo,
        )
        pending = []
        for r in rows:
            try:
                ev = json.loads(r.get("event") or "{}")
            except Exception:
                ev = {}
            pending.append({
                "number": r.get("number", 0),
                "titleIfNew": r.get("title_if_new", "") or "",
                "event": ev,
                "submitter": r.get("submitter", "") or "",
                "submittedAt": r.get("submitted_at", 0),
            })
        return json_response({"ok": True, "pending": pending})

    if method == "DELETE":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        await d1_run(
            env, "DELETE FROM issue_inbox WHERE owner=? AND repo=?", owner, repo
        )
        return json_response({"ok": True})

    return json_response({"error": "method_not_allowed"}, status=405)


async def pulls_handler(env, request, owner, repo):
    await ensure_schema(env)
    method = method_name(request)
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
            env, "SELECT COUNT(*) AS c FROM pull_inbox WHERE owner=? AND repo=?",
            owner, repo,
        )
        if count and count.get("c", 0) >= MAX_PENDING_PULLS:
            return json_response({"error": "inbox_full"}, status=429)
        await d1_run(
            env,
            """INSERT INTO pull_inbox (owner, repo, pull, submitter, submitted_at)
               VALUES (?,?,?,?,?)""",
            owner, repo, json.dumps(pull),
            clean_string(pull.get("author", ""), 120), int(Date.now()),
        )
        return json_response({"ok": True}, status=201)

    if method == "GET":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        rows = await d1_all(
            env,
            """SELECT pull, submitter, submitted_at FROM pull_inbox
               WHERE owner=? AND repo=? ORDER BY id ASC""",
            owner, repo,
        )
        pending = []
        for r in rows:
            try:
                pj = json.loads(r.get("pull") or "{}")
            except Exception:
                pj = {}
            pending.append({
                "pull": pj,
                "submitter": r.get("submitter", "") or "",
                "submittedAt": r.get("submitted_at", 0),
            })
        return json_response({"ok": True, "pending": pending})

    if method == "DELETE":
        if not await _authorize_owner(env, request, owner):
            return json_response({"error": "unauthorized"}, status=401)
        await d1_run(
            env, "DELETE FROM pull_inbox WHERE owner=? AND repo=?", owner, repo
        )
        return json_response({"ok": True})

    return json_response({"error": "method_not_allowed"}, status=405)


class Default(WorkerEntrypoint):
    async def fetch(self, request):
        url = urlparse(request.url)

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
