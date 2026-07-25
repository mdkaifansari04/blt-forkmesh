"""D1-backed participant-only one-to-one direct-message API."""

import re


DIRECT_MESSAGE_BODY_MAX_BYTES = 16 * 1024
COLLECTION_RE = re.compile(r"^/api/chat/direct-messages/?$")
ROOM_ACCESS_RE = re.compile(
    r"^/api/chat/direct-messages/([0-9a-f]{32})/room-access/?$")


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
    data, error = await runtime.json_body(DIRECT_MESSAGE_BODY_MAX_BYTES)
    if error:
        return None, _response(
            runtime,
            {"error": error},
            status=413 if error == "payload_too_large" else 400,
        )
    return data, None


def _other_user(record, actor):
    participants = (record or {}).get("participants")
    if not isinstance(participants, list) or len(participants) != 2:
        return ""
    actor = str(actor or "").strip().lower()
    names = [str(value or "").strip().lower() for value in participants]
    if actor not in names:
        return ""
    return names[1] if names[0] == actor else names[0]


def _conversation_payload(row, record, actor):
    other_user = _other_user(record, actor)
    if not other_user:
        return None
    return {
        "id": str(row.get("conversation_id") or ""),
        "otherUser": other_user,
        "updatedAt": int(row.get("updated_at") or 0),
        "keyVersion": int(row.get("key_version") or 1),
    }


async def _conversation_by_pair(runtime, pair_bi):
    return await runtime.d1_first(
        "SELECT conversation_id,data,updated_at,key_version "
        "FROM chat_direct_conversations WHERE pair_bi=?",
        pair_bi,
    )


async def _opened_payload(runtime, row, actor):
    if not row:
        return None
    try:
        record = await runtime.open(row.get("data"))
    except Exception:
        return None
    return _conversation_payload(row, record, actor)


async def _start_conversation(runtime, account_bi, actor, data):
    username = str((data or {}).get("username") or "").strip().lower()
    target_bi, target = await runtime.account(username)
    if not target_bi or not target:
        return _response(runtime, {"error": "user_not_found"}, status=404)
    if target_bi == account_bi:
        return _response(
            runtime, {"error": "cannot_message_self"}, status=400)

    pair_bi = await runtime.blind(
        "chat-direct-pair:" + ":".join(sorted((account_bi, target_bi)))
    )
    existing = await _conversation_by_pair(runtime, pair_bi)
    payload = await _opened_payload(runtime, existing, actor)
    if payload:
        return _response(
            runtime, {"ok": True, "conversation": payload})

    conversation_id = runtime.new_id()
    now = runtime.now()
    record = {"participants": sorted((actor, target))}
    sealed_record = await runtime.seal(record)
    sealed_actor = await runtime.seal({"username": actor})
    sealed_target = await runtime.seal({"username": target})
    statements = [
        (
            "INSERT INTO chat_direct_conversations "
            "(conversation_id,pair_bi,data,created_at,updated_at,key_version) "
            "VALUES (?,?,?,?,?,1)",
            (conversation_id, pair_bi, sealed_record, now, now),
        ),
        (
            "INSERT INTO chat_direct_participants "
            "(conversation_id,participant_bi,data,joined_at) "
            "VALUES (?,?,?,?)",
            (conversation_id, account_bi, sealed_actor, now),
        ),
        (
            "INSERT INTO chat_direct_participants "
            "(conversation_id,participant_bi,data,joined_at) "
            "VALUES (?,?,?,?)",
            (conversation_id, target_bi, sealed_target, now),
        ),
    ]
    try:
        await runtime.batch(statements)
    except Exception:
        raced = await _conversation_by_pair(runtime, pair_bi)
        raced_payload = await _opened_payload(runtime, raced, actor)
        if raced_payload:
            return _response(
                runtime, {"ok": True, "conversation": raced_payload})
        raise
    row = {
        "conversation_id": conversation_id,
        "updated_at": now,
        "key_version": 1,
    }
    return _response(
        runtime,
        {
            "ok": True,
            "conversation": _conversation_payload(row, record, actor),
        },
        status=201,
    )


async def _list_conversations(runtime, account_bi, actor):
    rows = await runtime.d1_all(
        "SELECT c.conversation_id,c.data,c.updated_at,c.key_version "
        "FROM chat_direct_participants p "
        "JOIN chat_direct_conversations c "
        "ON c.conversation_id=p.conversation_id "
        "WHERE p.participant_bi=? "
        "ORDER BY c.updated_at DESC,c.conversation_id",
        account_bi,
    )
    conversations = []
    for row in rows:
        payload = await _opened_payload(runtime, row, actor)
        if payload:
            conversations.append(payload)
    return _response(runtime, {"conversations": conversations})


async def _room_access(runtime, account_bi, actor, conversation_id):
    row = await runtime.d1_first(
        "SELECT c.conversation_id,c.data,c.updated_at,c.key_version "
        "FROM chat_direct_conversations c "
        "JOIN chat_direct_participants p "
        "ON p.conversation_id=c.conversation_id "
        "WHERE c.conversation_id=? AND p.participant_bi=?",
        conversation_id,
        account_bi,
    )
    payload = await _opened_payload(runtime, row, actor)
    if not payload:
        return _response(runtime, {"error": "not_found"}, status=404)
    access = await runtime.room_access(
        conversation_id,
        int(row.get("key_version") or 1),
        account_bi,
    )
    result = dict(access or {})
    result["conversation"] = payload
    return _response(runtime, result)


async def handle(runtime, path):
    """Serve direct-message collection and participant room access."""
    await runtime.ensure_schema()
    normalized_path = str(path or "")
    collection = COLLECTION_RE.fullmatch(normalized_path)
    room_access = ROOM_ACCESS_RE.fullmatch(normalized_path)
    if not (collection or room_access):
        return _response(runtime, {"error": "not_found"}, status=404)

    method = runtime.method()
    if collection and method not in {"GET", "POST"}:
        return _response(
            runtime,
            {"error": "method_not_allowed"},
            status=405,
            allow="GET, POST",
        )
    if room_access and method != "GET":
        return _response(
            runtime,
            {"error": "method_not_allowed"},
            status=405,
            allow="GET",
        )

    data = None
    if collection and method == "POST":
        data, error_response = await _body(runtime)
        if error_response is not None:
            return error_response
    account_bi, account = await runtime.session(data)
    actor = str((account or {}).get("name") or "").strip().lower()
    if not account_bi or not actor:
        return _response(
            runtime, {"error": "invalid_session"}, status=401)

    if collection and method == "GET":
        return await _list_conversations(runtime, account_bi, actor)
    if collection and method == "POST":
        return await _start_conversation(
            runtime, account_bi, actor, data)
    return await _room_access(
        runtime, account_bi, actor, room_access.group(1))
