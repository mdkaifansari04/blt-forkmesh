"""Organization-private universal tasks for the World and dashboard HUD.

The legacy Marketing wall is a filtered view of the same task catalog. The
Worker adapter owns account sessions, organization membership, row encryption,
and D1 access; no task record is returned outside the configured organization.

Task copy, assignee labels, and check-in notes are encrypted at rest.  The
plaintext columns contain only opaque blind indexes, bounded routing/state,
and server-authoritative timer metadata. Nothing is stored in or published
through the Office Durable Object.
"""

import re
from urllib.parse import urlsplit


PREFIX = "/api/world/office/marketing-tasks"
UNIVERSAL_PREFIX = "/api/tasks"
LEGACY_MARKETING_PREFIX = PREFIX
BODY_MAX_BYTES = 64 * 1024
MAX_TASKS = 2000
MAX_CHECKINS_PER_TASK = 50
MAX_TITLE = 160
MAX_DETAILS = 4000
MAX_COMPLETION_NOTE = 4000
MAX_CHECKIN_NOTE = 500
MAX_AGENT_FIELD = 64
MIN_PRIORITY = 1
MAX_PRIORITY = 99
DEFAULT_PRIORITY = 50
MAX_ELAPSED_MS = 10 * 365 * 24 * 60 * 60 * 1000
MAX_BOUNTY_LAMPORTS = 1_000_000 * 1_000_000_000
MAX_PROOFS = 5000
MAX_PROOFS_PER_MEMBER = 100
MAX_INITIATIVES = 250
CHECKIN_MIN_MS = 4 * 60 * 1000
CHECKIN_MAX_MS = 9 * 60 * 1000

TASK_STATES = frozenset({"idle", "active", "done"})
TASK_KINDS = frozenset({"task", "bid"})
CHECKIN_STATES = frozenset({"going_well", "blocked", "needs_help"})
DESTINATIONS = frozenset({
    "department", "personal", "repository", "qa", "agent",
})
# One general bot replaces the old per-vendor Claude/Codex choice. The legacy
# kinds still decode from stored rows and are projected as "agent" so an
# existing task keeps working after the picker was removed.
AGENT_ASSIGNEE_KINDS = frozenset({"agent", "bot", "claude", "codex"})
ASSIGNEE_KINDS = (
    frozenset({"user", "unassigned"}) | AGENT_ASSIGNEE_KINDS
)
QA_STATES = frozenset({"unknown", "passed", "failed"})
DEPARTMENTS = (
    "general", "marketing", "engineering", "product-design", "security",
    "infrastructure", "community", "partnerships", "operations", "executive",
    "quality-assurance",
)
PERMISSION_RANK = {
    "read": 0,
    "write": 1,
    "maintain": 2,
    "admin": 3,
}

_ID_RE = re.compile(r"^[a-f0-9]{32}$")
_AGENT_REF_RE = re.compile(r"^[A-Za-z0-9._@#/-]{1,64}$")
_REPO_SEGMENT_RE = re.compile(r"^[A-Za-z0-9._-]{1,100}$")
_SCOPE_RE = re.compile(r"^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$")
_SOL_AMOUNT_RE = re.compile(r"^(0|[1-9][0-9]{0,6})(?:\.([0-9]{1,9}))?$")


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


def _attachments(value):
    """Keep bounded metadata plus a tiny encrypted image preview with a task."""
    if not isinstance(value, list):
        return []
    result = []
    for item in value[:4]:
        if not isinstance(item, dict):
            continue
        name = _text(item.get("name"), 180)
        mime = _text(item.get("mime"), 100, "application/octet-stream")
        try:
            size = max(0, min(1024 * 1024, int(item.get("size") or 0)))
        except (TypeError, ValueError):
            size = 0
        if name and size:
            attachment = {"name": name, "mime": mime, "size": size}
            thumbnail = str(item.get("thumbnail") or "")
            if (
                mime in ("image/png", "image/jpeg", "image/webp")
                and len(thumbnail) <= 10_000
                and re.fullmatch(
                    r"data:image/(?:png|jpeg|webp);base64,[A-Za-z0-9+/=]+",
                    thumbnail,
                )
            ):
                attachment["thumbnail"] = thumbnail
            result.append(attachment)
    return result


def valid_id(value):
    return bool(_ID_RE.fullmatch(str(value or "").strip().lower()))


def _priority(value, default=DEFAULT_PRIORITY):
    try:
        value = int(value)
    except (TypeError, ValueError, OverflowError):
        value = int(default)
    return max(MIN_PRIORITY, min(MAX_PRIORITY, value))


def _agent_ref(value):
    """Bound an opaque desktop-side agent reference (bot label, session id).

    Anything that is not already a compact token is dropped rather than
    squeezed into one: a mangled bot label is worse than no bot label.
    """

    clean = _text(value, MAX_AGENT_FIELD)
    return clean if _AGENT_REF_RE.fullmatch(clean) else ""


def _agent_run(value):
    """Bound the run provenance a desktop records against an agent task.

    A prompt launched from the desktop opens the task, so the task itself has
    to explain the run without reaching back into a transcript nobody else can
    read: which bot opened it, which bot reported it finished, and the model,
    permission mode, and reasoning strength the run used. Every field is
    encrypted with the rest of the task copy; ``None`` means "no agent run".
    """

    record = value if isinstance(value, dict) else {}
    run = {
        "provider": _text(record.get("provider"), 40).lower(),
        "startedBy": _agent_ref(record.get("startedBy")).lower(),
        "finishedBy": _agent_ref(record.get("finishedBy")).lower(),
        "model": _text(record.get("model"), MAX_AGENT_FIELD),
        "mode": _text(record.get("mode"), 40),
        "strength": _text(record.get("strength"), 32).lower(),
        "sessionId": _agent_ref(record.get("sessionId")),
    }
    return run if any(run.values()) else None


