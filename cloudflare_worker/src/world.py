"""Privacy-safe protocol helpers for the transient multiplayer world.

This module is deliberately pure Python: the Durable Object in ``entry.py``
uses it to turn untrusted WebSocket JSON into a very small, allowlisted public
presence state.  Unknown fields are discarded rather than echoed, so repository
paths, wallet addresses, URLs, form contents, and other private client data
cannot accidentally become multiplayer payloads.

Nothing here identifies a network connection.  Country is an approximate
Cloudflare-provided two-letter code, while peer ids are random per connection.
"""

import math
import re
import unicodedata


# One shared virtual day lasts four real hours.  Clients advance the returned
# worldTimeMs locally and wrap at WORLD_DAY_LENGTH_MS, so every visitor sees the
# same cycle without polling.
WORLD_DAY_LENGTH_MS = 4 * 60 * 60 * 1000

# The socket is intentionally low-bandwidth: a client should send movement only
# a few times per second and a heartbeat while idle.
WORLD_MESSAGE_MAX_BYTES = 1024
WORLD_RATE_WINDOW_MS = 1000
WORLD_RATE_MAX_PER_WINDOW = 4
# Ordinary browser bursts can legitimately cross the soft budget while a
# connection publishes its profile and initial position.  The Durable Object
# drops those disposable excess frames, but a sender that keeps flooding past
# this bounded ceiling is still disconnected within the same one-second
# window.
WORLD_RATE_HARD_MAX_PER_WINDOW = 12
WORLD_BROADCAST_WINDOW_MS = 1000
WORLD_BROADCAST_MAX_PER_WINDOW = 24
WORLD_CONNECT_WINDOW_MS = 10 * 1000
WORLD_CONNECT_MAX_PER_WINDOW = 20
WORLD_CLIENT_STALE_MS = 90 * 1000
WORLD_MAX_CONNECTIONS = 64
WORLD_COORD_LIMIT = 512.0
WORLD_ARRIVAL_COLUMNS = 10
WORLD_ARRIVAL_X = -8.1
WORLD_ARRIVAL_Z = 30.0
WORLD_ARRIVAL_COLUMN_GAP = 1.8
WORLD_ARRIVAL_ROW_GAP = 2.1

WORLD_BROWSER_VALUES = frozenset({
    "chrome", "edge", "firefox", "safari", "other", "hidden",
})
WORLD_OS_VALUES = frozenset({
    "android", "chromeos", "ios", "linux", "macos", "windows", "other",
    "hidden",
})
WORLD_STATUS_VALUES = frozenset({
    "available", "away", "busy", "exploring", "hidden", "idle",
})
WORLD_ACTIVITY_VALUES = frozenset({
    "browsing-code-visualization", "exploring-town-square", "hidden",
    "reading-documentation", "viewing-repository", "visiting-office",
    "visiting-organization",
})
WORLD_FIRST_VISIT_AGE_VALUES = frozenset({
    "this-session", "today", "this-week", "this-month", "this-year",
    "over-a-year", "hidden",
})
WORLD_DOOR_VALUES = frozenset({"closed", "knock", "open"})
WORLD_EMOTE_VALUES = frozenset({"celebrate", "idea", "wave"})
WORLD_ACCOUNT_STATUS_VALUES = frozenset({
    "Guest", "Registered", "Supporting member", "Mirror operator",
    "Organization admin", "Verified bot",
})
WORLD_SPACE_VALUES = frozenset({
    "town-square", "east", "central", "west", "sky-campus",
    "space-station", "code-planet", "organization-region", "planet-atlas",
})
WORLD_INACTIVITY_VALUES = frozenset({
    "away", "inactive", "recent", "offline-operator", "returning",
})
WORLD_NODE_BADGE_MAX = 6
WORLD_STATUS_NOTE_MAX = 20

