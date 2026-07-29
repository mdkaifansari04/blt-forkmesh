"""D1-backed encrypted chat-channel and private-membership API."""

import re


CHANNEL_BODY_MAX_BYTES = 64 * 1024
MAX_CHAT_CHANNELS = 100
MAX_CHANNEL_MEMBERS = 500
CHANNEL_NAME_RE = re.compile(
    r"^[a-z0-9](?:[a-z0-9-]{0,38}[a-z0-9])?$")
CHANNEL_ID_RE = re.compile(r"^[0-9a-f]{32}$")
COLLECTION_RE = re.compile(r"^/api/chat/channels/?$")
MEMBERS_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/members/?$")
ROOM_ACCESS_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/room-access/?$")
HISTORY_RE = re.compile(
    r"^/api/chat/channels/([0-9a-f]{32})/history/?$")
HISTORY_MAX_MESSAGES = 200


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


async def _body(runtime):
    data, error = await runtime.json_body(CHANNEL_BODY_MAX_BYTES)
    if error:
        return None, _response(
            runtime,
            {"error": error},
            status=413 if error == "payload_too_large" else 400,
        )
    return data, None


def _channel_payload(row, record, can_manage):
    return {
        "id": str(row.get("channel_id") or ""),
        "name": str((record or {}).get("name") or ""),
        "updatedAt": int(row.get("updated_at") or 0),
        "keyVersion": int(row.get("key_version") or 1),
        "canManage": bool(can_manage),
        "visibility": _visibility(record),
    }


def _visibility(record):
    return "public" if (record or {}).get("visibility") == "public" else (
        "private"
    )


async def _create_channel(runtime, account_bi, actor, data):
    name = str((data or {}).get("name") or "").strip().lower()
    if not CHANNEL_NAME_RE.fullmatch(name):
        return _response(
            runtime, {"error": "invalid_channel_name"}, status=400)
    visibility = str(
        (data or {}).get("visibility") or "private"
    ).strip().lower()
    if visibility not in {"private", "public"}:
        return _response(
            runtime, {"error": "invalid_visibility"}, status=400)
    raw_members = (data or {}).get("members", [])
    if not isinstance(raw_members, list):
        return _response(runtime, {"error": "invalid_members"}, status=400)
    if len(raw_members) > MAX_CHANNEL_MEMBERS:
        return _response(runtime, {"error": "too_many_members"}, status=429)
    if visibility == "public" and raw_members:
        return _response(
            runtime, {"error": "members_not_allowed"}, status=400)

    initial_members = []
    seen_members = set()
    for value in raw_members:
        if not isinstance(value, str):
            return _response(
                runtime, {"error": "invalid_members"}, status=400)
        member_bi, canonical = await runtime.account(value)
        if not member_bi or not canonical:
            return _response(
                runtime, {"error": "user_not_found"}, status=404)
        if canonical in seen_members:
            continue
        seen_members.add(canonical)
        if await runtime.is_admin(canonical):
            continue
        initial_members.append((member_bi, canonical))

    name_bi = await runtime.blind("chat-channel-name:" + name)
    existing = await runtime.d1_first(
        "SELECT 1 AS one FROM chat_channels WHERE name_bi=?", name_bi)
    if existing:
        return _response(
            runtime, {"error": "channel_name_taken"}, status=409)
    count = await runtime.d1_first(
        "SELECT COUNT(*) AS n FROM chat_channels")
    if int((count or {}).get("n") or 0) >= MAX_CHAT_CHANNELS:
        return _response(
            runtime, {"error": "too_many_channels"}, status=429)

    now = runtime.now()
    channel_id = runtime.new_id()
    record = {
        "name": name,
        "createdBy": actor,
        "visibility": visibility,
    }
    sealed = await runtime.seal(record)
    statements = [(
        "INSERT INTO chat_channels "
        "(channel_id,name_bi,data,created_by_bi,created_at,updated_at,"
        "key_version) VALUES (?,?,?,?,?,?,1)",
        (channel_id, name_bi, sealed, account_bi, now, now),
    )]
    member_payloads = []
    for member_bi, canonical in initial_members:
        member_payloads.append({"username": canonical, "joinedAt": now})
        member_data = await runtime.seal({"username": canonical})
        statements.append((
            "INSERT INTO chat_channel_members "
            "(channel_id,member_bi,data,invited_by_bi,joined_at) "
            "VALUES (?,?,?,?,?)",
            (channel_id, member_bi, member_data, account_bi, now),
        ))
    await runtime.batch(statements)
    await runtime.audit(
        actor,
        "chat.channel.create",
        "chat_channel",
        channel_id,
        details={"reason": "administrator_created"},
    )
    row = {
        "channel_id": channel_id,
        "updated_at": now,
        "key_version": 1,
    }
    return _response(
        runtime,
        {"ok": True, "channel": _channel_payload(
            row, record, True), "members": member_payloads},
        status=201,
    )


