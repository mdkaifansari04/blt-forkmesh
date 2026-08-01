"""Encrypted, repository/commit/run-scoped Code Workshop persistence.

The route adapter supplied by ``entry.py`` owns sessions, blind indexes, row
encryption, and D1 access. This module keeps all workshop inputs bounded and
requires an explicit participant row for every read or mutation. Repository
names, report paths, comments, and participant labels are encrypted at rest.
"""

import json
import re


PREFIX = "/api/world/workshops"
BODY_MAX_BYTES = 256 * 1024
MAX_SESSIONS = 50
MAX_RESULTS = 20
MAX_EVENTS = 100
MAX_PARTICIPANTS = 30
MAX_SESSIONS_PER_OWNER = 100
MAX_RESULTS_PER_SESSION = 50
MAX_EVENTS_PER_SESSION = 1000

WORKSHOP_TYPES = frozenset({
    "Architecture analysis",
    "Database-model analysis",
    "Dependency mapping",
    "Redundancy detection",
    "Dead-code detection",
    "Security analysis",
    "Test-coverage analysis",
    "Documentation analysis",
    "Performance analysis",
    "License compatibility analysis",
})
ROLES = frozenset({"owner", "editor", "viewer"})
WRITE_ROLES = frozenset({"owner", "editor"})
STATUSES = frozenset({"active", "completed", "archived"})
EVENT_KINDS = frozenset({"comment"})

_ID_RE = re.compile(r"^[a-f0-9]{32}$")
_COMMIT_RE = re.compile(r"^[0-9a-f]{40,64}$")
_RUN_RE = re.compile(r"^[A-Za-z0-9_-]{16,80}$")
_OWNER_RE = re.compile(r"^[a-z](?:[a-z0-9-]{0,61}[a-z0-9])?$")
_REPO_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")


def _response(runtime, data, status=200, allow=""):
    headers = {"x-content-type-options": "nosniff"}
    if allow:
        headers["allow"] = allow
    return runtime.response(
        data,
        status=status,
        cache_control="no-store, max-age=0, must-revalidate",
        extra_headers=headers,
    )


def _text(value, maximum, fallback=""):
    clean = "".join(
        character if character.isprintable() and character not in "<>" else " "
        for character in str(value or "")
    )
    clean = " ".join(clean.split()).strip()
    return (clean or fallback)[:maximum]


def _integer(value, maximum=100000):
    if isinstance(value, bool):
        return 0
    try:
        return max(0, min(maximum, int(value or 0)))
    except (TypeError, ValueError):
        return 0


def repository_parts(value):
    parts = str(value or "").strip().split("/")
    if (
        len(parts) != 2
        or not _OWNER_RE.fullmatch(parts[0].lower())
        or not _REPO_RE.fullmatch(parts[1])
    ):
        return None
    return parts[0].lower(), parts[1]


def valid_id(value):
    return bool(_ID_RE.fullmatch(str(value or "").strip().lower()))


def normalize_run(value):
    value = str(value or "").strip()
    return value if _RUN_RE.fullmatch(value) else ""


def normalize_commit(value):
    value = str(value or "").strip().lower()
    return value if _COMMIT_RE.fullmatch(value) else ""


def _string_list(value, maximum, item_maximum):
    if not isinstance(value, list):
        return []
    return [
        clean
        for clean in (_text(item, item_maximum) for item in value[:maximum])
        if clean
    ]