# This is the complete state that may leave the Durable Object.  Keeping the
# list explicit is the privacy boundary for snapshots and presence frames.
WORLD_PUBLIC_FIELDS = (
    "id", "name", "countryCode", "browser", "os", "status", "localTime",
    "activityCategory", "inputActive", "visitCount", "firstVisitAge",
    "accountStatus", "nodeCount", "space",
    "publicDoor", "statusEmoji", "statusNote",
    "x", "y", "z", "yaw", "moving", "updatedAt",
)

_COUNTRY_RE = re.compile(r"^[A-Z]{2}$")
_LOCAL_TIME_RE = re.compile(r"^(?:[01]\d|2[0-3]):[0-5]\d$")
_PEER_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,32}$")


def approximate_country_code(value):
    """Return an approximate two-letter code or an empty string.

    Cloudflare uses ``XX`` when the country is unknown and may use non-country
    sentinels such as ``T1``.  Neither is suitable for a public flag, so they
    collapse to no country rather than exposing lower-level connection data.
    """
    code = str(value or "").strip().upper()
    if not _COUNTRY_RE.fullmatch(code) or code == "XX":
        return ""
    return code


def clean_display_name(value, fallback="Guest"):
    """Bound a public display name and remove markup/control punctuation."""
    raw = str(value or "")[:128]
    # Unicode letters and digits are welcome.  A deliberately tiny punctuation
    # set keeps names readable while excluding HTML/URL syntax and controls.
    clean = "".join(
        ch for ch in raw if ch.isalnum() or ch in (" ", ".", "_", "-")
    )
    clean = " ".join(clean.split()).strip(" ._-")[:32].strip()
    if clean:
        return clean
    safe_fallback = "".join(
        ch for ch in str(fallback or "Guest")
        if ch.isalnum() or ch in (" ", ".", "_", "-")
    )
    return (" ".join(safe_fallback.split()).strip(" ._-")[:32] or "Guest")


def _emoji_base(character):
    codepoint = ord(character)
    return (
        codepoint in {
            0x00A9, 0x00AE, 0x203C, 0x2049, 0x2122, 0x2139,
            0x24C2, 0x3030, 0x303D, 0x3297, 0x3299,
        }
        or 0x2194 <= codepoint <= 0x21FF
        or codepoint in {0x231A, 0x231B, 0x2328, 0x23CF}
        or 0x23E9 <= codepoint <= 0x23F3
        or 0x23F8 <= codepoint <= 0x23FA
        or 0x25AA <= codepoint <= 0x25AB
        or codepoint in {0x25B6, 0x25C0}
        or 0x25FB <= codepoint <= 0x25FE
        or 0x2600 <= codepoint <= 0x27BF
        or 0x2934 <= codepoint <= 0x2935
        or 0x2B05 <= codepoint <= 0x2B07
        or 0x2B1B <= codepoint <= 0x2B1C
        or codepoint in {0x2B50, 0x2B55}
        or (
            0x1F000 <= codepoint <= 0x1FAFF
            and not 0x1F1E6 <= codepoint <= 0x1F1FF
        )
    )


