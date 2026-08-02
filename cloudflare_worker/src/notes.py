"""Encrypted collaborative notes shared by the web, World, and desktop apps.

The adapter supplied by ``entry.py`` owns runtime-specific concerns (D1,
account sessions, encryption and Durable Objects).  This module keeps the API
contract and authorization rules independently testable.
"""

import re


PREFIX = "/api/notes"
MAX_BODY_BYTES = 1024 * 1024
MAX_TITLE = 200
MAX_MARKDOWN = 512 * 1024
MAX_NOTES = 500
MAX_VERSIONS = 100
NOTE_ID_RE = re.compile(r"^[0-9a-f]{32}$")
NAME_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
REPO_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")
LINK_KINDS = frozenset({"issue", "pull", "discussion"})
ROLES = frozenset({"viewer", "editor"})


def _response(runtime, payload, status=200, allow=""):
    headers = {"x-content-type-options": "nosniff"}
    if allow:
        headers["allow"] = allow
    return runtime.response(
        payload, status=status,
        cache_control="no-store, max-age=0, must-revalidate",
        extra_headers=headers,
    )


def _title(value):
    return " ".join(str(value or "").replace("<", " ").replace(">", " ").split())[:MAX_TITLE]


def _markdown(value):
    value = str(value or "").replace("\x00", "")
    return value[:MAX_MARKDOWN]


def _route(path):
    clean = str(path or "").rstrip("/")
    if clean == PREFIX:
        return "collection", "", ""
    match = re.fullmatch(
        r"/api/notes/([0-9a-f]{32})(?:/(versions|shares|links|ws))?", clean)
    if not match:
        return None
    return "note", match.group(1), match.group(2) or ""


async def _actor(runtime, data=None):
    account_bi, record = await runtime.session(data or {})
    name = str((record or {}).get("name") or "").strip().lower()
    return str(account_bi or ""), name


async def _row(runtime, note_id):
    return await runtime.d1_first(
        "SELECT note_id,owner_bi,visibility,version,created_at,updated_at,"
        "published_at,data FROM notes WHERE note_id=?", note_id)


async def _role(runtime, row, account_bi):
    if not row:
        return ""
    if account_bi and str(row.get("owner_bi") or "") == account_bi:
        return "owner"
    if account_bi:
        direct = await runtime.d1_first(
            "SELECT role FROM note_shares WHERE note_id=? "
            "AND principal_type='user' AND principal_bi=?",
            row["note_id"], account_bi)
        if direct:
            return str(direct.get("role") or "viewer")
        organization = await runtime.d1_first(
            "SELECT s.role FROM note_shares s INNER JOIN org_members m "
            "ON m.org_bi=s.principal_bi WHERE s.note_id=? "
            "AND s.principal_type='organization' AND m.member_bi=? "
            "ORDER BY CASE s.role WHEN 'editor' THEN 0 ELSE 1 END LIMIT 1",
            row["note_id"], account_bi)
        if organization:
            return str(organization.get("role") or "viewer")
    if str(row.get("visibility") or "") == "public":
        return "public"
    return ""


async def _shares(runtime, note_id):
    rows = await runtime.d1_all(
        "SELECT principal_type,principal_name,role,created_at "
        "FROM note_shares WHERE note_id=? "
        "ORDER BY principal_type,principal_name COLLATE NOCASE", note_id)
    return [{
        "type": str(row.get("principal_type") or ""),
        "name": str(row.get("principal_name") or ""),
        "role": str(row.get("role") or "viewer"),
        "createdAt": int(row.get("created_at") or 0),
    } for row in rows or []]


async def _links(runtime, note_id):
    rows = await runtime.d1_all(
        "SELECT owner,repo,kind,number,created_at FROM note_links "
        "WHERE note_id=? ORDER BY created_at,owner,repo,kind,number", note_id)
    return [{
        "owner": str(row.get("owner") or ""),
        "repo": str(row.get("repo") or ""),
        "kind": str(row.get("kind") or ""),
        "number": int(row.get("number") or 0),
        "createdAt": int(row.get("created_at") or 0),
    } for row in rows or []]


