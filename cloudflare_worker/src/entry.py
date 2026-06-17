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
# Each room exposes a WebSocket (/ws) and a read-only live client count
# (/clients); the Durable Object picks behavior from the upgrade header.
ROOM_RE = re.compile(r"^/api/room/([^/]+)/(?:ws|clients)$")
REPO_ROOM_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/rooms/([^/]+)/(?:ws|clients)$")
REPO_FILES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/files$")
# Issue inbox: signed submissions from people without write access to the repo.
REPO_ISSUES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/issues$")
# Live tunnel: desktop clients connect to /host; the website pulls /tree and
# /blob, which the worker forwards to the best-connected host.
REPO_HOST_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blob)$")
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


def safe_files_record(data):
    if not isinstance(data, dict):
        return None

    owner = safe_segment(data.get("owner", ""))
    name = safe_segment(data.get("name", ""))
    public_key = clean_string(data.get("maintainer", ""), 120)
    if not owner or not name or not public_key:
        return None

    raw_files = data.get("files")
    if not isinstance(raw_files, list):
        return None

    files = []
    for item in raw_files[:MAX_FILES]:
        if not isinstance(item, dict):
            continue
        path = clean_string(item.get("path", ""), 512)
        if not path:
            continue
        try:
            size = int(item.get("size", 0))
        except (TypeError, ValueError):
            size = 0
        files.append({"path": path, "size": max(size, 0)})

    return {
        "owner": owner,
        "name": name,
        "branch": clean_string(data.get("branch", ""), 80),
        "updatedAt": clean_string(data.get("updatedAt", ""), 32),
        "maintainer": public_key,
        "signature": clean_string(data.get("signature", ""), 220),
        "files": files,
    }


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

        if url.path in ("/api/repositories", "/api/repositories/"):
            catalog_id = self.env.FORKMESH_CATALOG.idFromName("global")
            catalog = self.env.FORKMESH_CATALOG.get(catalog_id)
            return await catalog.fetch(request)

        if url.path in ("/api/accounts/signup", "/api/accounts/login") or \
                ACCOUNTS_RE.match(url.path):
            accounts_id = self.env.FORKMESH_ACCOUNTS.idFromName("global")
            accounts = self.env.FORKMESH_ACCOUNTS.get(accounts_id)
            return await accounts.fetch(request)

        files_match = REPO_FILES_RE.match(url.path)
        if files_match:
            owner = safe_segment(files_match.group(1))
            repo = safe_segment(files_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            files_id = self.env.FORKMESH_FILES.idFromName(f"files:{owner}/{repo}")
            files_object = self.env.FORKMESH_FILES.get(files_id)
            return await files_object.fetch(request)

        issues_match = REPO_ISSUES_RE.match(url.path)
        if issues_match:
            owner = safe_segment(issues_match.group(1))
            repo = safe_segment(issues_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            issues_id = self.env.FORKMESH_ISSUES.idFromName(f"issues:{owner}/{repo}")
            issues_object = self.env.FORKMESH_ISSUES.get(issues_id)
            return await issues_object.fetch(request)

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


class ForkMeshCatalog(DurableObject):
    async def fetch(self, request):
        method = method_name(request)
        if method == "GET":
            records = await self._records()
            return json_response(
                {
                    "ok": True,
                    "repositories": sorted(
                        records.values(),
                        key=lambda repo: repo.get("updatedAt", ""),
                        reverse=True,
                    ),
                }
            )

        if method == "POST":
            try:
                data = await request.json()
            except Exception:
                return json_response({"error": "invalid_json"}, status=400)

            record = safe_catalog_record(data)
            if not record:
                return json_response(
                    {"error": "owner_name_and_maintainer_required"},
                    status=400,
                )

            key = (
                record["maintainer"] + ":" + record["owner"] + "/" + record["name"]
            )
            records = await self._records()
            records[key] = record
            if len(records) > MAX_CATALOG_REPOS:
                ordered = sorted(
                    records.items(),
                    key=lambda item: item[1].get("updatedAt", ""),
                    reverse=True,
                )
                records = dict(ordered[:MAX_CATALOG_REPOS])

            await self.ctx.storage.put("repositories", records)
            return json_response({"ok": True, "repository": record}, status=201)

        return json_response({"error": "method_not_allowed"}, status=405)

    async def _records(self):
        records = await self.ctx.storage.get("repositories")
        return records if isinstance(records, dict) else {}


class ForkMeshFiles(DurableObject):
    # Stores a per-repository file listing (paths + sizes only, no contents) so
    # the website can let visitors traverse the repository tree. File data
    # itself stays on the desktop node's local mirror.
    async def fetch(self, request):
        method = method_name(request)
        if method == "GET":
            record = await self.ctx.storage.get("files")
            if not isinstance(record, dict):
                return json_response({"ok": True, "exists": False, "files": []})
            return json_response({"ok": True, "exists": True, **record})

        if method == "POST":
            try:
                data = await request.json()
            except Exception:
                return json_response({"error": "invalid_json"}, status=400)

            record = safe_files_record(data)
            if not record:
                return json_response(
                    {"error": "owner_name_and_maintainer_required"},
                    status=400,
                )

            await self.ctx.storage.put("files", record)
            return json_response(
                {"ok": True, "count": len(record["files"])}, status=201
            )

        return json_response({"error": "method_not_allowed"}, status=405)


class ForkMeshIssues(DurableObject):
    # Submission inbox for one repository. Canonical issues live in the repo's
    # issues/ folder (git). People without write access POST signed issue/comment
    # events here; the repo owner's node pulls them (owner-authenticated), merges
    # them into the folder, commits, and acks to clear the inbox. Every submission
    # is signature-verified before it is accepted (see verify_issue_event).
    async def _pending(self):
        data = await self.ctx.storage.get("pending")
        return data if isinstance(data, list) else []

    def _owner_from_path(self, request):
        match = REPO_ISSUES_RE.match(urlparse(request.url).path)
        return safe_segment(match.group(1)) if match else None

    async def fetch(self, request):
        method = method_name(request)
        if method == "POST":
            return await self._submit(request)
        if method == "GET":
            return await self._list(request)
        if method == "DELETE":
            return await self._ack(request)
        return json_response({"error": "method_not_allowed"}, status=405)

    async def _submit(self, request):
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

        body_text = event.get("body", "") or ""
        if len(body_text.encode("utf-8")) > MAX_ISSUE_BYTES:
            return json_response({"error": "issue_too_large"}, status=413)

        if not await verify_issue_event(number, event):
            return json_response({"error": "bad_signature"}, status=401)

        pending = await self._pending()
        if len(pending) >= MAX_PENDING_ISSUES:
            return json_response({"error": "inbox_full"}, status=429)
        pending.append(
            {
                "number": number,
                "titleIfNew": clean_string(data.get("titleIfNew", ""), 240),
                "event": event,
                "submitter": clean_string(event.get("author", ""), 120),
                "submittedAt": int(Date.now()),
            }
        )
        await self.ctx.storage.put("pending", pending)
        return json_response({"ok": True, "pending": len(pending)}, status=201)

    async def _owner_pubkey(self, owner):
        # Look up the repo owner's registered account public key via the accounts
        # Durable Object, so only the owner can pull/ack the inbox.
        try:
            from js import Request as JsRequest

            accounts_id = self.env.FORKMESH_ACCOUNTS.idFromName("global")
            accounts = self.env.FORKMESH_ACCOUNTS.get(accounts_id)
            resp = await accounts.fetch(
                JsRequest.new("https://do/api/accounts/" + owner)
            )
            record = json.loads(await resp.text())
            return record.get("pubkey", "") if record.get("exists") else ""
        except Exception:
            return ""

    async def _authorize_owner(self, request):
        owner = self._owner_from_path(request)
        if not owner:
            return False
        params = parse_qs(urlparse(request.url).query)
        ts = params.get("ts", [""])[0]
        sig = params.get("sig", [""])[0]
        owner_pub = await self._owner_pubkey(owner)
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

    async def _list(self, request):
        if not await self._authorize_owner(request):
            return json_response({"error": "unauthorized"}, status=401)
        return json_response({"ok": True, "pending": await self._pending()})

    async def _ack(self, request):
        if not await self._authorize_owner(request):
            return json_response({"error": "unauthorized"}, status=401)
        await self.ctx.storage.put("pending", [])
        return json_response({"ok": True})


class ForkMeshAccounts(DurableObject):
    # The website's account database. Each node registers a unique node name
    # bound to its Ed25519 public key, plus an email encrypted at rest. Signup
    # and login are proven by signing a canonical string with the node key, so
    # only the node that holds the private key can claim or use a name.
    async def _accounts(self):
        accounts = await self.ctx.storage.get("accounts")
        return accounts if isinstance(accounts, dict) else {}

    async def _email_key(self):
        secret = getattr(self.env, "ACCOUNTS_KEY", "forkmesh-dev-accounts-key")
        digest = await js_crypto.subtle.digest("SHA-256", _to_js(secret.encode()))
        return await js_crypto.subtle.importKey(
            "raw", digest, to_js({"name": "AES-GCM"}), False,
            _to_js(["encrypt", "decrypt"])
        )

    async def _encrypt_email(self, email):
        key = await self._email_key()
        iv = js_crypto.getRandomValues(Uint8Array.new(12))
        cipher = await js_crypto.subtle.encrypt(
            to_js({"name": "AES-GCM", "iv": iv}), key, _to_js(email.encode())
        )
        blob = bytes(iv.to_py()) + bytes(Uint8Array.new(cipher).to_py())
        return base64.b64encode(blob).decode()

    async def _decrypt_email(self, stored):
        try:
            blob = base64.b64decode(stored)
            iv = _to_js(blob[:12])
            cipher = _to_js(blob[12:])
            key = await self._email_key()
            plain = await js_crypto.subtle.decrypt(
                to_js({"name": "AES-GCM", "iv": iv}), key, cipher
            )
            return bytes(Uint8Array.new(plain).to_py()).decode()
        except Exception:
            return ""

    async def fetch(self, request):
        url = urlparse(request.url)
        method = method_name(request)

        if url.path == "/api/accounts/signup" and method == "POST":
            return await self._signup(request)
        if url.path == "/api/accounts/login" and method == "POST":
            return await self._login(request)

        match = ACCOUNTS_RE.match(url.path)
        if match and method == "GET":
            name = match.group(1)
            accounts = await self._accounts()
            record = accounts.get(name)
            if not record:
                return json_response({"ok": True, "exists": False, "name": name})
            # Public lookup never returns the email.
            return json_response(
                {"ok": True, "exists": True, "name": name,
                 "pubkey": record.get("pubkey", ""),
                 "createdAt": record.get("createdAt", 0)}
            )

        return json_response({"error": "not_found"}, status=404)

    async def _signup(self, request):
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

        accounts = await self._accounts()
        existing = accounts.get(name)
        if existing and existing.get("pubkey") != pubkey:
            return json_response({"error": "node_name_taken"}, status=409)

        accounts[name] = {
            "pubkey": pubkey,
            "emailEnc": await self._encrypt_email(email),
            "createdAt": existing.get("createdAt", int(Date.now())) if existing
            else int(Date.now()),
        }
        await self.ctx.storage.put("accounts", accounts)
        return json_response({"ok": True, "nodeName": name}, status=201)

    async def _login(self, request):
        try:
            data = await request.json()
        except Exception:
            return json_response({"error": "invalid_json"}, status=400)

        name = clean_string(data.get("nodeName", ""), 32).lower()
        ts = clean_string(data.get("ts", ""), 20)
        signature = clean_string(data.get("sig", ""), 200)
        accounts = await self._accounts()
        record = accounts.get(name)
        if not record:
            return json_response({"error": "no_such_account"}, status=404)

        try:
            skew = abs(int(Date.now()) - int(ts))
        except (TypeError, ValueError):
            skew = LOGIN_MAX_SKEW_MS + 1
        if skew > LOGIN_MAX_SKEW_MS:
            return json_response({"error": "stale_timestamp"}, status=401)

        canonical = ("forkmesh-login-v1\n" + name + "\n" + ts).encode()
        if not await ed25519_verify(record.get("pubkey", ""), signature, canonical):
            return json_response({"error": "bad_signature"}, status=401)

        # The owner proved key possession, so return their decrypted email.
        return json_response(
            {"ok": True, "nodeName": name, "email": await self._decrypt_email(
                record.get("emailEnc", ""))}
        )


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
        if action in ("tree", "blob"):
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