def normalize_report(value):
    """Return the only report shape persisted by the browser workshop API."""

    if not isinstance(value, dict):
        return None
    raw_nodes = value.get("nodes")
    if not isinstance(raw_nodes, list):
        raw_nodes = []
    nodes = []
    for node in raw_nodes[:80]:
        if not isinstance(node, dict):
            continue
        raw_use_sites = node.get("useSites")
        if not isinstance(raw_use_sites, list):
            raw_use_sites = []
        use_sites = []
        for use in raw_use_sites[:20]:
            if not isinstance(use, dict):
                continue
            path = _text(use.get("path"), 500)
            try:
                count = max(0, min(100000, int(use.get("count") or 0)))
            except (TypeError, ValueError):
                count = 0
            if path:
                use_sites.append({"path": path, "count": count})
        nodes.append({
            "label": _text(node.get("label"), 120, "Candidate"),
            "kind": _text(node.get("kind"), 80),
            "detail": _text(node.get("detail"), 300),
            "definitionPath": _text(node.get("definitionPath"), 500),
            "useSites": use_sites,
        })
    raw_edges = value.get("edges")
    if not isinstance(raw_edges, list):
        raw_edges = []
    edges = []
    for edge in raw_edges[:160]:
        if not isinstance(edge, dict):
            continue
        source = _text(edge.get("from"), 500)
        target = _text(edge.get("to"), 500)
        if source and target:
            edges.append({
                "from": source,
                "to": target,
                "path": _text(edge.get("path"), 500),
            })
    raw_models = value.get("modelUseSites")
    if not isinstance(raw_models, list):
        raw_models = []
    model_use_sites = []
    for model in raw_models[:80]:
        if not isinstance(model, dict):
            continue
        raw_uses = model.get("useSites")
        if not isinstance(raw_uses, list):
            raw_uses = []
        uses = []
        for use in raw_uses[:20]:
            if isinstance(use, dict) and _text(use.get("path"), 500):
                uses.append({
                    "path": _text(use.get("path"), 500),
                    "count": _integer(use.get("count")),
                })
        model_use_sites.append({
            "name": _text(model.get("name"), 120, "Model"),
            "definitionPath": _text(model.get("definitionPath"), 500),
            "useSites": uses,
        })
    return {
        "nodes": nodes,
        "edges": edges,
        "findings": _string_list(value.get("findings"), 80, 1000),
        "recommendations": _string_list(
            value.get("recommendations"), 20, 1000),
        "references": _string_list(value.get("references"), 80, 500),
        "modelUseSites": model_use_sites,
        "contributorCount": _integer(
            value.get("contributorCount"), 10000000),
    }


def _route(path):
    clean = str(path or "").rstrip("/")
    if clean == PREFIX:
        return ("collection", "", "")
    if not clean.startswith(PREFIX + "/"):
        return None
    parts = clean[len(PREFIX) + 1:].split("/")
    if not parts or not valid_id(parts[0]):
        return None
    session_id = parts[0].lower()
    if len(parts) == 1:
        return ("session", session_id, "")
    if len(parts) == 2 and parts[1] in (
            "results", "participants", "events"):
        return (parts[1], session_id, "")
    if (
        len(parts) == 3
        and parts[1] == "results"
        and valid_id(parts[2])
    ):
        return ("result", session_id, parts[2].lower())
    return None


async def _body(runtime):
    value, error = await runtime.json_body(BODY_MAX_BYTES)
    if error:
        status = 413 if error == "payload_too_large" else 400
        return None, _response(runtime, {"error": error}, status=status)
    return value, None


async def _actor(runtime, data=None):
    account_bi, account = await runtime.session(data or {})
    name = str((account or {}).get("name") or "").strip().lower()
    return str(account_bi or ""), name


async def _member_session(runtime, session_id, account_bi):
    return await runtime.d1_first(
        "SELECT s.*,p.role AS viewer_role,p.data AS participant_data "
        "FROM world_workshop_sessions s "
        "JOIN world_workshop_participants p ON p.session_id=s.session_id "
        "WHERE s.session_id=? AND p.account_bi=?",
        session_id,
        account_bi,
    )


async def _event(runtime, session_id, kind, actor_bi, data, now):
    event_id = runtime.new_id()
    await runtime.d1_run(
        "INSERT INTO world_workshop_events "
        "(event_id,session_id,kind,actor_bi,data,created_at) "
        "VALUES (?,?,?,?,?,?)",
        event_id,
        session_id,
        kind,
        actor_bi,
        await runtime.seal(data),
        now,
    )
    return event_id


async def _project_result(runtime, row):
    try:
        body = await runtime.open(row.get("data"))
    except Exception:
        return None
    if not isinstance(body, dict):
        return None
    return {
        "id": str(row.get("result_id") or ""),
        "runId": str(row.get("run_id") or ""),
        "commit": str(row.get("commit_hash") or ""),
        "createdAt": int(row.get("created_at") or 0),
        "createdBy": _text(body.get("createdBy"), 63),
        "report": body.get("report") if isinstance(body.get("report"), dict)
        else {},
    }


async def _project_event(runtime, row):
    try:
        body = await runtime.open(row.get("data"))
    except Exception:
        return None
    if not isinstance(body, dict):
        return None
    return {
        "id": str(row.get("event_id") or ""),
        "cursor": int(row.get("event_no") or 0),
        "kind": str(row.get("kind") or ""),
        "actor": _text(body.get("actor"), 63),
        "createdAt": int(row.get("created_at") or 0),
        "data": body.get("data") if isinstance(body.get("data"), dict) else {},
    }