async def _project(runtime, row, role, full=True):
    data = await runtime.open(row.get("data"))
    data = data if isinstance(data, dict) else {}
    result = {
        "id": row["note_id"],
        "title": _title(data.get("title")) or "Untitled note",
        "visibility": str(row.get("visibility") or "private"),
        "version": int(row.get("version") or 1),
        "createdAt": int(row.get("created_at") or 0),
        "updatedAt": int(row.get("updated_at") or 0),
        "publishedAt": int(row.get("published_at") or 0),
        "role": role,
    }
    if full:
        result["markdown"] = _markdown(data.get("markdown"))
        # Publishing exposes the authored document, not its ACL or possibly
        # private repository context. An authenticated collaborator receives
        # the management metadata; an anonymous public reader never does.
        if role != "public":
            result["shares"] = await _shares(runtime, row["note_id"])
            result["links"] = await _links(runtime, row["note_id"])
            result["webSocketUrl"] = PREFIX + "/" + row["note_id"] + "/ws"
    return result


async def _list(runtime, account_bi):
    rows = await runtime.d1_all(
        "SELECT DISTINCT n.note_id,n.owner_bi,n.visibility,n.version,"
        "n.created_at,n.updated_at,n.published_at,n.data FROM notes n "
        "LEFT JOIN note_shares s ON s.note_id=n.note_id "
        "LEFT JOIN org_members m ON s.principal_type='organization' "
        "AND m.org_bi=s.principal_bi AND m.member_bi=? "
        "WHERE n.owner_bi=? OR (s.principal_type='user' AND s.principal_bi=?) "
        "OR m.member_bi=? ORDER BY n.updated_at DESC LIMIT ?",
        account_bi, account_bi, account_bi, account_bi, MAX_NOTES)
    notes = []
    for row in rows or []:
        role = await _role(runtime, row, account_bi)
        if role:
            notes.append(await _project(runtime, row, role, full=False))
    return _response(runtime, {"ok": True, "notes": notes})


async def _create(runtime, account_bi, actor, data):
    title = _title(data.get("title")) or "Untitled note"
    markdown = _markdown(data.get("markdown"))
    visibility = "public" if data.get("visibility") == "public" else "private"
    now = runtime.now()
    note_id = runtime.new_id()
    sealed = await runtime.seal({"title": title, "markdown": markdown})
    published = now if visibility == "public" else 0
    await runtime.d1_run(
        "INSERT INTO notes(note_id,owner_bi,visibility,version,created_at,"
        "updated_at,published_at,data) VALUES(?,?,?,1,?,?,?,?)",
        note_id, account_bi, visibility, now, now, published, sealed)
    await runtime.d1_run(
        "INSERT INTO note_versions(note_id,version,author_bi,created_at,data) "
        "VALUES(?,1,?,?,?)", note_id, account_bi, now, sealed)
    row = await _row(runtime, note_id)
    await runtime.audit(actor, "note.create", "note", note_id)
    return _response(runtime, {
        "ok": True, "note": await _project(runtime, row, "owner")}, status=201)


async def _update(runtime, row, account_bi, actor, data):
    role = await _role(runtime, row, account_bi)
    if role not in ("owner", "editor"):
        return _response(runtime, {"error": "note_not_found"}, status=404)
    try:
        base_version = int(data.get("baseVersion"))
    except (TypeError, ValueError, OverflowError):
        return _response(runtime, {"error": "base_version_required"}, status=400)
    current_version = int(row.get("version") or 1)
    if base_version != current_version:
        return _response(runtime, {
            "error": "version_conflict",
            "note": await _project(runtime, row, role),
        }, status=409)
    old = await runtime.open(row.get("data"))
    old = old if isinstance(old, dict) else {}
    title = _title(data.get("title", old.get("title"))) or "Untitled note"
    markdown = _markdown(data.get("markdown", old.get("markdown")))
    visibility = str(row.get("visibility") or "private")
    if "visibility" in data:
        if role != "owner" or data.get("visibility") not in ("private", "public"):
            return _response(runtime, {"error": "owner_required"}, status=403)
        visibility = data["visibility"]
    new_version = current_version + 1
    now = runtime.now()
    published = int(row.get("published_at") or 0)
    if visibility == "public" and not published:
        published = now
    if visibility == "private":
        published = 0
    sealed = await runtime.seal({"title": title, "markdown": markdown})
    result = await runtime.d1_run(
        "UPDATE notes SET visibility=?,version=?,updated_at=?,published_at=?,"
        "data=? WHERE note_id=? AND version=?",
        visibility, new_version, now, published, sealed,
        row["note_id"], current_version)
    if runtime.changes(result) != 1:
        latest = await _row(runtime, row["note_id"])
        return _response(runtime, {
            "error": "version_conflict",
            "note": await _project(runtime, latest, await _role(runtime, latest, account_bi)),
        }, status=409)
    await runtime.d1_run(
        "INSERT INTO note_versions(note_id,version,author_bi,created_at,data) "
        "VALUES(?,?,?,?,?)", row["note_id"], new_version, account_bi, now, sealed)
    await runtime.d1_run(
        "DELETE FROM note_versions WHERE note_id=? AND version NOT IN "
        "(SELECT version FROM note_versions WHERE note_id=? "
        "ORDER BY version DESC LIMIT ?)", row["note_id"], row["note_id"], MAX_VERSIONS)
    updated = await _row(runtime, row["note_id"])
    await runtime.broadcast(row["note_id"], {
        "type": "note-updated", "version": new_version,
        "updatedAt": now, "actor": actor})
    return _response(runtime, {
        "ok": True, "note": await _project(runtime, updated, role)})