def clean_status_emoji(value):
    """Return one bounded Unicode emoji grapheme or an empty string.

    This parser accepts pictographs, flags, keycaps, skin-tone modifiers,
    subdivision-flag tags, and zero-width-joiner sequences. It intentionally
    rejects arbitrary text and multiple adjacent emoji, keeping the public
    presence field both useful and mechanically bounded.
    """
    emoji = unicodedata.normalize("NFC", str(value or "").strip())
    if not emoji or len(emoji) > 24 or len(emoji.encode("utf-8")) > 96:
        return ""
    codepoints = [ord(character) for character in emoji]
    if (
        len(codepoints) == 2
        and all(0x1F1E6 <= codepoint <= 0x1F1FF for codepoint in codepoints)
    ):
        return emoji
    if (
        len(codepoints) in (2, 3)
        and emoji[0] in "#*0123456789"
        and codepoints[-1] == 0x20E3
        and (len(codepoints) == 2 or codepoints[1] == 0xFE0F)
    ):
        return emoji

    index = 0

    def consume_pictograph(offset):
        if offset >= len(emoji) or not _emoji_base(emoji[offset]):
            return -1
        base = ord(emoji[offset])
        offset += 1
        if offset < len(emoji) and ord(emoji[offset]) in (0xFE0E, 0xFE0F):
            offset += 1
        if offset < len(emoji) and 0x1F3FB <= ord(emoji[offset]) <= 0x1F3FF:
            offset += 1
        if offset < len(emoji) and 0xE0020 <= ord(emoji[offset]) <= 0xE007E:
            if base != 0x1F3F4:
                return -1
            while (
                offset < len(emoji)
                and 0xE0020 <= ord(emoji[offset]) <= 0xE007E
            ):
                offset += 1
            if offset >= len(emoji) or ord(emoji[offset]) != 0xE007F:
                return -1
            offset += 1
        return offset

    index = consume_pictograph(index)
    if index < 0:
        return ""
    while index < len(emoji):
        if ord(emoji[index]) != 0x200D:
            return ""
        index = consume_pictograph(index + 1)
        if index < 0:
            return ""
    return emoji


def clean_status_note(value):
    """Return one Unicode word of at most 20 code points."""
    note = unicodedata.normalize("NFKC", str(value or "").strip())
    if not note or len(note) > WORLD_STATUS_NOTE_MAX:
        return ""
    first_category = unicodedata.category(note[0])
    if first_category[:1] not in {"L", "N"}:
        return ""
    for character in note[1:]:
        if (
            unicodedata.category(character)[:1] not in {"L", "M", "N"}
            and character not in {"'", "\u2019", "-"}
        ):
            return ""
    return note


def _choice(value, allowed, fallback):
    choice = str(value or "").strip().lower()
    return choice if choice in allowed else fallback


def _bounded_number(value, fallback):
    if isinstance(value, bool):
        return fallback
    try:
        number = float(value)
    except (TypeError, ValueError):
        return fallback
    if not math.isfinite(number):
        return fallback
    number = max(-WORLD_COORD_LIMIT, min(WORLD_COORD_LIMIT, number))
    return round(number, 2)


def _bounded_yaw(value, fallback):
    if isinstance(value, bool):
        return fallback
    try:
        number = float(value)
    except (TypeError, ValueError):
        return fallback
    if not math.isfinite(number):
        return fallback
    return round(max(-math.pi, min(math.pi, number)), 3)


def _bounded_visit_count(value, fallback):
    """Return an integer visit count without coercing strings or booleans."""
    if isinstance(value, bool) or not isinstance(value, int):
        return fallback
    return max(0, min(999, value))


def arrival_position(slot):
    """Return one deterministic, non-overlapping Town Square arrival slot.

    The 64-person room fits into seven shallow rows.  Ten people fill a row
    before the next row begins, and yaw zero faces everyone toward the square
    instead of toward one another.  The slot itself stays private to the live
    Durable Object attachment; only the ordinary bounded coordinates leave it.
    """
    try:
        slot = int(slot)
    except (TypeError, ValueError):
        slot = 0
    slot = max(0, min(WORLD_MAX_CONNECTIONS - 1, slot))
    column = slot % WORLD_ARRIVAL_COLUMNS
    row = slot // WORLD_ARRIVAL_COLUMNS
    return {
        "x": round(WORLD_ARRIVAL_X + column * WORLD_ARRIVAL_COLUMN_GAP, 2),
        "y": 0.38,
        "z": round(WORLD_ARRIVAL_Z - row * WORLD_ARRIVAL_ROW_GAP, 2),
        "yaw": 0.0,
    }


