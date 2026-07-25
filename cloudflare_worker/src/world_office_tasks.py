"""Private organization marketing tasks for the in-world Office.

The Worker adapter owns account sessions, organization membership, row
encryption, and D1 access.  This module deliberately receives no platform
administrator primitive: only active members of the configured organization
can read the board, and only organization owner/admin or maintain+ members can
create or reassign work.

Task copy, assignee labels, and check-in notes are encrypted at rest.  The
plaintext columns contain only opaque blind indexes, bounded state, and
server-authoritative timer metadata.  Nothing in this module is stored in or
published through the Office Durable Object.
"""

import re


PREFIX = "/api/world/office/marketing-tasks"
BODY_MAX_BYTES = 12 * 1024
MAX_TASKS = 250
MAX_CHECKINS_PER_TASK = 50
MAX_TITLE = 160
MAX_DETAILS = 4000
MAX_CHECKIN_NOTE = 500
MAX_ELAPSED_MS = 10 * 365 * 24 * 60 * 60 * 1000
CHECKIN_MIN_MS = 4 * 60 * 1000
CHECKIN_MAX_MS = 9 * 60 * 1000

TASK_STATES = frozenset({"idle", "active"})
CHECKIN_STATES = frozenset({"going_well", "blocked", "needs_help"})
PERMISSION_RANK = {
    "read": 0,
    "write": 1,
    "maintain": 2,
    "admin": 3,
}

_ID_RE = re.compile(r"^[a-f0-9]{32}$")


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
    """Bound user-facing copy without retaining controls or markup."""

    clean = "".join(
        character
        if character.isprintable() and character not in "<>"
        else " "
        for character in str(value or "")
    )
    clean = " ".join(clean.split()).strip()
    return (clean or fallback)[:maximum]


def valid_id(value):
    return bool(_ID_RE.fullmatch(str(value or "").strip().lower()))


def _route(path):
    clean = str(path or "").rstrip("/")
    if clean == PREFIX:
        return ("collection", "", "")
    # Collection actions must be resolved before the opaque task-id parser.
    if clean == PREFIX + "/stop-active":
        return ("stop-active", "", "stop-active")
    if not clean.startswith(PREFIX + "/"):
        return None
    parts = clean[len(PREFIX) + 1:].split("/")
    if not parts or not valid_id(parts[0]):
        return None
    task_id = parts[0].lower()
    if len(parts) == 1:
        return ("task", task_id, "")
    if len(parts) == 2 and parts[1] in ("start", "stop", "checkin"):
        return ("action", task_id, parts[1])
    return None


async def _body(runtime):
    value, error = await runtime.json_body(BODY_MAX_BYTES)
    if error:
        status = 413 if error == "payload_too_large" else 400
        return None, _response(runtime, {"error": error}, status=status)
    return value, None


async def _actor(runtime, data=None):
    account_bi, record = await runtime.session(data or {})
    actor = _text((record or {}).get("name"), 64).lower()
    return str(account_bi or ""), actor


def _can_manage(role, permission):
    return (
        str(role or "") in ("owner", "admin")
        or PERMISSION_RANK.get(str(permission or ""), -1)
        >= PERMISSION_RANK["maintain"]
    )


def _checkin_deadline(runtime, now):
    """Return a cryptographically seeded deadline between 4 and 9 minutes."""

    token = str(runtime.new_id() or "")
    try:
        seed = int(token[:16], 16)
    except (TypeError, ValueError):
        seed = sum(ord(character) for character in token)
    span = CHECKIN_MAX_MS - CHECKIN_MIN_MS
    return int(now) + CHECKIN_MIN_MS + (seed % (span + 1))