async def _versions(runtime, row, account_bi, actor, method, data):
    role = await _role(runtime, row, account_bi)
    if not role:
        return _response(runtime, {"error": "note_not_found"}, status=404)
    if method == "GET":
        rows = await runtime.d1_all(
            "SELECT version,created_at FROM note_versions WHERE note_id=? "
            "ORDER BY version DESC LIMIT ?", row["note_id"], MAX_VERSIONS)
        return _response(runtime, {"ok": True, "versions": [{
            "version": int(item.get("version") or 0),
            "createdAt": int(item.get("created_at") or 0),
        } for item in rows or []]})
    if role not in ("owner", "editor"):
        return _response(runtime, {"error": "editor_required"}, status=403)
    try:
        target = int(data.get("version"))
        base = int(data.get("baseVersion"))
    except (TypeError, ValueError, OverflowError):
        return _response(runtime, {"error": "invalid_version"}, status=400)
    version_row = await runtime.d1_first(
        "SELECT data FROM note_versions WHERE note_id=? AND version=?",
        row["note_id"], target)
    if not version_row:
        return _response(runtime, {"error": "version_not_found"}, status=404)
    restored = await runtime.open(version_row.get("data"))
    return await _update(runtime, row, account_bi, actor, {
        "baseVersion": base,
        "title": (restored or {}).get("title", "Untitled note"),
        "markdown": (restored or {}).get("markdown", ""),
    })


async def _share(runtime, row, account_bi, method, data):
    if await _role(runtime, row, account_bi) != "owner":
        return _response(runtime, {"error": "note_not_found"}, status=404)
    if method == "GET":
        return _response(runtime, {"ok": True, "shares": await _shares(runtime, row["note_id"])})
    kind = str(data.get("type") or "").strip().lower()
    name = str(data.get("name") or "").strip().lower()
    role = str(data.get("role") or "viewer").strip().lower()
    if kind not in ("user", "organization") or not NAME_RE.fullmatch(name):
        return _response(runtime, {"error": "invalid_principal"}, status=400)
    principal_bi, canonical = await runtime.principal(kind, name)
    if not principal_bi:
        return _response(runtime, {"error": "principal_not_found"}, status=404)
    if method == "DELETE":
        await runtime.d1_run(
            "DELETE FROM note_shares WHERE note_id=? AND principal_type=? "
            "AND principal_bi=?", row["note_id"], kind, principal_bi)
    else:
        if role not in ROLES:
            return _response(runtime, {"error": "invalid_role"}, status=400)
        await runtime.d1_run(
            "INSERT INTO note_shares(note_id,principal_type,principal_bi,"
            "principal_name,role,added_by_bi,created_at) VALUES(?,?,?,?,?,?,?) "
            "ON CONFLICT(note_id,principal_type,principal_bi) DO UPDATE SET "
            "principal_name=excluded.principal_name,role=excluded.role",
            row["note_id"], kind, principal_bi, canonical, role,
            account_bi, runtime.now())
    # Force existing sockets through the current ACL immediately.
    await runtime.broadcast(row["note_id"], {"type": "access-changed"})
    return _response(runtime, {"ok": True, "shares": await _shares(runtime, row["note_id"])})