def first_available_arrival_slot(used_slots):
    """Choose the first free room slot without consulting persistent storage."""
    used = set()
    for value in used_slots or ():
        if isinstance(value, bool):
            continue
        try:
            slot = int(value)
        except (TypeError, ValueError):
            continue
        if 0 <= slot < WORLD_MAX_CONNECTIONS:
            used.add(slot)
    for slot in range(WORLD_MAX_CONNECTIONS):
        if slot not in used:
            return slot
    return WORLD_MAX_CONNECTIONS - 1


def default_presence(peer_id, now):
    """Create a non-identifying, privacy-default per-connection state."""
    peer_id = str(peer_id or "")[:32]
    suffix = "".join(ch for ch in peer_id[:4] if ch.isalnum())
    return {
        "id": peer_id,
        "name": clean_display_name("", "Guest " + suffix if suffix else "Guest"),
        # The server keeps the coarse edge country privately on the live socket.
        # A client must explicitly send shareCountry=true before it is public.
        "countryCode": "",
        "browser": "hidden",
        "os": "hidden",
        "status": "hidden",
        "localTime": "",
        "activityCategory": "hidden",
        # These coarse indicators are meaningful only while generalized
        # activity sharing is enabled. They never contain event coordinates,
        # visited URLs, query strings, or timestamps.
        "inputActive": False,
        "visitCount": 0,
        "firstVisitAge": "hidden",
        # Account status and operator-belt count are supplied by the routing
        # Worker after it validates a short-lived world ticket. They are never
        # accepted from arbitrary socket JSON.
        "accountStatus": "Guest",
        "nodeCount": 0,
        "space": "town-square",
        "publicDoor": "closed",
        "statusEmoji": "",
        "statusNote": "",
        "x": 0.0,
        "y": 0.0,
        "z": 0.0,
        "yaw": 0.0,
        "moving": False,
        "updatedAt": int(now),
    }


def public_presence(state):
    """Return exactly the allowlisted public fields from a socket state."""
    state = state if isinstance(state, dict) else {}
    return {field: state.get(field) for field in WORLD_PUBLIC_FIELDS}