async def _channel(runtime, channel_id):
    if not CHANNEL_ID_RE.fullmatch(str(channel_id or "")):
        return None, None
    row = await runtime.d1_first(
        "SELECT channel_id,data,updated_at,key_version FROM chat_channels "
        "WHERE channel_id=?",
        channel_id,
    )
    if not row:
        return None, None
    try:
        record = await runtime.open(row.get("data"))
    except Exception:
        return None, None
    return row, record


async def _list_channels(runtime, account_bi, actor, is_admin):
    if is_admin:
        rows = await runtime.d1_all(
            "SELECT channel_id,data,updated_at,key_version "
            "FROM chat_channels ORDER BY created_at,channel_id"
        )
    else:
        rows = await runtime.d1_all(
            "SELECT c.channel_id,c.data,c.updated_at,c.key_version,"
            "CASE WHEN m.member_bi IS NULL THEN 0 ELSE 1 END AS joined "
            "FROM chat_channels c LEFT JOIN chat_channel_members m "
            "ON m.channel_id=c.channel_id AND m.member_bi=? "
            "ORDER BY c.created_at,c.channel_id",
            account_bi,
        )
    channels = []
    for row in rows:
        try:
            record = await runtime.open(row.get("data"))
        except Exception:
            continue
        if (
            not is_admin
            and _visibility(record) != "public"
            and not bool(int(row.get("joined") or 0))
        ):
            continue
        payload = _channel_payload(row, record, is_admin)
        if _visibility(record) == "private":
            member_rows = await runtime.d1_all(
                "SELECT data,joined_at FROM chat_channel_members "
                "WHERE channel_id=? ORDER BY joined_at,member_bi",
                str(row.get("channel_id") or ""),
            )
            members = []
            creator = str((record or {}).get("createdBy") or "").strip().lower()
            if creator:
                members.append(creator)
            for member_row in member_rows:
                member = await _member_payload(runtime, member_row)
                username = member["username"]
                if username and username not in members:
                    members.append(username)
            payload["members"] = members
        channels.append(payload)
    return _response(runtime, {"channels": channels})


async def _require_admin(runtime, actor, action, channel_id):
    if await runtime.is_admin(actor):
        return None
    await runtime.audit(
        actor,
        action,
        "chat_channel",
        channel_id,
        outcome="denied",
        details={"reason": "platform_administrator_required"},
    )
    return _response(runtime, {"error": "admin_required"}, status=403)


async def _member_payload(runtime, row):
    try:
        record = await runtime.open(row.get("data"))
    except Exception:
        record = {}
    return {
        "username": str((record or {}).get("username") or ""),
        "joinedAt": int(row.get("joined_at") or 0),
    }


async def _list_members(runtime, channel_id):
    row, record = await _channel(runtime, channel_id)
    if not row:
        return _response(runtime, {"error": "not_found"}, status=404)
    if _visibility(record) != "private":
        return _response(
            runtime, {"error": "members_not_allowed"}, status=409)
    rows = await runtime.d1_all(
        "SELECT data,joined_at FROM chat_channel_members "
        "WHERE channel_id=? ORDER BY joined_at,member_bi",
        channel_id,
    )
    members = []
    for member_row in rows:
        member = await _member_payload(runtime, member_row)
        if member["username"]:
            members.append(member)
    return _response(runtime, {"members": members})