async def _latest_checkins(runtime, task_ids):
    if not task_ids:
        return {}
    # task_ids originate in the D1 result and are validated opaque hex ids.
    task_ids = [
        str(value).lower() for value in task_ids if valid_id(value)
    ][:MAX_TASKS]
    if not task_ids:
        return {}
    rows = []
    # Keep each D1 statement well below its bound-parameter ceiling.
    for offset in range(0, len(task_ids), 50):
        chunk = task_ids[offset:offset + 50]
        placeholders = ",".join("?" for _value in chunk)
        rows.extend(await runtime.d1_all(
            "SELECT c.* FROM world_office_marketing_checkins c "
            "WHERE c.task_id IN (" + placeholders + ") "
            "AND c.checkin_id=("
            "SELECT newest.checkin_id "
            "FROM world_office_marketing_checkins newest "
            "WHERE newest.task_id=c.task_id "
            "ORDER BY newest.created_at DESC,newest.checkin_id DESC LIMIT 1)",
            *chunk,
        ) or [])
    return {str(row.get("task_id") or ""): row for row in rows or []}


async def _project_checkin(runtime, row):
    if not row:
        return None
    try:
        data = await runtime.open(row.get("data"))
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    state = str(row.get("state") or "")
    if state not in CHECKIN_STATES:
        return None
    return {
        "state": state,
        "note": _text(data.get("note"), MAX_CHECKIN_NOTE),
        "at": int(row.get("created_at") or 0),
    }


async def _project_task(runtime, row, now, checkin=None):
    try:
        data = await runtime.open(row.get("data"))
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    state = str(row.get("status") or "")
    if state not in TASK_STATES:
        return None
    started_at = int(row.get("started_at") or 0) if state == "active" else 0
    elapsed_ms = max(0, int(row.get("elapsed_ms") or 0))
    if started_at:
        elapsed_ms += max(0, int(now) - started_at)
    return {
        "id": str(row.get("task_id") or ""),
        "title": _text(data.get("title"), MAX_TITLE, "Marketing task"),
        "details": _text(data.get("details"), MAX_DETAILS),
        "assignee": _text(data.get("assignee"), 64).lower(),
        "status": state,
        "elapsedMs": min(MAX_ELAPSED_MS, elapsed_ms),
        "startedAt": started_at,
        "nextCheckinAt": (
            int(row.get("next_checkin_at") or 0)
            if state == "active" else 0
        ),
        "createdAt": int(row.get("created_at") or 0),
        "updatedAt": int(row.get("updated_at") or 0),
        "lastCheckin": await _project_checkin(runtime, checkin),
    }


async def _task(runtime, org_bi, task_id):
    return await runtime.d1_first(
        "SELECT * FROM world_office_marketing_tasks "
        "WHERE org_bi=? AND task_id=?",
        org_bi,
        task_id,
    )


async def _task_response(runtime, row, now, status=200):
    if not row:
        return _response(runtime, {"error": "task_not_found"}, status=404)
    checkins = await _latest_checkins(
        runtime, [str(row.get("task_id") or "")])
    task = await _project_task(
        runtime,
        row,
        now,
        checkins.get(str(row.get("task_id") or "")),
    )
    if task is None:
        return _response(runtime, {"error": "task_unavailable"}, status=500)
    return _response(runtime, {"ok": True, "task": task}, status=status)


async def _list(runtime, org_bi, account_bi, actor, can_manage, now):
    if can_manage:
        rows = await runtime.d1_all(
            "SELECT * FROM world_office_marketing_tasks "
            "WHERE org_bi=? ORDER BY updated_at DESC,task_id DESC LIMIT ?",
            org_bi,
            MAX_TASKS,
        )
    else:
        rows = await runtime.d1_all(
            "SELECT * FROM world_office_marketing_tasks "
            "WHERE org_bi=? AND assignee_bi=? "
            "ORDER BY updated_at DESC,task_id DESC LIMIT ?",
            org_bi,
            account_bi,
            MAX_TASKS,
        )
    latest = await _latest_checkins(
        runtime, [str(row.get("task_id") or "") for row in rows or []])
    tasks = []
    for row in rows or []:
        task = await _project_task(
            runtime,
            row,
            now,
            latest.get(str(row.get("task_id") or "")),
        )
        if task is not None:
            tasks.append(task)
    result = {
        "ok": True,
        "actor": actor,
        "canManage": bool(can_manage),
        "serverNow": int(now),
        "tasks": tasks,
    }
    if can_manage:
        # The active-only roster is intentionally absent for plain members.
        result["members"] = await runtime.eligible_users()
    return _response(runtime, result)