def sanitize_message(payload, current, now, country_source="",
                     trusted_name="", trusted_node_count=0):
    """Apply one allowlisted client frame.

    Returns ``(kind, new_state)`` for ``presence``, ``move``, and ``ping`` or
    ``None`` for every other shape.  Client-supplied ids/country codes and all
    unknown fields are ignored.
    """
    if not isinstance(payload, dict) or not isinstance(current, dict):
        return None
    kind = str(payload.get("type") or "").strip().lower()
    state = dict(current)

    if kind == "presence":
        if payload.get("shareName") is False:
            suffix = "".join(
                ch for ch in str(state.get("id") or "")[:4] if ch.isalnum())
            state["name"] = clean_display_name(
                "", "Guest " + suffix if suffix else "Guest")
        elif trusted_name:
            state["name"] = clean_display_name(
                trusted_name, state.get("name") or "Contributor")
        elif "name" in payload:
            state["name"] = clean_display_name(
                payload.get("name"), state.get("name") or "Guest")
        if "browser" in payload:
            state["browser"] = _choice(
                payload.get("browser"), WORLD_BROWSER_VALUES,
                state.get("browser") or "hidden")
        if "os" in payload:
            state["os"] = _choice(
                payload.get("os"), WORLD_OS_VALUES,
                state.get("os") or "hidden")
        if "status" in payload:
            state["status"] = _choice(
                payload.get("status"), WORLD_STATUS_VALUES,
                state.get("status") or "exploring")
        if "localTime" in payload:
            local_time = str(payload.get("localTime") or "").strip()
            state["localTime"] = (
                local_time if _LOCAL_TIME_RE.fullmatch(local_time) else "")
        if "activityCategory" in payload:
            state["activityCategory"] = _choice(
                payload.get("activityCategory"), WORLD_ACTIVITY_VALUES,
                "hidden")
        if "inputActive" in payload:
            state["inputActive"] = (
                payload.get("inputActive")
                if isinstance(payload.get("inputActive"), bool)
                else False)
        if "visitCount" in payload:
            state["visitCount"] = _bounded_visit_count(
                payload.get("visitCount"), 0)
        if "firstVisitAge" in payload:
            state["firstVisitAge"] = _choice(
                payload.get("firstVisitAge"),
                WORLD_FIRST_VISIT_AGE_VALUES,
                "hidden")
        # Activity privacy is the parent control for all three derived
        # indicators. Explicitly clear prior values so turning sharing off
        # cannot leave stale metadata visible in a live socket attachment.
        if state.get("activityCategory") == "hidden":
            state["inputActive"] = False
            state["visitCount"] = 0
            state["firstVisitAge"] = "hidden"
        if "publicDoor" in payload:
            state["publicDoor"] = _choice(
                payload.get("publicDoor"), WORLD_DOOR_VALUES, "closed")
        if "statusEmoji" in payload:
            state["statusEmoji"] = clean_status_emoji(
                payload.get("statusEmoji"))
        if "statusNote" in payload:
            state["statusNote"] = clean_status_note(
                payload.get("statusNote"))
        if not state.get("statusEmoji"):
            state["statusNote"] = ""
        if "space" in payload:
            state["space"] = _choice(
                payload.get("space"), WORLD_SPACE_VALUES, "town-square")
        if isinstance(payload.get("shareNodes"), bool):
            try:
                trusted_count = int(trusted_node_count or 0)
            except (TypeError, ValueError):
                trusted_count = 0
            state["nodeCount"] = (
                max(0, min(WORLD_NODE_BADGE_MAX, trusted_count))
                if payload["shareNodes"] else 0)
        # The connection's approximate country came from the edge, not the
        # client. A privacy toggle may hide or restore that coarse value without
        # accepting a spoofed country code from arbitrary JSON.
        if isinstance(payload.get("shareCountry"), bool):
            state["countryCode"] = (
                approximate_country_code(country_source)
                if payload["shareCountry"] else "")
        state["updatedAt"] = int(now)
        return kind, state

    if kind == "move":
        for field in ("x", "y", "z"):
            if field in payload:
                state[field] = _bounded_number(
                    payload.get(field), state.get(field, 0.0))
        if "yaw" in payload:
            state["yaw"] = _bounded_yaw(
                payload.get("yaw"), state.get("yaw", 0.0))
        if isinstance(payload.get("moving"), bool):
            state["moving"] = payload["moving"]
        state["updatedAt"] = int(now)
        return kind, state

    if kind == "ping":
        # Heartbeats refresh only the private socket liveness timestamp.  They
        # do not fabricate public movement/activity.
        return kind, state

    return None


def trusted_presence_claim(name="", account_status="Guest", node_count=0):
    """Sanitize the public parts of a server-validated account claim.

    The name and full node list remain private inputs to the live attachment.
    Only the status and bounded count enter the initial public state; the name
    and belt appear later if the owner explicitly enables those presence
    fields.
    """
    status = str(account_status or "Guest").strip()
    if status not in WORLD_ACCOUNT_STATUS_VALUES:
        status = "Guest"
    try:
        count = int(node_count or 0)
    except (TypeError, ValueError):
        count = 0
    return {
        "name": clean_display_name(name, "Contributor") if name else "",
        "accountStatus": status,
        "nodeCount": max(0, min(WORLD_NODE_BADGE_MAX, count)),
    }


def sanitize_inactivity_record(payload):
    """Return one consented generalized inactivity choice or ``None``."""
    if not isinstance(payload, dict):
        return None
    status = str(payload.get("status") or "").strip().lower()
    if status not in WORLD_INACTIVITY_VALUES:
        return None
    if payload.get("shareInactivity") is not True:
        return None
    return {
        "status": status,
        "shareName": payload.get("shareName") is True,
        "shareNodes": payload.get("shareNodes") is True,
    }