async def _add_member(runtime, account_bi, actor, channel_id, data):
    row, record = await _channel(runtime, channel_id)
    if not row:
        return _response(runtime, {"error": "not_found"}, status=404)
    if _visibility(record) != "private":
        return _response(
            runtime, {"error": "members_not_allowed"}, status=409)
    username = str((data or {}).get("username") or "").strip().lower()
    member_bi, canonical = await runtime.account(username)
    if not member_bi or not canonical:
        return _response(runtime, {"error": "user_not_found"}, status=404)
    existing = await runtime.d1_first(
        "SELECT data,joined_at FROM chat_channel_members "
        "WHERE channel_id=? AND member_bi=?",
        channel_id,
        member_bi,
    )
    if existing:
        return _response(
            runtime,
            {"ok": True, "member": await _member_payload(runtime, existing)},
        )
    if await runtime.is_admin(canonical):
        return _response(
            runtime,
            {
                "ok": True,
                "member": {"username": canonical, "joinedAt": 0},
                "implicitAdmin": True,
            },
        )
    count = await runtime.d1_first(
        "SELECT COUNT(*) AS n FROM chat_channel_members WHERE channel_id=?",
        channel_id,
    )
    if int((count or {}).get("n") or 0) >= MAX_CHANNEL_MEMBERS:
        return _response(runtime, {"error": "too_many_members"}, status=429)
    now = runtime.now()
    sealed = await runtime.seal({"username": canonical})
    await runtime.d1_run(
        "INSERT INTO chat_channel_members "
        "(channel_id,member_bi,data,invited_by_bi,joined_at) "
        "VALUES (?,?,?,?,?)",
        channel_id,
        member_bi,
        sealed,
        account_bi,
        now,
    )
    await runtime.audit(
        actor,
        "chat.channel.member.add",
        "chat_channel",
        channel_id,
        details={"reason": "administrator_invited_user"},
    )
    return _response(
        runtime,
        {"ok": True, "member": {"username": canonical, "joinedAt": now}},
        status=201,
    )


async def _remove_member(runtime, actor, channel_id, data):
    row, record = await _channel(runtime, channel_id)
    if not row:
        return _response(runtime, {"error": "not_found"}, status=404)
    if _visibility(record) != "private":
        return _response(
            runtime, {"error": "members_not_allowed"}, status=409)
    username = str((data or {}).get("username") or "").strip().lower()
    member_bi, _canonical = await runtime.account(username)
    if not member_bi:
        return _response(runtime, {"error": "user_not_found"}, status=404)
    deleted = await runtime.d1_first(
        "DELETE FROM chat_channel_members "
        "WHERE channel_id=? AND member_bi=? RETURNING member_bi",
        channel_id,
        member_bi,
    )
    removed = bool(deleted)
    if removed:
        await runtime.revoke_room(
            channel_id, int(row.get("key_version") or 1))
    current = await runtime.d1_first(
        "SELECT key_version FROM chat_channels WHERE channel_id=?",
        channel_id,
    )
    await runtime.audit(
        actor,
        "chat.channel.member.remove",
        "chat_channel",
        channel_id,
        details={"reason": "administrator_removed_user", "removed": removed},
    )
    return _response(
        runtime,
        {
            "ok": True,
            "removed": removed,
            "keyVersion": int((current or {}).get("key_version") or 1),
        },
    )


async def _readable_channel(runtime, account_bi, channel_id, is_admin):
    """The channel row/record this account may read, or (None, None)."""
    row, record = await _channel(runtime, channel_id)
    if not row:
        return None, None
    if not is_admin and _visibility(record) != "public":
        membership = await runtime.d1_first(
            "SELECT 1 AS one FROM chat_channel_members "
            "WHERE channel_id=? AND member_bi=?",
            channel_id,
            account_bi,
        )
        if not membership:
            return None, None
    return row, record