async def _session_detail(runtime, row, include_events=True):
    session_id = str(row.get("session_id") or "")
    try:
        data = await runtime.open(row.get("data"))
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    result_rows = await runtime.d1_all(
        "SELECT result_id,run_id,commit_hash,data,created_at "
        "FROM world_workshop_results WHERE session_id=? "
        "ORDER BY created_at DESC,result_id DESC LIMIT ?",
        session_id,
        MAX_RESULTS,
    )
    participant_rows = await runtime.d1_all(
        "SELECT role,data,created_at FROM world_workshop_participants "
        "WHERE session_id=? ORDER BY created_at ASC LIMIT ?",
        session_id,
        MAX_PARTICIPANTS,
    )
    results = []
    for result_row in result_rows or []:
        projected = await _project_result(runtime, result_row)
        if projected:
            results.append(projected)
    participants = []
    for participant in participant_rows or []:
        try:
            participant_data = await runtime.open(participant.get("data"))
        except Exception:
            continue
        if isinstance(participant_data, dict):
            participants.append({
                "account": _text(participant_data.get("account"), 63),
                "role": str(participant.get("role") or ""),
                "joinedAt": int(participant.get("created_at") or 0),
            })
    events = []
    cursor = 0
    if include_events:
        event_rows = await runtime.d1_all(
            "SELECT event_no,event_id,kind,data,created_at "
            "FROM world_workshop_events WHERE session_id=? "
            "ORDER BY event_no DESC LIMIT ?",
            session_id,
            min(25, MAX_EVENTS),
        )
        for event_row in reversed(event_rows or []):
            projected = await _project_event(runtime, event_row)
            if projected:
                events.append(projected)
                cursor = max(cursor, projected["cursor"])
    return {
        "id": session_id,
        "repository": _text(data.get("repository"), 201),
        "commit": str(row.get("commit_hash") or ""),
        "runId": str(row.get("run_id") or ""),
        "workshopType": str(row.get("workshop_type") or ""),
        "title": _text(data.get("title"), 120),
        "status": str(row.get("status") or ""),
        "viewerRole": str(row.get("viewer_role") or ""),
        "createdAt": int(row.get("created_at") or 0),
        "updatedAt": int(row.get("updated_at") or 0),
        "results": results,
        "participants": participants,
        "events": events,
        "eventCursor": cursor,
    }


async def _list(runtime, account_bi, actor):
    query = runtime.query_params()
    repository = str(query.get("repository") or "")
    commit = normalize_commit(query.get("commit"))
    args = [account_bi]
    where = ["p.account_bi=?"]
    if repository:
        parts = repository_parts(repository)
        if not parts:
            return _response(
                runtime, {"error": "invalid_repository"}, status=400)
        where.append("s.repo_bi=?")
        args.append(await runtime.blind(parts[0] + "/" + parts[1]))
    if query.get("commit") and not commit:
        return _response(runtime, {"error": "invalid_commit"}, status=400)
    if commit:
        where.append("s.commit_hash=?")
        args.append(commit)
    args.append(MAX_SESSIONS)
    rows = await runtime.d1_all(
        "SELECT s.*,p.role AS viewer_role FROM world_workshop_sessions s "
        "JOIN world_workshop_participants p ON p.session_id=s.session_id "
        "WHERE " + " AND ".join(where)
        + " ORDER BY s.updated_at DESC LIMIT ?",
        *args,
    )
    sessions = []
    for row in rows or []:
        try:
            data = await runtime.open(row.get("data"))
        except Exception:
            continue
        if not isinstance(data, dict):
            continue
        sessions.append({
            "id": str(row.get("session_id") or ""),
            "repository": _text(data.get("repository"), 201),
            "commit": str(row.get("commit_hash") or ""),
            "runId": str(row.get("run_id") or ""),
            "workshopType": str(row.get("workshop_type") or ""),
            "title": _text(data.get("title"), 120),
            "status": str(row.get("status") or ""),
            "viewerRole": str(row.get("viewer_role") or ""),
            "createdAt": int(row.get("created_at") or 0),
            "updatedAt": int(row.get("updated_at") or 0),
        })
    return _response(runtime, {"ok": True, "sessions": sessions})