async def _create(
        runtime, org_bi, account_bi, actor, data, can_manage, now):
    if not can_manage:
        return _response(runtime, {"error": "forbidden"}, status=403)
    title = _text(data.get("title"), MAX_TITLE)
    details = _text(data.get("details"), MAX_DETAILS)
    assignee = _text(data.get("assignee"), 64).lower()
    if not title or not assignee:
        return _response(
            runtime, {"error": "title_and_assignee_required"}, status=400)
    member = await runtime.active_user(assignee)
    if not member:
        return _response(
            runtime, {"error": "assignee_not_active_member"}, status=400)
    count = await runtime.d1_first(
        "SELECT COUNT(*) AS count FROM world_office_marketing_tasks "
        "WHERE org_bi=?",
        org_bi,
    )
    if int((count or {}).get("count") or 0) >= MAX_TASKS:
        return _response(
            runtime, {"error": "task_capacity_reached"}, status=409)
    task_id = runtime.new_id()
    if not valid_id(task_id):
        return _response(runtime, {"error": "id_generation_failed"}, status=500)
    sealed = await runtime.seal({
        "title": title,
        "details": details,
        "assignee": member["name"],
        "createdBy": actor,
    })
    try:
        await runtime.d1_run(
            "INSERT INTO world_office_marketing_tasks "
            "(task_id,org_bi,status,assignee_bi,data,created_by_bi,"
            "created_at,updated_at,elapsed_ms,started_at,next_checkin_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?)",
            task_id,
            org_bi,
            "idle",
            member["bi"],
            sealed,
            account_bi,
            now,
            now,
            0,
            0,
            0,
        )
    except Exception as error:
        if "catalog_full" in str(error):
            return _response(
                runtime, {"error": "task_capacity_reached"}, status=409)
        raise
    await runtime.audit(
        actor,
        "office.marketing_task_created",
        "office_task",
        task_id,
        details={"assigned": True},
    )
    row = await _task(runtime, org_bi, task_id)
    return await _task_response(runtime, row, now, status=201)


async def _update(
        runtime, org_bi, actor, task_id, data, can_manage, now):
    if not can_manage:
        return _response(runtime, {"error": "forbidden"}, status=403)
    row = await _task(runtime, org_bi, task_id)
    if not row:
        return _response(runtime, {"error": "task_not_found"}, status=404)
    try:
        current = await runtime.open(row.get("data"))
    except Exception:
        return _response(runtime, {"error": "task_unavailable"}, status=500)
    if not isinstance(current, dict):
        return _response(runtime, {"error": "task_unavailable"}, status=500)
    allowed = {"title", "details", "assignee", "sessionToken"}
    changed = [
        field for field in ("title", "details", "assignee") if field in data
    ]
    if not changed or any(field not in allowed for field in data):
        return _response(runtime, {"error": "invalid_update"}, status=400)
    title = (
        _text(data.get("title"), MAX_TITLE)
        if "title" in data
        else _text(current.get("title"), MAX_TITLE)
    )
    details = (
        _text(data.get("details"), MAX_DETAILS)
        if "details" in data
        else _text(current.get("details"), MAX_DETAILS)
    )
    assignee = (
        _text(data.get("assignee"), 64).lower()
        if "assignee" in data
        else _text(current.get("assignee"), 64).lower()
    )
    if not title or not assignee:
        return _response(
            runtime, {"error": "title_and_assignee_required"}, status=400)
    member = await runtime.active_user(assignee)
    if not member:
        return _response(
            runtime, {"error": "assignee_not_active_member"}, status=400)
    if str(row.get("status") or "") == "active":
        return _response(
            runtime, {"error": "active_task_cannot_be_updated"}, status=409)
    sealed = await runtime.seal({
        "title": title,
        "details": details,
        "assignee": member["name"],
        "createdBy": _text(current.get("createdBy"), 64).lower(),
    })
    await runtime.d1_run(
        "UPDATE world_office_marketing_tasks "
        "SET assignee_bi=?,data=?,updated_at=? "
        "WHERE org_bi=? AND task_id=? AND status='idle'",
        member["bi"],
        sealed,
        now,
        org_bi,
        task_id,
    )
    changed_row = await _task(runtime, org_bi, task_id)
    if (
        not changed_row
        or str(changed_row.get("status") or "") != "idle"
        or str(changed_row.get("assignee_bi") or "") != str(member["bi"])
        or str(changed_row.get("data") or "") != str(sealed)
    ):
        return _response(runtime, {"error": "task_state_conflict"}, status=409)
    await runtime.audit(
        actor,
        "office.marketing_task_updated",
        "office_task",
        task_id,
        details={"fields": ",".join(changed)},
    )
    return await _task_response(runtime, changed_row, now)