async def _history(runtime, account_bi, channel_id, is_admin):
    # Read-only replay of one channel room for clients that hold no WebSocket
    # into it (the desktop app mirroring the World office's rooms). Bodies stay
    # exactly as the room retained them — AES-GCM envelopes only the room key
    # opens — so this endpoint hands out no plaintext; the caller decrypts with
    # the passphrase returned alongside, which the same authorization gates.
    row, record = await _readable_channel(
        runtime, account_bi, channel_id, is_admin)
    if not row:
        return _response(runtime, {"error": "not_found"}, status=404)
    key_version = int(row.get("key_version") or 1)
    try:
        since = int(str(runtime.query_params().get("since", "0")) or 0)
    except (TypeError, ValueError):
        since = 0
    entries = await runtime.history(channel_id, key_version, max(0, since))
    messages = [
        {"ts": int(entry.get("ts") or 0), "body": str(entry.get("body") or "")}
        for entry in (entries or [])[-HISTORY_MAX_MESSAGES:]
    ]
    return _response(
        runtime,
        {
            "channel": _channel_payload(row, record, is_admin),
            "room": (
                "chat-channel:" + str(channel_id) + ":v" + str(key_version)
            ),
            "keyVersion": key_version,
            "passphrase": await runtime.channel_passphrase(
                channel_id, key_version),
            "messages": messages,
            "latestTs": max(
                [int(message["ts"]) for message in messages] + [since]),
        },
    )


async def _room_access(runtime, account_bi, actor, channel_id, is_admin):
    row, record = await _readable_channel(
        runtime, account_bi, channel_id, is_admin)
    if not row:
        return _response(runtime, {"error": "not_found"}, status=404)
    access = await runtime.room_access(
        channel_id,
        int(row.get("key_version") or 1),
        account_bi,
        actor,
    )
    payload = dict(access or {})
    payload["channel"] = _channel_payload(row, record, is_admin)
    return _response(runtime, payload)


async def handle(runtime, path):
    """Serve authenticated encrypted-channel collection and resources."""
    await runtime.ensure_schema()
    normalized_path = str(path or "")
    collection = COLLECTION_RE.fullmatch(normalized_path)
    members = MEMBERS_RE.fullmatch(normalized_path)
    room_access = ROOM_ACCESS_RE.fullmatch(normalized_path)
    history = HISTORY_RE.fullmatch(normalized_path)
    if not (collection or members or room_access or history):
        return _response(runtime, {"error": "not_found"}, status=404)
    method = runtime.method()
    body_required = method in {"POST", "DELETE"}
    data = None
    if body_required:
        data, error_response = await _body(runtime)
        if error_response is not None:
            return error_response
    account_bi, account = await runtime.session(data)
    actor = str((account or {}).get("name") or "").strip().lower()
    if not account_bi or not actor:
        return _response(
            runtime, {"error": "invalid_session"}, status=401)
    is_admin = await runtime.is_admin(actor)

    if collection and method == "GET":
        return await _list_channels(runtime, account_bi, actor, is_admin)
    if collection and method == "POST":
        denied = await _require_admin(
            runtime, actor, "chat.channel.create", "new")
        if denied is not None:
            return denied
        return await _create_channel(runtime, account_bi, actor, data)

    channel_id = (members or room_access or history).group(1)
    if members and method in {"GET", "POST", "DELETE"}:
        action = {
            "GET": "chat.channel.member.list",
            "POST": "chat.channel.member.add",
            "DELETE": "chat.channel.member.remove",
        }[method]
        denied = await _require_admin(runtime, actor, action, channel_id)
        if denied is not None:
            return denied
        if method == "GET":
            return await _list_members(runtime, channel_id)
        if method == "POST":
            return await _add_member(
                runtime, account_bi, actor, channel_id, data)
        return await _remove_member(runtime, actor, channel_id, data)

    if room_access and method == "GET":
        return await _room_access(
            runtime, account_bi, actor, channel_id, is_admin)

    if history and method == "GET":
        return await _history(runtime, account_bi, channel_id, is_admin)

    allow = "GET, POST" if collection else (
        "GET, POST, DELETE" if members else "GET")
    return _response(
        runtime,
        {"error": "method_not_allowed"},
        status=405,
        allow=allow,
    )