async def _create(runtime, data, account_bi, actor):
    parts = repository_parts(data.get("repository"))
    commit = normalize_commit(data.get("commit"))
    run_id = normalize_run(data.get("runId"))
    workshop_type = str(data.get("workshopType") or "")
    report = normalize_report(data.get("report"))
    if not parts:
        return _response(runtime, {"error": "invalid_repository"}, status=400)
    if not commit:
        return _response(runtime, {"error": "invalid_commit"}, status=400)
    if not run_id:
        return _response(runtime, {"error": "invalid_run_id"}, status=400)
    if workshop_type not in WORKSHOP_TYPES:
        return _response(runtime, {"error": "invalid_workshop_type"}, status=400)
    if report is None:
        return _response(runtime, {"error": "invalid_report"}, status=400)
    if not await runtime.repository_access(parts[0], parts[1], actor):
        return _response(runtime, {"error": "not_found"}, status=404)
    repository = parts[0] + "/" + parts[1]
    repo_bi = await runtime.blind(repository)
    owned = await runtime.d1_first(
        "SELECT COUNT(*) AS n FROM world_workshop_sessions WHERE owner_bi=?",
        account_bi)
    if int((owned or {}).get("n") or 0) >= MAX_SESSIONS_PER_OWNER:
        return _response(runtime, {"error": "session_limit"}, status=409)
    existing = await runtime.d1_first(
        "SELECT session_id FROM world_workshop_sessions "
        "WHERE repo_bi=? AND commit_hash=? AND run_id=?",
        repo_bi, commit, run_id,
    )
    if existing:
        existing_session_id = str(existing.get("session_id") or "")
        member = await _member_session(
            runtime, existing_session_id, account_bi)
        response = {"error": "run_exists"}
        if member:
            response["sessionId"] = existing_session_id
        return _response(runtime, response, status=409)
    now = runtime.now()
    session_id = runtime.new_id()
    result_id = runtime.new_id()
    session_data = await runtime.seal({
        "repository": repository,
        "title": _text(
            data.get("title"), 120,
            workshop_type + " · " + commit[:12]),
    })
    participant_data = await runtime.seal({"account": actor})
    result_data = await runtime.seal({
        "createdBy": actor,
        "report": report,
    })
    try:
        await runtime.d1_run(
            "INSERT INTO world_workshop_sessions "
            "(session_id,repo_bi,commit_hash,run_id,workshop_type,owner_bi,"
            "status,data,created_at,updated_at) "
            "VALUES (?,?,?,?,?,?,'active',?,?,?)",
            session_id, repo_bi, commit, run_id, workshop_type, account_bi,
            session_data, now, now,
        )
        await runtime.d1_run(
            "INSERT INTO world_workshop_participants "
            "(session_id,account_bi,role,data,added_by_bi,created_at,updated_at) "
            "VALUES (?,?,'owner',?,?,?,?)",
            session_id, account_bi, participant_data, account_bi, now, now,
        )
        await runtime.d1_run(
            "INSERT INTO world_workshop_results "
            "(result_id,session_id,run_id,commit_hash,created_by_bi,data,"
            "created_at) VALUES (?,?,?,?,?,?,?)",
            result_id, session_id, run_id, commit, account_bi, result_data, now,
        )
        await _event(
            runtime, session_id, "session-created", account_bi,
            {"actor": actor, "data": {
                "resultId": result_id, "workshopType": workshop_type}},
            now,
        )
    except Exception:


        try:
            await runtime.d1_run(
                "DELETE FROM world_workshop_sessions WHERE session_id=?",
                session_id)
            await runtime.d1_run(
                "DELETE FROM world_workshop_participants WHERE session_id=?",
                session_id)
            await runtime.d1_run(
                "DELETE FROM world_workshop_results WHERE session_id=?",
                session_id)
        except Exception:
            pass
        return _response(
            runtime, {"error": "temporarily_unavailable"}, status=503)
    await runtime.audit(
        actor,
        "world.workshop.create",
        "workshop_session",
        session_id,
        details={"workshopType": workshop_type},
    )
    row = await _member_session(runtime, session_id, account_bi)
    return _response(runtime, {
        "ok": True,
        "session": await _session_detail(runtime, row),
        "resultId": result_id,
    }, status=201)