def _sol_amount(value):
    """Return a canonical SOL string backed by an exact lamport integer."""

    match = _SOL_AMOUNT_RE.fullmatch(str(value or "").strip())
    if not match:
        return "", 0
    whole = int(match.group(1))
    fraction = (match.group(2) or "").ljust(9, "0")
    lamports = whole * 1_000_000_000 + int(fraction or "0")
    if lamports <= 0 or lamports > MAX_BOUNTY_LAMPORTS:
        return "", 0
    canonical_fraction = f"{lamports % 1_000_000_000:09d}".rstrip("0")
    canonical = str(lamports // 1_000_000_000)
    if canonical_fraction:
        canonical += "." + canonical_fraction
    return canonical, lamports


def _route(path):
    clean = str(path or "").rstrip("/")
    base = next(
        (
            candidate
            for candidate in (UNIVERSAL_PREFIX, LEGACY_MARKETING_PREFIX)
            if clean == candidate or clean.startswith(candidate + "/")
        ),
        "",
    )
    if not base:
        return None
    if clean == base:
        return ("collection", "", "")
    # Collection actions must be resolved before the opaque task-id parser.
    if clean == base + "/stop-active":
        return ("stop-active", "", "stop-active")
    if clean == base + "/proofs":
        return ("proofs", "", "proofs")
    if clean == base + "/initiatives":
        return ("initiatives", "", "initiatives")
    if not clean.startswith(base + "/"):
        return None
    parts = clean[len(base) + 1:].split("/")
    if not parts or not valid_id(parts[0]):
        return None
    task_id = parts[0].lower()
    if len(parts) == 1:
        return ("task", task_id, "")
    if len(parts) == 2 and parts[1] in (
            "start", "stop", "checkin", "complete", "qa", "return"):
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
            "SELECT c.* FROM organization_task_checkins c "
            "WHERE c.task_id IN (" + placeholders + ") "
            "AND c.checkin_id=("
            "SELECT newest.checkin_id "
            "FROM organization_task_checkins newest "
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
    state = (
        "done"
        if int(row.get("completed_at") or 0) > 0
        else str(row.get("status") or "")
    )
    if state not in TASK_STATES:
        return None
    started_at = int(row.get("started_at") or 0) if state == "active" else 0
    elapsed_ms = max(0, int(row.get("elapsed_ms") or 0))
    if started_at:
        elapsed_ms += max(0, int(now) - started_at)
    assignee_kind = str(row.get("assignee_kind") or "user")
    if assignee_kind in AGENT_ASSIGNEE_KINDS:
        assignee_kind = "agent"
    elif assignee_kind not in ASSIGNEE_KINDS:
        assignee_kind = "user"
    qa_status = str(row.get("qa_status") or "unknown")
    if qa_status not in QA_STATES:
        qa_status = "unknown"
    task_kind = str(data.get("kind") or "task").strip().lower()
    if task_kind not in TASK_KINDS:
        task_kind = "task"
    bounty_request = None
    if task_kind == "bid":
        amount_sol, lamports = _sol_amount(
            (data.get("bountyRequest") or {}).get("amountSol")
            if isinstance(data.get("bountyRequest"), dict)
            else ""
        )
        if amount_sol and lamports:
            bounty_request = {
                "currency": "SOL",
                "amountSol": amount_sol,
                "lamports": lamports,
                "status": "requested",
            }
    return {
        "id": str(row.get("task_id") or ""),
        "kind": task_kind,
        "title": _text(data.get("title"), MAX_TITLE, "Organization task"),
        "details": _text(data.get("details"), MAX_DETAILS),
        "attachments": _attachments(data.get("attachments")),
        "completionNote": _text(
            data.get("completionNote"), MAX_COMPLETION_NOTE),
        "createdBy": _text(data.get("createdBy"), 64).lower(),
        "parentTaskId": (
            str(data.get("parentTaskId") or "").lower()
            if valid_id(data.get("parentTaskId")) else ""
        ),
        "bountyRequest": bounty_request,
        "assignee": (
            "agent"
            if assignee_kind == "agent"
            else _text(data.get("assignee"), 64).lower()
        ),
        "assigneeKind": assignee_kind,
        "department": _text(row.get("department"), 64, "general").lower(),
        "team": _text(row.get("team"), 64).lower(),
        "destination": str(row.get("destination") or "department"),
        "priority": _priority(row.get("priority")),
        "repository": _text(data.get("repository"), 201),
        "agentSessionId": str(row.get("agent_session_id") or ""),
        "agent": _agent_run(data.get("agent")),
        "qa": {
            "status": qa_status,
            "reviewer": _text(data.get("qaReviewer"), 64).lower(),
            "reviewedAt": int(row.get("qa_reviewed_at") or 0),
            "requestedAt": int(row.get("qa_requested_at") or 0),
            "howToTest": _text(data.get("howToTest"), 720),
        },
        "status": state,
        "elapsedMs": min(MAX_ELAPSED_MS, elapsed_ms),
        "startedAt": started_at,
        "nextCheckinAt": (
            int(row.get("next_checkin_at") or 0)
            if state == "active" else 0
        ),
        "createdAt": int(row.get("created_at") or 0),
        "updatedAt": int(row.get("updated_at") or 0),
        "completedAt": int(row.get("completed_at") or 0),
        "lastCheckin": await _project_checkin(runtime, checkin),
    }


async def _notify_activity(
        runtime, org_bi, actor, task_id, row, now, action):
    """Best-effort private HUD fan-out; mutations never depend on it."""
    notifier = getattr(runtime, "notify_organization_task_activity", None)
    if not callable(notifier) and action == "started":
        notifier = getattr(runtime, "notify_engineering_task_started", None)
        if callable(notifier):
            try:
                projected = await _project_task(runtime, row, now)
                if projected:
                    await notifier(org_bi, actor, task_id, projected)
            except Exception:
                pass
        return
    if not callable(notifier) or not row:
        return
    try:
        projected = (
            row if isinstance(row, dict) and "title" in row
            else await _project_task(runtime, row, now)
        )
        if projected:
            await notifier(org_bi, actor, task_id, projected, action)
    except Exception:
        pass


async def _task(runtime, org_bi, task_id):
    return await runtime.d1_first(
        "SELECT * FROM organization_tasks "
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


async def _list(
        runtime, org_bi, account_bi, actor, can_manage, now,
        marketing_only=False, department="", team=""):
    where = ["org_bi=?"]
    arguments = [org_bi]
    if marketing_only:
        where.append("department='marketing'")
    elif department:
        where.append("department=?")
        arguments.append(department)
    if team:
        where.append("team=?")
        arguments.append(team)
    rows = await runtime.d1_all(
        "SELECT * FROM organization_tasks WHERE "
        + " AND ".join(where)
        + " ORDER BY completed_at>0,priority,updated_at DESC,task_id DESC LIMIT ?",
        *arguments,
        MAX_TASKS,
    )
    if marketing_only and not can_manage:
        # Preserve the private assignee-only behavior of the physical Marketing
        # wall while the universal organization list remains member-visible.
        rows = [
            row for row in rows or []
            if str(row.get("assignee_bi") or "") == account_bi
        ]
    else:
        rows = rows or []
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
    marketing_members = (
        await runtime.marketing_members(org_bi) if marketing_only else []
    )
    marketing_actor = (
        await runtime.marketing_member(org_bi, actor)
        if marketing_only else None
    )
    marketing_names = [
        _text(member.get("name"), 64).lower()
        for member in marketing_members
        if isinstance(member, dict) and _text(member.get("name"), 64)
    ]
    result = {
        "ok": True,
        "authorized": True,
        "privacyBoundary": "organization-private-encrypted-at-rest",
        "actor": actor,
        "canManage": bool(can_manage),
        "serverNow": int(now),
        "tasks": tasks,
        "marketingMembers": marketing_names if marketing_actor else [],
        "attendanceDays": (
            await runtime.marketing_attendance(marketing_members, now)
            if marketing_actor else []
        ),
        "proofs": (
            await _proof_records(runtime, org_bi)
            if marketing_actor else []
        ),
        "initiatives": (
            await _initiative_records(runtime, org_bi)
            if marketing_actor else []
        ),
    }
    if marketing_only and can_manage:
        # Assignment is deliberately narrower than organization management:
        # even an owner may only place Marketing work onto a Marketing member.
        result["members"] = marketing_names
    elif not marketing_only:
        result["members"] = [
            member["name"]
            for member in await runtime.organization_members(org_bi)
        ]
        result["teams"] = await runtime.organization_teams(org_bi)
        result["departments"] = list(DEPARTMENTS)
    return _response(runtime, result)


async def _initiative_records(runtime, org_bi):
    rows = await runtime.d1_all(
        "SELECT initiative_id,data,created_at "
        "FROM world_office_marketing_initiatives "
        "WHERE org_bi=? "
        "ORDER BY created_at DESC,initiative_id DESC LIMIT ?",
        org_bi,
        MAX_INITIATIVES,
    )
    records = []
    for row in rows or []:
        try:
            data = await runtime.open(row.get("data"))
        except Exception:
            continue
        if not isinstance(data, dict) or not valid_id(row.get("initiative_id")):
            continue
        owner = _text(data.get("owner"), 100)
        repo = _text(data.get("repo"), 100)
        number = int(data.get("number") or 0)
        title = _text(data.get("title"), MAX_TITLE, "Repository issue")
        if (
            not _REPO_SEGMENT_RE.fullmatch(owner)
            or not _REPO_SEGMENT_RE.fullmatch(repo)
            or number <= 0
            or number > 2_147_483_647
        ):
            continue
        records.append({
            "id": str(row.get("initiative_id")),
            "title": title,
            "repo": owner + "/" + repo,
            "number": number,
            "href": f"/{owner}/{repo}/issues/{number}",
            "createdAt": int(row.get("created_at") or 0),
        })
    return records


async def _initiative_create(
        runtime, org_bi, account_bi, actor, data, can_manage, now):
    marketing_actor = await runtime.marketing_member(org_bi, actor)
    if not can_manage and not marketing_actor:
        return _response(runtime, {"error": "marketing_team_only"}, status=403)
    owner = _text(data.get("owner"), 100)
    repo = _text(data.get("repo"), 100)
    title = _text(data.get("title"), MAX_TITLE, "Repository issue")
    try:
        number = int(data.get("number") or 0)
    except (TypeError, ValueError):
        number = 0
    if (
        not _REPO_SEGMENT_RE.fullmatch(owner)
        or not _REPO_SEGMENT_RE.fullmatch(repo)
        or number <= 0
        or number > 2_147_483_647
    ):
        return _response(runtime, {"error": "invalid_issue_source"}, status=400)
    source_bi = await runtime.blind(
        "office-marketing-initiative:"
        + owner.lower() + "/" + repo.lower() + "#" + str(number)
    )
    existing = await runtime.d1_first(
        "SELECT initiative_id FROM world_office_marketing_initiatives "
        "WHERE org_bi=? AND source_bi=?",
        org_bi,
        source_bi,
    )
    if existing:
        records = await _initiative_records(runtime, org_bi)
        initiative_id = str(existing.get("initiative_id") or "")
        initiative = next(
            (record for record in records if record["id"] == initiative_id),
            None,
        )
        return _response(runtime, {
            "ok": True,
            "existing": True,
            "initiative": initiative,
        })
    count = await runtime.d1_first(
        "SELECT COUNT(*) AS count "
        "FROM world_office_marketing_initiatives WHERE org_bi=?",
        org_bi,
    )
    if int((count or {}).get("count") or 0) >= MAX_INITIATIVES:
        return _response(
            runtime, {"error": "initiative_capacity_reached"}, status=409)
    initiative_id = runtime.new_id()
    if not valid_id(initiative_id):
        return _response(runtime, {"error": "id_generation_failed"}, status=500)
    sealed = await runtime.seal({
        "owner": owner,
        "repo": repo,
        "number": number,
        "title": title,
    })
    try:
        await runtime.d1_run(
            "INSERT INTO world_office_marketing_initiatives "
            "(initiative_id,org_bi,source_bi,data,created_by_bi,created_at) "
            "VALUES (?,?,?,?,?,?)",
            initiative_id,
            org_bi,
            source_bi,
            sealed,
            account_bi,
            now,
        )
    except Exception as error:
        message = str(error)
        if "catalog_full" in message:
            return _response(
                runtime, {"error": "initiative_capacity_reached"}, status=409)
        if "UNIQUE" in message.upper():
            return _response(runtime, {"ok": True, "existing": True})
        raise
    await runtime.audit(
        actor,
        "office.marketing_initiative_created",
        "office_marketing_initiative",
        initiative_id,
        details={"source": "repository_issue"},
    )
    records = await _initiative_records(runtime, org_bi)
    initiative = next(
        (record for record in records if record["id"] == initiative_id),
        None,
    )
    return _response(
        runtime, {"ok": True, "initiative": initiative}, status=201)


def _proof_url(value):
    raw = str(value or "").strip()
    if not raw or len(raw) > 500:
        return ""
    try:
        parsed = urlsplit(raw)
        port = parsed.port
    except (TypeError, ValueError):
        return ""
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username
        or parsed.password
        or parsed.fragment
        or port not in (None, 443)
    ):
        return ""
    host = parsed.hostname.lower().rstrip(".")
    if (
        host in ("localhost", "localhost.localdomain")
        or host.endswith(".local")
        or host.endswith(".internal")
        or re.fullmatch(r"\d+(?:\.\d+){3}", host)
        or ":" in host
    ):
        return ""
    return raw


async def _proof_records(runtime, org_bi):
    rows = await runtime.d1_all(
        "SELECT proof_id,account_bi,data,created_at "
        "FROM world_office_marketing_proofs "
        "WHERE org_bi=? "
        "ORDER BY created_at DESC,proof_id DESC LIMIT ?",
        org_bi,
        MAX_PROOFS,
    )
    records = []
    for row in rows or []:
        try:
            data = await runtime.open(row.get("data"))
        except Exception:
            continue
        if not isinstance(data, dict):
            continue
        url = _proof_url(data.get("url"))
        member = _text(data.get("member"), 64).lower()
        if not url or not member or not valid_id(row.get("proof_id")):
            continue
        records.append({
            "id": str(row.get("proof_id")),
            "member": member,
            "url": url,
            "host": _text(urlsplit(url).hostname, 120),
            "label": _text(data.get("label"), 120, urlsplit(url).hostname),
            "createdAt": int(row.get("created_at") or 0),
        })
    return records


async def _proof_create(
        runtime, org_bi, account_bi, actor, data, now):
    if not await runtime.marketing_member(org_bi, actor):
        return _response(runtime, {"error": "marketing_team_only"}, status=403)
    url = _proof_url(data.get("url"))
    if not url:
        return _response(runtime, {"error": "invalid_social_url"}, status=400)
    label = _text(
        data.get("label"), 120, urlsplit(url).hostname or "Social post")
    count = await runtime.d1_first(
        "SELECT COUNT(*) AS count FROM world_office_marketing_proofs "
        "WHERE org_bi=? AND account_bi=?",
        org_bi,
        account_bi,
    )
    if int((count or {}).get("count") or 0) >= MAX_PROOFS_PER_MEMBER:
        return _response(runtime, {"error": "proof_capacity_reached"}, status=409)
    proof_id = runtime.new_id()
    if not valid_id(proof_id):
        return _response(runtime, {"error": "id_generation_failed"}, status=500)
    sealed = await runtime.seal({
        "member": actor,
        "url": url,
        "label": label,
    })
    try:
        await runtime.d1_run(
            "INSERT INTO world_office_marketing_proofs "
            "(proof_id,org_bi,account_bi,url_bi,data,created_at) "
            "VALUES (?,?,?,?,?,?)",
            proof_id,
            org_bi,
            account_bi,
            await runtime.blind("office-marketing-proof:" + url.lower()),
            sealed,
            now,
        )
    except Exception as error:
        if "catalog_full" in str(error):
            return _response(
                runtime, {"error": "proof_capacity_reached"}, status=409)
        raise
    await runtime.audit(
        actor,
        "office.marketing_proof_created",
        "office_marketing_proof",
        proof_id,
        details={"source": "marketing_desk"},
    )
    records = await _proof_records(runtime, org_bi)
    proof = next(
        (record for record in records if record["id"] == proof_id),
        None,
    )
    return _response(
        runtime, {"ok": True, "proof": proof}, status=201)


async def _create(
        runtime, org_bi, account_bi, actor, data, can_manage, now,
        marketing_only=False):
    if marketing_only and not can_manage:
        return _response(runtime, {"error": "forbidden"}, status=403)
    title = _text(data.get("title"), MAX_TITLE)
    details = _text(data.get("details"), MAX_DETAILS)
    department = (
        "marketing"
        if marketing_only
        else _text(data.get("department"), 64, "general").lower()
    )
    team = (
        "marketing"
        if marketing_only
        else _text(data.get("team"), 64).lower()
    )
    destination = (
        "department"
        if marketing_only
        else str(data.get("destination") or "department").strip().lower()
    )
    requested_assignee_kind = (
        "user"
        if marketing_only
        else str(data.get("assigneeKind") or "user").strip().lower()
    )
    assignee_kind = requested_assignee_kind
    parent_task_id = str(data.get("parentTaskId") or "").strip().lower()
    parent_data = None
    parent_row = None
    if parent_task_id:
        if marketing_only or not valid_id(parent_task_id):
            return _response(
                runtime, {"error": "invalid_parent_task"}, status=400)
        parent_row = await _task(runtime, org_bi, parent_task_id)
        if not parent_row:
            return _response(
                runtime, {"error": "parent_task_not_found"}, status=404)
        try:
            parent_data = await runtime.open(parent_row.get("data"))
        except Exception:
            parent_data = None
        if not isinstance(parent_data, dict):
            return _response(
                runtime, {"error": "parent_task_unavailable"}, status=500)
        if (
            not can_manage
            and str(parent_row.get("created_by_bi") or "") != account_bi
            and str(parent_row.get("assignee_bi") or "") != account_bi
        ):
            return _response(runtime, {"error": "forbidden"}, status=403)
        # A follow-up is a continuation of the routed work. The server, rather
        # than the client, inherits its assignee and routing from the parent.
        assignee_kind = str(
            parent_row.get("assignee_kind") or "unassigned").lower()
        requested_assignee_kind = assignee_kind
        department = str(parent_row.get("department") or "general")
        team = str(parent_row.get("team") or "")
        destination = str(parent_row.get("destination") or "department")
        data = dict(data)
        data["assignee"] = parent_data.get("assignee")
        data["repository"] = parent_data.get("repository")
    task_kind = (
        "task"
        if marketing_only
        else str(data.get("kind") or "task").strip().lower()
    )
    if (
        not title
        or task_kind not in TASK_KINDS
        or not _SCOPE_RE.fullmatch(department)
        or (team and not _SCOPE_RE.fullmatch(team))
        or destination not in DESTINATIONS
        or assignee_kind not in ASSIGNEE_KINDS
    ):
        return _response(runtime, {"error": "invalid_task_routing"}, status=400)
    if department not in DEPARTMENTS:
        return _response(runtime, {"error": "invalid_department"}, status=400)

    bounty_request = None
    if task_kind == "bid":
        amount_sol, lamports = _sol_amount(data.get("bountyAmountSol"))
        if not amount_sol or not lamports:
            return _response(
                runtime, {"error": "invalid_bounty_request"}, status=400)
        # A lobby bid is always made by and assigned to the authenticated
        # organization member. Client-supplied assignee fields cannot make a
        # compensation request appear to come from somebody else.
        assignee_kind = "user"
        data = dict(data)
        data["assignee"] = actor
        bounty_request = {
            "currency": "SOL",
            "amountSol": amount_sol,
            "lamports": lamports,
            "status": "requested",
        }

    assignee = _text(data.get("assignee"), 64).lower()
    member = None
    assignee_bi = ""
    if assignee_kind == "user":
        assignee = assignee or actor
        member = (
            await runtime.marketing_member(org_bi, assignee)
            if marketing_only
            else await runtime.organization_member(org_bi, assignee)
        )
        if not member:
            return _response(
                runtime,
                {
                    "error": (
                        "assignee_not_marketing_member"
                        if marketing_only else "assignee_not_org_member"
                    )
                },
                status=400,
            )
        if not can_manage and member["name"] != actor:
            return _response(
                runtime, {"error": "cannot_assign_other_member"}, status=403)
        if team and not marketing_only and not await runtime.team_member(
                org_bi, team, member["name"]):
            return _response(
                runtime, {"error": "assignee_not_team_member"}, status=400)
        assignee = member["name"]
        assignee_bi = member["bi"]
    elif assignee_kind in AGENT_ASSIGNEE_KINDS:
        # The board carries one general bot; the node picks the runtime.
        assignee_kind = "agent"
        assignee = "agent"
        destination = "agent"
    else:
        assignee = ""

    repository = _text(data.get("repository"), 201)
    if repository:
        parts = repository.split("/", 1)
        if (
            len(parts) != 2
            or not _REPO_SEGMENT_RE.fullmatch(parts[0])
            or not _REPO_SEGMENT_RE.fullmatch(parts[1])
        ):
            return _response(
                runtime, {"error": "invalid_repository"}, status=400)
    if destination in ("repository", "agent") and not repository:
        return _response(
            runtime, {"error": "repository_required"}, status=400)
    how_to_test = _text(data.get("howToTest"), 720)
    attachments = _attachments(data.get("attachments"))
    requested_priority = _priority(data.get("priority"))
    if "priority" in data and not can_manage:
        return _response(runtime, {"error": "manager_required"}, status=403)
    # A desktop that launches a prompt opens the task in the same call, so the
    # run's provenance arrives with it rather than through a second round trip.
    agent_run = (
        _agent_run(data.get("agent"))
        if requested_assignee_kind in AGENT_ASSIGNEE_KINDS
        else None
    )
    agent_session_id = _agent_ref((agent_run or {}).get("sessionId"))
    qa_requested_at = (
        now
        if destination == "qa" or data.get("sendToQa") is True
        else 0
    )
    count = await runtime.d1_first(
        "SELECT COUNT(*) AS count FROM organization_tasks "
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
        "kind": task_kind,
        "title": title,
        "details": details,
        "attachments": attachments,
        "completionNote": "",
        "assignee": assignee,
        "createdBy": actor,
        "parentTaskId": parent_task_id,
        "bountyRequest": bounty_request,
        "repository": repository,
        "howToTest": how_to_test,
        "qaReviewer": "",
        "agent": agent_run,
    })
    try:
        await runtime.d1_run(
            "INSERT INTO organization_tasks "
            "(task_id,org_bi,department,team,destination,assignee_kind,"
            "status,assignee_bi,data,created_by_bi,created_at,updated_at,"
            "elapsed_ms,started_at,next_checkin_at,qa_requested_at,"
            "agent_session_id,priority) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            task_id,
            org_bi,
            department,
            team,
            destination,
            assignee_kind,
            "idle",
            assignee_bi,
            sealed,
            account_bi,
            now,
            now,
            0,
            0,
            0,
            qa_requested_at,
            agent_session_id,
            requested_priority,
        )
    except Exception as error:
        if "catalog_full" in str(error):
            return _response(
                runtime, {"error": "task_capacity_reached"}, status=409)
        raise
    await runtime.audit(
        actor,
        (
            "office.marketing_task_created"
            if marketing_only else "organization.task_created"
        ),
        "organization_task",
        task_id,
        details=(
            {"assigned": True}
            if marketing_only
            else {
                "kind": task_kind,
                "department": department,
                "destination": destination,
                "assigneeKind": assignee_kind,
                "bountyAmountSol": (
                    bounty_request["amountSol"] if bounty_request else ""
                ),
                "agentRun": bool(agent_run),
                "priority": requested_priority,
                "followUp": bool(parent_task_id),
            }
        ),
    )
    row = await _task(runtime, org_bi, task_id)
    await _notify_activity(
        runtime, org_bi, actor, task_id, row, now, "created")
    return await _task_response(runtime, row, now, status=201)