async def _start(
        runtime, org_bi, account_bi, actor, task_id, row, now):
    if str(row.get("assignee_bi") or "") != account_bi:
        return _response(runtime, {"error": "assignee_only"}, status=403)
    if str(row.get("status") or "") == "active":
        return await _task_response(runtime, row, now)
    existing = await runtime.d1_first(
        "SELECT task_id FROM world_office_marketing_tasks "
        "WHERE org_bi=? AND active_assignee_bi=? AND status='active' "
        "LIMIT 1",
        org_bi,
        account_bi,
    )
    if existing:
        return _response(
            runtime,
            {
                "error": "active_task_exists",
                "activeTaskId": str(existing.get("task_id") or ""),
            },
            status=409,
        )
    deadline = _checkin_deadline(runtime, now)
    try:
        await runtime.d1_run(
            "UPDATE world_office_marketing_tasks SET "
            "status='active',active_assignee_bi=?,started_at=?,"
            "next_checkin_at=?,updated_at=? "
            "WHERE org_bi=? AND task_id=? AND assignee_bi=? "
            "AND status='idle' AND active_assignee_bi=''",
            account_bi,
            now,
            deadline,
            now,
            org_bi,
            task_id,
            account_bi,
        )
    except Exception as error:
        if "UNIQUE" in str(error).upper():
            return _response(
                runtime, {"error": "active_task_exists"}, status=409)
        raise
    changed = await _task(runtime, org_bi, task_id)
    if not changed or str(changed.get("status") or "") != "active":
        return _response(runtime, {"error": "task_state_conflict"}, status=409)
    await runtime.audit(
        actor,
        "office.marketing_task_started",
        "office_task",
        task_id,
        details={"state": "active"},
    )
    return await _task_response(runtime, changed, now)


async def _stop(
        runtime, org_bi, account_bi, actor, task_id, row, now):
    if str(row.get("assignee_bi") or "") != account_bi:
        return _response(runtime, {"error": "assignee_only"}, status=403)
    if (
        str(row.get("status") or "") != "active"
        or str(row.get("active_assignee_bi") or "") != account_bi
    ):
        return _response(runtime, {"error": "task_not_active"}, status=409)
    started_at = int(row.get("started_at") or 0)
    addition = max(0, now - started_at) if started_at else 0
    elapsed = min(
        MAX_ELAPSED_MS,
        max(0, int(row.get("elapsed_ms") or 0)) + addition,
    )
    await runtime.d1_run(
        "UPDATE world_office_marketing_tasks SET "
        "status='idle',active_assignee_bi='',elapsed_ms=?,started_at=0,"
        "next_checkin_at=0,updated_at=? "
        "WHERE org_bi=? AND task_id=? AND assignee_bi=? "
        "AND status='active' AND active_assignee_bi=?",
        elapsed,
        now,
        org_bi,
        task_id,
        account_bi,
        account_bi,
    )
    await runtime.audit(
        actor,
        "office.marketing_task_stopped",
        "office_task",
        task_id,
        details={"state": "idle"},
    )
    return await _task_response(
        runtime, await _task(runtime, org_bi, task_id), now)