async def handle(runtime, path):
    await runtime.ensure_schema()
    route = _route(path)
    if route is None:
        return _response(runtime, {"error": "not_found"}, status=404)
    resource, session_id, resource_id = route
    method = runtime.method()
    data = {}
    if method in ("POST", "PATCH", "DELETE"):
        data, error_response = await _body(runtime)
        if error_response is not None:
            return error_response
    account_bi, actor = await _actor(runtime, data)
    if not actor:
        return _response(runtime, {"error": "invalid_session"}, status=401)

    if resource == "collection":
        if method == "GET":
            return await _list(runtime, account_bi, actor)
        if method == "POST":
            return await _create(runtime, data, account_bi, actor)
        return _response(
            runtime, {"error": "method_not_allowed"}, status=405,
            allow="GET, POST")

    row = await _member_session(runtime, session_id, account_bi)
    if not row:
        return _response(runtime, {"error": "not_found"}, status=404)
    role = str(row.get("viewer_role") or "")
    now = runtime.now()

    if resource == "session":
        if method == "GET":
            detail = await _session_detail(runtime, row)
            if detail is None:
                return _response(runtime, {"error": "not_found"}, status=404)
            return _response(runtime, {"ok": True, "session": detail})
        if method != "PATCH":
            return _response(
                runtime, {"error": "method_not_allowed"}, status=405,
                allow="GET, PATCH")
        if role not in WRITE_ROLES:
            return _response(runtime, {"error": "forbidden"}, status=403)
        status = str(data.get("status") or row.get("status") or "")
        if status not in STATUSES:
            return _response(runtime, {"error": "invalid_status"}, status=400)
        try:
            current = await runtime.open(row.get("data"))
        except Exception:
            return _response(runtime, {"error": "not_found"}, status=404)
        if not isinstance(current, dict):
            return _response(runtime, {"error": "not_found"}, status=404)
        title = _text(data.get("title"), 120, current.get("title"))
        current["title"] = title
        await runtime.d1_run(
            "UPDATE world_workshop_sessions SET status=?,data=?,updated_at=? "
            "WHERE session_id=?",
            status, await runtime.seal(current), now, session_id,
        )
        await _event(
            runtime, session_id, "status-updated", account_bi,
            {"actor": actor, "data": {"status": status}}, now)
        await runtime.audit(
            actor, "world.workshop.update", "workshop_session", session_id,
            details={"status": status})
        updated = await _member_session(runtime, session_id, account_bi)
        return _response(runtime, {
            "ok": True, "session": await _session_detail(runtime, updated)})

    if resource == "results":
        if method != "POST":
            return _response(
                runtime, {"error": "method_not_allowed"}, status=405,
                allow="POST")
        if role not in WRITE_ROLES:
            return _response(runtime, {"error": "forbidden"}, status=403)
        report = normalize_report(data.get("report"))
        if report is None:
            return _response(runtime, {"error": "invalid_report"}, status=400)
        count = await runtime.d1_first(
            "SELECT COUNT(*) AS n FROM world_workshop_results "
            "WHERE session_id=?", session_id)
        if int((count or {}).get("n") or 0) >= MAX_RESULTS_PER_SESSION:
            return _response(runtime, {"error": "result_limit"}, status=409)
        result_id = runtime.new_id()
        await runtime.d1_run(
            "INSERT INTO world_workshop_results "
            "(result_id,session_id,run_id,commit_hash,created_by_bi,data,"
            "created_at) VALUES (?,?,?,?,?,?,?)",
            result_id, session_id, row.get("run_id"), row.get("commit_hash"),
            account_bi,
            await runtime.seal({"createdBy": actor, "report": report}),
            now,
        )
        await runtime.d1_run(
            "UPDATE world_workshop_sessions SET updated_at=? WHERE session_id=?",
            now, session_id)
        await _event(
            runtime, session_id, "result-saved", account_bi,
            {"actor": actor, "data": {"resultId": result_id}}, now)
        await runtime.audit(
            actor, "world.workshop.result.save", "workshop_result", result_id)
        return _response(
            runtime, {"ok": True, "resultId": result_id}, status=201)

    if resource == "result":
        if method != "GET":
            return _response(
                runtime, {"error": "method_not_allowed"}, status=405,
                allow="GET")
        result = await runtime.d1_first(
            "SELECT result_id,run_id,commit_hash,data,created_at "
            "FROM world_workshop_results "
            "WHERE session_id=? AND result_id=?",
            session_id, resource_id,
        )
        projected = await _project_result(runtime, result or {})
        return _response(
            runtime,
            {"ok": True, "result": projected}
            if projected else {"error": "not_found"},
            status=200 if projected else 404,
        )

    if resource == "participants":
        if role != "owner":
            return _response(runtime, {"error": "forbidden"}, status=403)
        if method == "POST":
            account = _text(data.get("account"), 63).lower()
            participant_role = str(data.get("role") or "viewer").lower()
            if participant_role not in {"editor", "viewer"}:
                return _response(
                    runtime, {"error": "invalid_role"}, status=400)
            participant_bi, participant_name = await runtime.account(account)
            if not participant_name:
                return _response(
                    runtime, {"error": "account_not_found"}, status=404)
            count = await runtime.d1_first(
                "SELECT COUNT(*) AS n FROM world_workshop_participants "
                "WHERE session_id=?", session_id)
            if int((count or {}).get("n") or 0) >= MAX_PARTICIPANTS:
                return _response(
                    runtime, {"error": "participant_limit"}, status=409)
            exists = await runtime.d1_first(
                "SELECT role FROM world_workshop_participants "
                "WHERE session_id=? AND account_bi=?",
                session_id, participant_bi)
            if exists:
                return _response(
                    runtime, {"error": "participant_exists"}, status=409)
            await runtime.d1_run(
                "INSERT INTO world_workshop_participants "
                "(session_id,account_bi,role,data,added_by_bi,created_at,"
                "updated_at) VALUES (?,?,?,?,?,?,?)",
                session_id, participant_bi, participant_role,
                await runtime.seal({"account": participant_name}),
                account_bi, now, now,
            )
            await _event(
                runtime, session_id, "participant-added", account_bi,
                {"actor": actor, "data": {
                    "account": participant_name, "role": participant_role}},
                now)
            await runtime.audit(
                actor, "world.workshop.participant.add",
                "workshop_session", session_id,
                details={"role": participant_role})
            return _response(runtime, {"ok": True}, status=201)
        if method == "DELETE":
            account = _text(data.get("account"), 63).lower()
            participant_bi, participant_name = await runtime.account(account)
            participant = await runtime.d1_first(
                "SELECT role FROM world_workshop_participants "
                "WHERE session_id=? AND account_bi=?",
                session_id, participant_bi)
            if not participant or participant.get("role") == "owner":
                return _response(runtime, {"error": "not_found"}, status=404)
            await runtime.d1_run(
                "DELETE FROM world_workshop_participants "
                "WHERE session_id=? AND account_bi=?",
                session_id, participant_bi)
            await _event(
                runtime, session_id, "participant-removed", account_bi,
                {"actor": actor, "data": {"account": participant_name}}, now)
            await runtime.audit(
                actor, "world.workshop.participant.remove",
                "workshop_session", session_id)
            return _response(runtime, {"ok": True})
        return _response(
            runtime, {"error": "method_not_allowed"}, status=405,
            allow="POST, DELETE")

    if resource == "events":
        if method == "GET":
            query = runtime.query_params()
            try:
                after = max(0, int(query.get("after") or 0))
            except (TypeError, ValueError):
                return _response(
                    runtime, {"error": "invalid_cursor"}, status=400)
            rows = await runtime.d1_all(
                "SELECT event_no,event_id,kind,data,created_at "
                "FROM world_workshop_events "
                "WHERE session_id=? AND event_no>? "
                "ORDER BY event_no ASC LIMIT ?",
                session_id, after, MAX_EVENTS)
            events = []
            cursor = after
            for event_row in rows or []:
                projected = await _project_event(runtime, event_row)
                if projected:
                    events.append(projected)
                    cursor = max(cursor, projected["cursor"])
            return _response(runtime, {
                "ok": True, "events": events, "cursor": cursor})
        if method == "POST":
            kind = str(data.get("kind") or "comment")
            message = _text(data.get("message"), 2000)
            if kind not in EVENT_KINDS or not message:
                return _response(
                    runtime, {"error": "invalid_event"}, status=400)
            count = await runtime.d1_first(
                "SELECT COUNT(*) AS n FROM world_workshop_events "
                "WHERE session_id=?", session_id)
            if int((count or {}).get("n") or 0) >= MAX_EVENTS_PER_SESSION:
                return _response(
                    runtime, {"error": "event_limit"}, status=409)
            event_id = await _event(
                runtime, session_id, kind, account_bi,
                {"actor": actor, "data": {"message": message}}, now)
            await runtime.d1_run(
                "UPDATE world_workshop_sessions SET updated_at=? "
                "WHERE session_id=?", now, session_id)
            return _response(
                runtime, {"ok": True, "eventId": event_id}, status=201)
        return _response(
            runtime, {"error": "method_not_allowed"}, status=405,
            allow="GET, POST")

    return _response(runtime, {"error": "not_found"}, status=404)