async def _update(
        runtime, org_bi, account_bi, actor, task_id, data, can_manage, now,
        marketing_only=False):
    row = await _task(runtime, org_bi, task_id)
    if not row:
        return _response(runtime, {"error": "task_not_found"}, status=404)
    try:
        current = await runtime.open(row.get("data"))
    except Exception:
        return _response(runtime, {"error": "task_unavailable"}, status=500)
    if not isinstance(current, dict):
        return _response(runtime, {"error": "task_unavailable"}, status=500)
    if set(data).issubset({"agentSessionId", "sessionToken"}):
        session_id = str(data.get("agentSessionId") or "").strip().lower()
        if (
            str(row.get("assignee_kind") or "") not in AGENT_ASSIGNEE_KINDS
            or not re.fullmatch(r"[a-f0-9-]{16,64}", session_id)
            or (
                not can_manage
                and str(row.get("created_by_bi") or "") != account_bi
            )
        ):
            return _response(
                runtime, {"error": "invalid_agent_session_link"}, status=400)
        await runtime.d1_run(
            "UPDATE organization_tasks SET agent_session_id=?,updated_at=? "
            "WHERE org_bi=? AND task_id=?",
            session_id, now, org_bi, task_id,
        )
        await runtime.audit(
            actor, "organization.task_agent_linked", "organization_task",
            task_id, details={"linked": True},
        )
        return await _task_response(
            runtime, await _task(runtime, org_bi, task_id), now)
    if not can_manage:
        return _response(runtime, {"error": "forbidden"}, status=403)
    if "priority" in data and set(data).issubset({"priority", "sessionToken"}):
        priority = _priority(data.get("priority"))
        await runtime.d1_run(
            "UPDATE organization_tasks SET priority=?,updated_at=? "
            "WHERE org_bi=? AND task_id=?",
            priority, now, org_bi, task_id,
        )
        await runtime.audit(
            actor, "organization.task_priority_changed",
            "organization_task", task_id,
            details={"priority": priority},
        )
        return await _task_response(
            runtime, await _task(runtime, org_bi, task_id), now)
    allowed = {
        "title", "details", "assignee", "assigneeKind", "priority",
        "repository", "sessionToken",
    }
    changed = [
        field for field in (
            "title", "details", "assignee", "assigneeKind", "priority",
            "repository",
        )
        if field in data
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
    assignee_kind = str(
        data.get("assigneeKind")
        if "assigneeKind" in data
        else row.get("assignee_kind") or "user"
    ).strip().lower()
    if assignee_kind in AGENT_ASSIGNEE_KINDS:
        assignee_kind = "agent"
    if assignee_kind not in ("user", "agent", "unassigned"):
        return _response(
            runtime, {"error": "invalid_assignee_kind"}, status=400)
    assignee = (
        _text(data.get("assignee"), 64).lower()
        if "assignee" in data
        else _text(current.get("assignee"), 64).lower()
    )
    repository = (
        _text(data.get("repository"), 201)
        if "repository" in data
        else _text(current.get("repository"), 201)
    )
    if repository:
        parts = repository.split("/", 1)
        if (
            len(parts) != 2
            or not _REPO_SEGMENT_RE.fullmatch(parts[0])
            or not _REPO_SEGMENT_RE.fullmatch(parts[1])
        ):
            return _response(
                runtime, {"error": "invalid_repository"}, status=400)
    priority = _priority(
        data.get("priority")
        if "priority" in data
        else row.get("priority"),
    )
    if not title:
        return _response(
            runtime, {"error": "title_required"}, status=400)
    if int(row.get("completed_at") or 0) > 0:
        return _response(
            runtime, {"error": "completed_task_cannot_be_updated"}, status=409)
    if str(row.get("status") or "") == "active":
        return _response(
            runtime, {"error": "active_task_cannot_be_updated"}, status=409)
    member = None
    assignee_bi = ""
    destination = str(row.get("destination") or "department")
    if assignee_kind == "user":
        if not assignee:
            return _response(
                runtime, {"error": "assignee_required"}, status=400)
        member = (
            await runtime.marketing_member(org_bi, assignee)
            if marketing_only
            else await runtime.organization_member(org_bi, assignee)
        )
        if not member:
            return _response(
                runtime,
                {
                    "error": (
                        "assignee_not_marketing_member"
                        if marketing_only else "assignee_not_org_member"
                    )
                },
                status=400,
            )
        assignee = member["name"]
        assignee_bi = member["bi"]
        if destination == "agent":
            destination = "department"
    elif assignee_kind == "agent":
        if marketing_only:
            return _response(runtime, {"error": "invalid_update"}, status=400)
        if not repository:
            return _response(
                runtime, {"error": "repository_required"}, status=400)
        assignee = "agent"
        destination = "agent"
    else:
        assignee = ""
        destination = (
            "department" if destination == "agent" else destination
        )
    agent_session_id = (
        ""
        if "assigneeKind" in data
        else str(row.get("agent_session_id") or "")
    )
    sealed = await runtime.seal({
        "kind": (
            str(current.get("kind") or "task")
            if str(current.get("kind") or "task") in TASK_KINDS
            else "task"
        ),
        "title": title,
        "details": details,
        "attachments": _attachments(current.get("attachments")),
        "completionNote": _text(
            current.get("completionNote"), MAX_COMPLETION_NOTE),
        "assignee": assignee,
        "createdBy": _text(current.get("createdBy"), 64).lower(),
        "parentTaskId": (
            str(current.get("parentTaskId") or "").lower()
            if valid_id(current.get("parentTaskId")) else ""
        ),
        "bountyRequest": (
            current.get("bountyRequest")
            if isinstance(current.get("bountyRequest"), dict)
            else None
        ),
        "repository": repository,
        "howToTest": _text(current.get("howToTest"), 720),
        "qaReviewer": _text(current.get("qaReviewer"), 64).lower(),
        # Editing the copy never rewrites who ran the task or how.
        "agent": _agent_run(current.get("agent")),
    })
    await runtime.d1_run(
        "UPDATE organization_tasks "
        "SET assignee_kind=?,assignee_bi=?,destination=?,agent_session_id=?,"
        "priority=?,data=?,updated_at=? "
        "WHERE org_bi=? AND task_id=? AND status='idle'",
        assignee_kind,
        assignee_bi,
        destination,
        agent_session_id,
        priority,
        sealed,
        now,
        org_bi,
        task_id,
    )
    changed_row = await _task(runtime, org_bi, task_id)
    if (
        not changed_row
        or str(changed_row.get("status") or "") != "idle"
        or str(changed_row.get("assignee_kind") or "") != assignee_kind
        or str(changed_row.get("assignee_bi") or "") != assignee_bi
        or str(changed_row.get("data") or "") != str(sealed)
    ):
        return _response(runtime, {"error": "task_state_conflict"}, status=409)
    await runtime.audit(
        actor,
        (
            "office.marketing_task_updated"
            if marketing_only else "organization.task_updated"
        ),
        "organization_task",
        task_id,
        details={"fields": ",".join(changed)},
    )
    return await _task_response(runtime, changed_row, now)


async def _start(
        runtime, org_bi, account_bi, actor, task_id, row, now):
    if str(row.get("assignee_bi") or "") != account_bi:
        return _response(runtime, {"error": "assignee_only"}, status=403)
    if int(row.get("completed_at") or 0) > 0:
        return _response(runtime, {"error": "task_completed"}, status=409)
    if str(row.get("status") or "") == "active":
        return await _task_response(runtime, row, now)
    existing = await runtime.d1_first(
        "SELECT task_id FROM organization_tasks "
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
            "UPDATE organization_tasks SET "
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
    await _notify_activity(
        runtime, org_bi, actor, task_id, changed, now, "started")
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
        "UPDATE organization_tasks SET "
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
    changed = await _task(runtime, org_bi, task_id)
    await _notify_activity(
        runtime, org_bi, actor, task_id, changed, now, "stopped")
    return await _task_response(runtime, changed, now)


async def _complete(
        runtime, org_bi, account_bi, actor, task_id, row, data,
        can_manage, now):
    """Finish assigned work and place it in the private QA review queue."""
    # A bot-assigned task has no member assignee_bi to match, so the desktop
    # that opened it — and only that desktop — reports its run as finished.
    launched_agent_run = (
        str(row.get("assignee_kind") or "") in AGENT_ASSIGNEE_KINDS
        and str(row.get("created_by_bi") or "") == account_bi
    )
    if (
        not can_manage
        and not launched_agent_run
        and str(row.get("assignee_bi") or "") != account_bi
    ):
        return _response(runtime, {"error": "assignee_or_manager_only"}, status=403)
    if (
        int(row.get("completed_at") or 0) > 0
        and int(row.get("qa_requested_at") or 0) > 0
    ):
        # Completion is idempotent, but a retried client may be adding the
        # human-readable work note after an older client already finished the
        # task. Preserve the completed/QA state and update only encrypted copy.
        if "completionNote" in data:
            try:
                completed_data = await runtime.open(row.get("data"))
            except Exception:
                completed_data = None
            if not isinstance(completed_data, dict):
                return _response(
                    runtime, {"error": "task_unavailable"}, status=500)
            completed_data["completionNote"] = _text(
                data.get("completionNote"), MAX_COMPLETION_NOTE)
            await runtime.d1_run(
                "UPDATE organization_tasks SET data=?,updated_at=? "
                "WHERE org_bi=? AND task_id=? AND completed_at>0",
                await runtime.seal(completed_data),
                now,
                org_bi,
                task_id,
            )
            await runtime.audit(
                actor,
                "organization.task_completion_note_updated",
                "organization_task",
                task_id,
                details={
                    "hasCompletionNote": bool(
                        completed_data["completionNote"]),
                },
            )
            row = await _task(runtime, org_bi, task_id)
        return await _task_response(runtime, row, now)
    try:
        current = await runtime.open(row.get("data"))
    except Exception:
        current = None
    if not isinstance(current, dict):
        return _response(runtime, {"error": "task_unavailable"}, status=500)
    current["completionNote"] = _text(
        data.get("completionNote")
        if "completionNote" in data
        else current.get("completionNote"),
        MAX_COMPLETION_NOTE,
    )
    if not _text(current.get("howToTest"), 720):
        current["howToTest"] = (
            "Open the completed feature and follow its normal user flow. "
            "Confirm the requested behavior works, existing behavior did not "
            "regress, and no console or network error appears."
        )
    current["qaReviewer"] = ""
    current["qaFailureReason"] = ""
    current["qaFailureScreenshot"] = ""
    # The finishing bot (and any model/mode/strength it ended up running with)
    # layers onto whatever the launching bot recorded when it opened the task.
    if isinstance(data.get("agent"), dict):
        merged = dict(current.get("agent") or {})
        merged.update({
            field: value
            for field, value in data["agent"].items()
            if value
        })
        current["agent"] = _agent_run(merged)
    started_at = int(row.get("started_at") or 0)
    addition = (
        max(0, int(now) - started_at)
        if str(row.get("status") or "") == "active" and started_at
        else 0
    )
    elapsed = min(
        MAX_ELAPSED_MS,
        max(0, int(row.get("elapsed_ms") or 0)) + addition,
    )
    await runtime.d1_run(
        "UPDATE organization_tasks SET "
        "status='idle',active_assignee_bi='',elapsed_ms=?,started_at=0,"
        "next_checkin_at=0,completed_at=?,destination='qa',"
        "qa_status='unknown',qa_reviewer_bi='',qa_reviewed_at=0,"
        "qa_requested_at=?,data=?,updated_at=? "
        "WHERE org_bi=? AND task_id=?",
        elapsed,
        now,
        now,
        await runtime.seal(current),
        now,
        org_bi,
        task_id,
    )
    changed = await _task(runtime, org_bi, task_id)
    if not changed or int(changed.get("completed_at") or 0) <= 0:
        return _response(runtime, {"error": "task_state_conflict"}, status=409)
    await runtime.audit(
        actor,
        "office.marketing_task_completed",
        "office_task",
        task_id,
        details={
            "state": "done",
            "qa": "ready",
            "hasCompletionNote": bool(current["completionNote"]),
        },
    )
    await _notify_activity(
        runtime, org_bi, actor, task_id, changed, now, "completed")
    return await _task_response(runtime, changed, now)


async def _request_qa(
        runtime, org_bi, account_bi, actor, task_id, row, data,
        can_manage, now):
    if (
        not can_manage
        and str(row.get("created_by_bi") or "") != account_bi
        and str(row.get("assignee_bi") or "") != account_bi
    ):
        return _response(
            runtime, {"error": "task_owner_or_manager_only"}, status=403)
    try:
        current = await runtime.open(row.get("data"))
    except Exception:
        current = None
    if not isinstance(current, dict):
        return _response(runtime, {"error": "task_unavailable"}, status=500)
    how_to_test = _text(
        data.get("howToTest"),
        720,
        _text(
            current.get("howToTest"),
            720,
            "Follow the normal user flow and confirm the requested behavior.",
        ),
    )
    current["howToTest"] = how_to_test
    current["qaReviewer"] = ""
    await runtime.d1_run(
        "UPDATE organization_tasks SET destination='qa',qa_status='unknown',"
        "qa_reviewer_bi='',qa_reviewed_at=0,qa_requested_at=?,data=?,"
        "updated_at=? WHERE org_bi=? AND task_id=?",
        now, await runtime.seal(current), now, org_bi, task_id,
    )
    await runtime.audit(
        actor, "organization.task_sent_to_qa", "organization_task", task_id,
        details={"qa": "requested"},
    )
    changed = await _task(runtime, org_bi, task_id)
    await _notify_activity(
        runtime, org_bi, actor, task_id, changed, now, "qa requested")
    return await _task_response(runtime, changed, now)


async def _return_to_list(
        runtime, org_bi, account_bi, actor, task_id, row, can_manage, now):
    """Pull a bot task back off the node queue and onto the task list."""

    if (
        not can_manage
        and str(row.get("created_by_bi") or "") != account_bi
    ):
        return _response(
            runtime, {"error": "task_owner_or_manager_only"}, status=403)
    if (
        str(row.get("assignee_kind") or "") not in AGENT_ASSIGNEE_KINDS
        and str(row.get("destination") or "") != "agent"
    ):
        return _response(runtime, {"error": "task_not_queued"}, status=409)
    if int(row.get("completed_at") or 0) > 0:
        return _response(runtime, {"error": "task_already_done"}, status=409)
    try:
        current = await runtime.open(row.get("data"))
    except Exception:
        current = None
    if not isinstance(current, dict):
        return _response(runtime, {"error": "task_unavailable"}, status=500)
    session_id = str(row.get("agent_session_id") or "")
    # Best effort: a node that already leased the job simply finds it retired.
    cancel = getattr(runtime, "cancel_agent_session", None)
    if session_id and callable(cancel):
        try:
            await cancel(org_bi, session_id)
        except Exception:
            pass
    current["assignee"] = ""
    await runtime.d1_run(
        "UPDATE organization_tasks SET assignee_kind='unassigned',"
        "assignee_bi='',destination='department',agent_session_id='',"
        "data=?,updated_at=? WHERE org_bi=? AND task_id=?",
        await runtime.seal(current), now, org_bi, task_id,
    )
    await runtime.audit(
        actor, "organization.task_returned", "organization_task", task_id,
        details={"returned": True, "hadSession": bool(session_id)},
    )
    changed = await _task(runtime, org_bi, task_id)
    await _notify_activity(
        runtime, org_bi, actor, task_id, changed, now, "returned")
    return await _task_response(runtime, changed, now)


async def _delete(runtime, org_bi, actor, task_id, can_manage):
    if not can_manage:
        return _response(runtime, {"error": "forbidden"}, status=403)
    prior = await _task(runtime, org_bi, task_id)
    await runtime.d1_run(
        "DELETE FROM organization_task_checkins "
        "WHERE org_bi=? AND task_id=?",
        org_bi,
        task_id,
    )
    await runtime.d1_run(
        "DELETE FROM organization_task_qa_reviews "
        "WHERE org_bi=? AND task_id=?",
        org_bi,
        task_id,
    )
    await runtime.d1_run(
        "DELETE FROM organization_tasks "
        "WHERE org_bi=? AND task_id=?",
        org_bi,
        task_id,
    )
    # A task promoted out of the legacy Marketing tables keeps its original
    # row there, and the lazy-schema bootstrap replays its
    # "INSERT OR IGNORE INTO organization_tasks ... SELECT ... FROM
    # world_office_marketing_tasks" backfill on every schema fingerprint
    # change. Without this purge the deleted task silently reappears on the
    # next deployment. The legacy tables are absent on databases created after
    # the promotion, so a missing table is not an error here.
    for legacy in (
            "world_office_marketing_checkins",
            "world_office_marketing_tasks",
    ):
        try:
            await runtime.d1_run(
                "DELETE FROM " + legacy + " WHERE org_bi=? AND task_id=?",
                org_bi,
                task_id,
            )
        except Exception:
            pass
    remaining = await _task(runtime, org_bi, task_id)
    if remaining:
        return _response(runtime, {"error": "task_delete_conflict"}, status=409)
    await runtime.audit(
        actor,
        "office.marketing_task_deleted",
        "office_task",
        task_id,
        details={"state": "deleted"},
    )
    await _notify_activity(
        runtime, org_bi, actor, task_id, prior, runtime.now(), "deleted")
    return _response(runtime, {"ok": True, "deleted": True, "id": task_id})


async def _stop_active(runtime, org_bi, account_bi, actor, now):
    """Idempotently stop this account's active timer using server time."""
    row = await runtime.d1_first(
        "SELECT task_id,elapsed_ms,started_at "
        "FROM organization_tasks "
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
        "UPDATE organization_tasks SET "
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
        "SELECT status FROM organization_tasks "
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
        "DELETE FROM organization_task_checkins WHERE checkin_id IN ("
        "SELECT checkin_id FROM organization_task_checkins "
        "WHERE task_id=? ORDER BY created_at DESC,checkin_id DESC "
        "LIMIT -1 OFFSET ?)",
        task_id,
        MAX_CHECKINS_PER_TASK - 1,
    )
    try:
        await runtime.d1_run(
            "INSERT INTO organization_task_checkins "
            "(checkin_id,task_id,org_bi,account_bi,state,data,created_at) "
            "SELECT ?,?,?,?,?,?,? "
            "FROM organization_tasks "
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
        "SELECT checkin_id FROM organization_task_checkins "
        "WHERE checkin_id=?",
        checkin_id,
    )
    if not inserted:
        return _response(runtime, {"error": "task_state_conflict"}, status=409)
    deadline = _checkin_deadline(runtime, now)
    await runtime.d1_run(
        "UPDATE organization_tasks "
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
    marketing_only = str(path or "").rstrip("/").startswith(
        LEGACY_MARKETING_PREFIX)
    allowed = (
        ("GET", "POST")
        if route[0] in ("collection", "proofs", "initiatives")
        else ("POST",)
        if route[0] == "stop-active"
        else ("GET", "PATCH", "DELETE")
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
    if not role:
        # Task titles and routing are organization-private. A valid ForkMesh
        # account without current membership receives no existence signal.
        return _response(runtime, {"error": "org_member_required"}, status=403)
    if kind == "initiatives":
        if not marketing_only:
            return _response(runtime, {"error": "not_found"}, status=404)
        marketing_actor = await runtime.marketing_member(org_bi, actor)
        if method == "GET":
            if not marketing_actor:
                return _response(
                    runtime, {"error": "marketing_team_only"}, status=403)
            return _response(runtime, {
                "ok": True,
                "actor": actor,
                "initiatives": await _initiative_records(runtime, org_bi),
            })
        return await _initiative_create(
            runtime, org_bi, account_bi, actor, data, can_manage, now)
    if kind == "proofs":
        if not marketing_only:
            return _response(runtime, {"error": "not_found"}, status=404)
        if not await runtime.marketing_member(org_bi, actor):
            return _response(
                runtime, {"error": "marketing_team_only"}, status=403)
        if method == "POST":
            return await _proof_create(
                runtime, org_bi, account_bi, actor, data, now)
        return _response(runtime, {
            "ok": True,
            "actor": actor,
            "proofs": await _proof_records(runtime, org_bi),
        })
    if kind == "stop-active":
        return await _stop_active(
            runtime, org_bi, account_bi, actor, now)
    if kind == "collection":
        if method == "GET":
            department = ""
            team = ""
            if not marketing_only:
                department = _text(
                    runtime.query("department"), 64).lower()
                team = _text(runtime.query("team"), 64).lower()
                if (
                    (department and (
                        department not in DEPARTMENTS
                        or not _SCOPE_RE.fullmatch(department)
                    ))
                    or (team and not _SCOPE_RE.fullmatch(team))
                ):
                    return _response(
                        runtime, {"error": "invalid_task_filter"}, status=400)
            return await _list(
                runtime, org_bi, account_bi, actor, can_manage, now,
                marketing_only=marketing_only,
                department=department,
                team=team,
            )
        return await _create(
            runtime, org_bi, account_bi, actor, data, can_manage, now,
            marketing_only=marketing_only,
        )

    row = await _task(runtime, org_bi, task_id)
    if (
        not row
        or (
            marketing_only
            and str(row.get("department") or "") != "marketing"
        )
    ):
        return _response(runtime, {"error": "task_not_found"}, status=404)
    if kind == "task":
        if method == "GET":
            if (
                marketing_only
                and not can_manage
                and str(row.get("assignee_bi") or "") != account_bi
            ):
                return _response(
                    runtime, {"error": "task_not_found"}, status=404)
            return await _task_response(runtime, row, now)
        if method == "DELETE":
            return await _delete(
                runtime, org_bi, actor, task_id, can_manage)
        return await _update(
            runtime, org_bi, account_bi, actor, task_id, data, can_manage, now,
            marketing_only=marketing_only,
        )
    if action == "start":
        return await _start(
            runtime, org_bi, account_bi, actor, task_id, row, now)
    if action == "stop":
        return await _stop(
            runtime, org_bi, account_bi, actor, task_id, row, now)
    if action == "complete":
        return await _complete(
            runtime, org_bi, account_bi, actor, task_id, row, data,
            can_manage, now)
    if action == "return":
        return await _return_to_list(
            runtime, org_bi, account_bi, actor, task_id, row, can_manage, now)
    if action == "qa":
        return await _request_qa(
            runtime, org_bi, account_bi, actor, task_id, row, data,
            can_manage, now)
    return await _checkin(
        runtime, org_bi, account_bi, actor, task_id, row, data, now)