def sanitize_interaction(payload, sender_state):
    """Return one tiny targeted social gesture or ``None``.

    Interactions never carry user text, room names, repository data, or URLs.
    The Durable Object supplies the sender id and delivers the gesture only to
    the requested live peer, so clients cannot impersonate another visitor.
    """
    if not isinstance(payload, dict) or not isinstance(sender_state, dict):
        return None
    if payload.get("type") != "interaction":
        return None
    sender = str(sender_state.get("id") or "").strip()
    if not _PEER_ID_RE.fullmatch(sender):
        return None
    kind = str(payload.get("kind") or "").strip().lower()
    if kind in ("knock", "home-grant", "home-decline"):
        target = str(payload.get("target") or "").strip()
        if not _PEER_ID_RE.fullmatch(target) or target == sender:
            return None
        return {"type": "interaction", "kind": kind, "target": target}
    if kind == "emote":
        emote = str(payload.get("emote") or "").strip().lower()
        if emote not in WORLD_EMOTE_VALUES:
            return None
        return {"type": "interaction", "kind": "emote", "emote": emote}
    return None


def movement_delta(state):
    """Small outgoing movement frame; no profile or connection metadata."""
    state = state if isinstance(state, dict) else {}
    return {
        "type": "move",
        "id": state.get("id"),
        "x": state.get("x"),
        "y": state.get("y"),
        "z": state.get("z"),
        "yaw": state.get("yaw"),
        "moving": bool(state.get("moving")),
        "updatedAt": state.get("updatedAt"),
    }


def advance_rate_window(start, count, now):
    """Advance the per-socket fixed window and return allowed/start/count."""
    try:
        start = int(start or 0)
        count = int(count or 0)
    except (TypeError, ValueError):
        start, count = 0, 0
    now = int(now)
    if start <= 0 or now < start or now - start >= WORLD_RATE_WINDOW_MS:
        start, count = now, 0
    count += 1
    return count <= WORLD_RATE_MAX_PER_WINDOW, start, count


def advance_broadcast_window(start, count, now):
    """Room-wide frame budget that caps fan-out amplification."""
    try:
        start = int(start or 0)
        count = int(count or 0)
    except (TypeError, ValueError):
        start, count = 0, 0
    now = int(now)
    if (start <= 0 or now < start
            or now - start >= WORLD_BROADCAST_WINDOW_MS):
        start, count = now, 0
    count += 1
    return count <= WORLD_BROADCAST_MAX_PER_WINDOW, start, count


def advance_connection_window(start, count, now):
    """Room-wide upgrade admission budget to blunt connection churn."""
    try:
        start = int(start or 0)
        count = int(count or 0)
    except (TypeError, ValueError):
        start, count = 0, 0
    now = int(now)
    if (start <= 0 or now < start
            or now - start >= WORLD_CONNECT_WINDOW_MS):
        start, count = now, 0
    count += 1
    return count <= WORLD_CONNECT_MAX_PER_WINDOW, start, count


def presence_is_stale(last_seen, now):
    """Whether a socket should be removed from live snapshots/broadcasts."""
    try:
        last_seen = int(last_seen or 0)
        now = int(now)
    except (TypeError, ValueError):
        return True
    return last_seen <= 0 or now - last_seen > WORLD_CLIENT_STALE_MS


def context_payload(country_code, now, chat_connections=0):
    """Public context with no address, user-agent, or precise location data."""
    now = int(now)
    return {
        "ok": True,
        "countryCode": approximate_country_code(country_code),
        "serverTimeMs": now,
        "worldTimeMs": now % WORLD_DAY_LENGTH_MS,
        "worldDayLengthMs": WORLD_DAY_LENGTH_MS,
        "worldConnections": WORLD_MAX_CONNECTIONS,
        "worldMessagesPerSecond": WORLD_RATE_MAX_PER_WINDOW,
        "chatConnections": max(0, int(chat_connections or 0)),
    }
