import asyncio
import json
import re
from urllib.parse import parse_qs, unquote, urlparse

from js import Date
from js import Object
from js import Response as JsResponse
from js import WebSocketPair
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
# Each room exposes a WebSocket (/ws) and a read-only live client count
# (/clients); the Durable Object picks behavior from the upgrade header.
ROOM_RE = re.compile(r"^/api/room/([^/]+)/(?:ws|clients)$")
REPO_ROOM_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/rooms/([^/]+)/(?:ws|clients)$")
REPO_FILES_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/files$")
# Live tunnel: desktop clients connect to /host; the website pulls /tree and
# /blob, which the worker forwards to the best-connected host.
REPO_HOST_RE = re.compile(r"^/api/repo/([^/]+)/([^/]+)/(host|tree|blob)$")
ROOM_NAME_RE = re.compile(r"^[A-Za-z0-9._:-]+$")
TUNNEL_TIMEOUT_MS = 20000


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

        files_match = REPO_FILES_RE.match(url.path)
        if files_match:
            owner = safe_segment(files_match.group(1))
            repo = safe_segment(files_match.group(2))
            if not owner or not repo:
                return json_response({"error": "not_found"}, status=404)
            files_id = self.env.FORKMESH_FILES.idFromName(f"files:{owner}/{repo}")
            files_object = self.env.FORKMESH_FILES.get(files_id)
            return await files_object.fetch(request)

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

    async def fetch(self, request):
        self._ensure()
        url = urlparse(request.url)
        action = url.path.rsplit("/", 1)[-1]
        upgrade = request.headers.get("upgrade")
        is_websocket = bool(upgrade) and upgrade.lower() == "websocket"

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
            if msg.get("type") != "response":
                return
            fut = self.pending.pop(msg.get("reqId"), None)
            if fut is not None and not fut.done():
                fut.set_result(msg)

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