async def _stop_active(runtime, org_bi, account_bi, actor, now):
    """Idempotently stop this account's active timer using server time."""
    row = await runtime.d1_first(
        "SELECT task_id,elapsed_ms,started_at "
        "FROM world_office_marketing_tasks "
        "WHERE org_bi=? AND active_assignee_bi=? AND status='active' "
        "LIMIT 1",
        org_bi,
        account_bi,
    )
    if not row:
        return _response(runtime, {
            "ok": True,
            "stopped": False,
            "serverNow": int(now),
        })
    started_at = int(row.get("started_at") or 0)
    addition = max(0, int(now) - started_at) if started_at else 0
    elapsed = min(
        MAX_ELAPSED_MS,
        max(0, int(row.get("elapsed_ms") or 0)) + addition,
    )
    task_id = str(row.get("task_id") or "")
    await runtime.d1_run(
        "UPDATE world_office_marketing_tasks SET "
        "status='idle',active_assignee_bi='',elapsed_ms=?,started_at=0,"
        "next_checkin_at=0,updated_at=? "
        "WHERE org_bi=? AND task_id=? AND active_assignee_bi=? "
        "AND status='active'",
        elapsed,
        now,
        org_bi,
        task_id,
        account_bi,
    )
    changed = await runtime.d1_first(
        "SELECT status FROM world_office_marketing_tasks "
        "WHERE org_bi=? AND task_id=?",
        org_bi,
        task_id,
    )
    stopped = bool(changed and changed.get("status") == "idle")
    if stopped:
        await runtime.audit(
            actor,
            "office.marketing_task_stopped",
            "office_task",
            task_id,
            details={"state": "idle", "source": "world-exit"},
        )
    return _response(runtime, {
        "ok": True,
        "stopped": stopped,
        "serverNow": int(now),
    })


async def _checkin(
        runtime, org_bi, account_bi, actor, task_id, row, data, now):
    if str(row.get("assignee_bi") or "") != account_bi:
        return _response(runtime, {"error": "assignee_only"}, status=403)
    if (
        str(row.get("status") or "") != "active"
        or str(row.get("active_assignee_bi") or "") != account_bi
    ):
        return _response(runtime, {"error": "task_not_active"}, status=409)
    state = str(data.get("state") or "").strip().lower()
    if state not in CHECKIN_STATES:
        return _response(runtime, {"error": "invalid_checkin_state"}, status=400)
    note = _text(data.get("note"), MAX_CHECKIN_NOTE)
    checkin_id = runtime.new_id()
    if not valid_id(checkin_id):
        return _response(runtime, {"error": "id_generation_failed"}, status=500)
    # Retain a small per-task history; the migration's global trigger is the
    # final race-safe guard against an unbounded table.
    await runtime.d1_run(
        "DELETE FROM world_office_marketing_checkins WHERE checkin_id IN ("
        "SELECT checkin_id FROM world_office_marketing_checkins "
        "WHERE task_id=? ORDER BY created_at DESC,checkin_id DESC "
        "LIMIT -1 OFFSET ?)",
        task_id,
        MAX_CHECKINS_PER_TASK - 1,
    )
    try:
        await runtime.d1_run(
            "INSERT INTO world_office_marketing_checkins "
            "(checkin_id,task_id,org_bi,account_bi,state,data,created_at) "
            "SELECT ?,?,?,?,?,?,? "
            "FROM world_office_marketing_tasks "
            "WHERE org_bi=? AND task_id=? AND status='active' "
            "AND assignee_bi=? AND active_assignee_bi=?",
            checkin_id,
            task_id,
            org_bi,
            account_bi,
            state,
            await runtime.seal({"note": note}),
            now,
            org_bi,
            task_id,
            account_bi,
            account_bi,
        )
    except Exception as error:
        if "catalog_full" in str(error):
            return _response(
                runtime, {"error": "checkin_capacity_reached"}, status=409)
        raise
    inserted = await runtime.d1_first(
        "SELECT checkin_id FROM world_office_marketing_checkins "
        "WHERE checkin_id=?",
        checkin_id,
    )
    if not inserted:
        return _response(runtime, {"error": "task_state_conflict"}, status=409)
    deadline = _checkin_deadline(runtime, now)
    await runtime.d1_run(
        "UPDATE world_office_marketing_tasks "
        "SET next_checkin_at=?,updated_at=? "
        "WHERE org_bi=? AND task_id=? AND status='active' "
        "AND active_assignee_bi=?",
        deadline,
        now,
        org_bi,
        task_id,
        account_bi,
    )
    await runtime.audit(
        actor,
        "office.marketing_task_checkin",
        "office_task",
        task_id,
        details={"state": state},
    )
    return await _task_response(
        runtime, await _task(runtime, org_bi, task_id), now)