async def _link(runtime, row, account_bi, actor, method, data):
    role = await _role(runtime, row, account_bi)
    if role not in ("owner", "editor"):
        return _response(runtime, {"error": "note_not_found"}, status=404)
    if method == "GET":
        return _response(runtime, {"ok": True, "links": await _links(runtime, row["note_id"])})
    owner = str(data.get("owner") or "").strip().lower()
    repo = str(data.get("repo") or "").strip()
    kind = str(data.get("kind") or "").strip().lower()
    try:
        number = int(data.get("number"))
    except (TypeError, ValueError, OverflowError):
        number = 0
    if (not NAME_RE.fullmatch(owner) or not REPO_RE.fullmatch(repo)
            or kind not in LINK_KINDS or number < 1 or number > 999999999):
        return _response(runtime, {"error": "invalid_link"}, status=400)
    if not await runtime.repository_access(owner, repo, actor):
        return _response(runtime, {"error": "repository_not_found"}, status=404)
    if method == "DELETE":
        await runtime.d1_run(
            "DELETE FROM note_links WHERE note_id=? AND owner=? AND repo=? "
            "AND kind=? AND number=?", row["note_id"], owner, repo, kind, number)
    else:
        await runtime.d1_run(
            "INSERT OR IGNORE INTO note_links(note_id,owner,repo,kind,number,created_at) "
            "VALUES(?,?,?,?,?,?)", row["note_id"], owner, repo, kind, number, runtime.now())
    return _response(runtime, {"ok": True, "links": await _links(runtime, row["note_id"])})


async def handle(runtime, path):
    route = _route(path)
    if route is None:
        return _response(runtime, {"error": "not_found"}, status=404)
    method = runtime.method()
    _, note_id, action = route
    allowed = (("GET", "POST") if not note_id else
               ("GET", "POST") if action == "versions" else
               ("GET", "POST", "DELETE") if action in ("shares", "links") else
               ("GET",) if action == "ws" else
               ("GET", "PATCH", "DELETE"))
    if method not in allowed:
        return _response(runtime, {"error": "method_not_allowed"}, status=405,
                         allow=", ".join(allowed))
    if method not in ("GET", "HEAD") and not runtime.same_origin():
        return _response(runtime, {"error": "origin_not_allowed"}, status=403)
    data = {}
    if method not in ("GET", "HEAD"):
        data, error = await runtime.json_body(MAX_BODY_BYTES)
        if error:
            return _response(runtime, {"error": error}, status=413 if error == "payload_too_large" else 400)
    await runtime.ensure_schema()
    account_bi, actor = await _actor(runtime, data)
    if not note_id:
        if not account_bi:
            return _response(runtime, {"error": "invalid_session"}, status=401)
        return await (_list(runtime, account_bi) if method == "GET" else
                      _create(runtime, account_bi, actor, data))
    row = await _row(runtime, note_id)
    role = await _role(runtime, row, account_bi)
    if not row or not role:
        return _response(runtime, {"error": "note_not_found"}, status=404)
    if action == "ws":
        if not account_bi:
            return _response(runtime, {"error": "invalid_session"}, status=401)
        return await runtime.realtime(note_id, account_bi, actor, role)
    if action == "versions":
        if not account_bi:
            return _response(runtime, {"error": "invalid_session"}, status=401)
        return await _versions(runtime, row, account_bi, actor, method, data)
    if action == "shares":
        if not account_bi:
            return _response(runtime, {"error": "invalid_session"}, status=401)
        return await _share(runtime, row, account_bi, method, data)
    if action == "links":
        if not account_bi:
            return _response(runtime, {"error": "invalid_session"}, status=401)
        return await _link(runtime, row, account_bi, actor, method, data)
    if method == "GET":
        return _response(runtime, {"ok": True, "note": await _project(runtime, row, role)})
    if not account_bi:
        return _response(runtime, {"error": "invalid_session"}, status=401)
    if method == "DELETE":
        if role != "owner":
            return _response(runtime, {"error": "owner_required"}, status=403)
        await runtime.d1_run("DELETE FROM notes WHERE note_id=?", note_id)
        await runtime.broadcast(note_id, {"type": "note-deleted"})
        await runtime.audit(actor, "note.delete", "note", note_id)
        return _response(runtime, {"ok": True})
    return await _update(runtime, row, account_bi, actor, data)