async def handle(runtime, path):
    route = _route(path)
    if route is None:
        return _response(runtime, {"error": "not_found"}, status=404)
    method = runtime.method()
    allowed = (
        ("GET", "POST")
        if route[0] == "collection"
        else ("POST",)
        if route[0] == "stop-active"
        else ("GET", "PATCH")
        if route[0] == "task"
        else ("POST",)
    )
    if method not in allowed:
        return _response(
            runtime,
            {"error": "method_not_allowed"},
            status=405,
            allow=", ".join(allowed),
        )
    if method != "GET" and not runtime.same_origin():
        return _response(runtime, {"error": "origin_not_allowed"}, status=403)

    data = {}
    if method != "GET":
        data, error_response = await _body(runtime)
        if error_response is not None:
            return error_response

    await runtime.ensure_schema()
    account_bi, actor = await _actor(runtime, data)
    if not account_bi or not actor:
        return _response(runtime, {"error": "invalid_session"}, status=401)
    org_bi, organization = await runtime.organization()
    if not org_bi or not organization:
        return _response(
            runtime, {"error": "office_organization_unavailable"}, status=503)
    role, permission = await runtime.membership(org_bi, actor)
    can_manage = _can_manage(role, permission)
    now = int(runtime.now())

    kind, task_id, action = route
    if kind == "stop-active":
        return await _stop_active(
            runtime, org_bi, account_bi, actor, now)
    if kind == "collection":
        if method == "GET":
            return await _list(
                runtime, org_bi, account_bi, actor, can_manage, now)
        return await _create(
            runtime, org_bi, account_bi, actor, data, can_manage, now)

    row = await _task(runtime, org_bi, task_id)
    if not row:
        return _response(runtime, {"error": "task_not_found"}, status=404)
    if kind == "task":
        if method == "GET":
            if (
                not can_manage
                and str(row.get("assignee_bi") or "") != account_bi
            ):
                return _response(
                    runtime, {"error": "task_not_found"}, status=404)
            return await _task_response(runtime, row, now)
        return await _update(
            runtime, org_bi, actor, task_id, data, can_manage, now)
    if action == "start":
        return await _start(
            runtime, org_bi, account_bi, actor, task_id, row, now)
    if action == "stop":
        return await _stop(
            runtime, org_bi, account_bi, actor, task_id, row, now)
    return await _checkin(
        runtime, org_bi, account_bi, actor, task_id, row, data, now)
