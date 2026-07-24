#!/usr/bin/env python3
"""Privacy and lifecycle contract for the transient multiplayer world."""

import ast
import asyncio
import importlib.util
import json
import re
import tomllib
from pathlib import Path
from types import SimpleNamespace
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
WORLD_PATH = ROOT / "src" / "world.py"
WRANGLER = ROOT / "wrangler.toml"
STATIC_ROUTES = ROOT / "src" / "static_routes.py"
REDIRECTS = ROOT / "public" / "_redirects"
SCHEMA = ROOT / "src" / "schema.py"
INACTIVE_MIGRATION = ROOT / "migrations" / "0051_world_presence_spaces.sql"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")

spec = importlib.util.spec_from_file_location("forkmesh_world_protocol", WORLD_PATH)
world = importlib.util.module_from_spec(spec)
spec.loader.exec_module(world)


def _top_level_node(name):
    for node in ast.parse(ENTRY_TEXT, filename=str(ENTRY)).body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            if node.name == name:
                return node
    raise AssertionError("%s not found in entry.py" % name)


def _function_without_docstring(name):
    node = _top_level_node(name)
    body = list(node.body)
    if (body and isinstance(body[0], ast.Expr)
            and isinstance(body[0].value, ast.Constant)
            and isinstance(body[0].value.value, str)):
        body = body[1:]
    clone = ast.FunctionDef(
        name=node.name,
        args=node.args,
        body=body,
        decorator_list=[],
        returns=node.returns,
        type_comment=node.type_comment,
    )
    return ast.unparse(ast.fix_missing_locations(clone))


class _Headers:
    def __init__(self, values=None, allowed=None):
        self.values = {
            str(key).lower(): value for key, value in (values or {}).items()
        }
        self.allowed = set(self.values if allowed is None else allowed)
        self.read = []

    def get(self, name, default=None):
        name = str(name).lower()
        self.read.append(name)
        if name not in self.allowed:
            raise AssertionError("unexpected privacy-sensitive header read: " + name)
        return self.values.get(name, default)


def _load_context_handler(now=17_500_000):
    nodes = [
        _top_level_node("world_request_country"),
        _top_level_node("world_context_handler"),
    ]
    module = ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[]))

    def json_response(data, status=200, cache_control=None, extra_headers=None,
                      **_kwargs):
        return {
            "status": status,
            "data": data,
            "cache_control": cache_control,
            "headers": dict(extra_headers or {}),
        }

    namespace = {
        "method_name": lambda request: str(request.method).upper(),
        "json_response": json_response,
        "world_protocol": world,
        "Date": SimpleNamespace(now=lambda: now),
        "MAX_CONNECTIONS": 128,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["world_context_handler"]


def test_context_returns_only_country_and_shared_clock_fields():
    now = 91_234_567
    headers = _Headers(
        {"cf-ipcountry": "us"},
        allowed={"cf-ipcountry"},
    )
    request = SimpleNamespace(method="GET", headers=headers)
    response = _load_context_handler(now)(request)

    assert response["status"] == 200
    assert response["data"] == {
        "ok": True,
        "countryCode": "US",
        "serverTimeMs": now,
        "worldTimeMs": now % world.WORLD_DAY_LENGTH_MS,
        "worldDayLengthMs": world.WORLD_DAY_LENGTH_MS,
        "worldConnections": 64,
        "worldMessagesPerSecond": 4,
        "chatConnections": 128,
    }
    assert headers.read == ["cf-ipcountry"]
    assert response["cache_control"] == "no-store, max-age=0, must-revalidate"
    assert response["headers"]["x-content-type-options"] == "nosniff"
    serialized = repr(response["data"]).lower()
    for forbidden in ("ip", "user-agent", "useragent", "latitude", "longitude",
                      "repo", "wallet", "url"):
        assert forbidden not in serialized


def test_context_rejects_non_get_without_reading_headers():
    headers = _Headers({}, allowed=set())
    request = SimpleNamespace(method="POST", headers=headers)
    response = _load_context_handler()(request)
    assert response["status"] == 405
    assert response["headers"]["allow"] == "GET"
    assert headers.read == []


def test_context_prefers_trusted_request_cf_country_without_header_read():
    headers = _Headers(
        {"cf-ipcountry": "GB"},
        allowed=set(),
    )
    request = SimpleNamespace(
        method="GET",
        headers=headers,
        cf=SimpleNamespace(country="JP"),
    )
    response = _load_context_handler()(request)
    assert response["data"]["countryCode"] == "JP"
    assert headers.read == []


def test_country_code_is_coarse_and_rejects_cloudflare_sentinels():
    assert world.approximate_country_code("ca") == "CA"
    assert world.approximate_country_code(" EU ") == "EU"
    for raw in ("", "XX", "T1", "USA", "1A", "<script>"):
        assert world.approximate_country_code(raw) == ""


def test_presence_message_discards_sensitive_and_unknown_fields():
    current = world.default_presence("abcd1234", 1000)
    payload = {
        "type": "presence",
        "name": "<b>Alice</b>",
        "browser": "firefox",
        "os": "linux",
        "status": "available",
        "localTime": "09:32",
        "activityCategory": "viewing-repository",
        "publicDoor": "knock",
        "shareCountry": True,
        # All of these must be ignored rather than reflected.
        "id": "attacker-picked",
        "countryCode": "GB",
        "ip": "203.0.113.10",
        "userAgent": "private-browser-build",
        "repo": "secret-owner/private-repo",
        "wallet": "private-wallet",
        "url": "https://example.test/private?q=secret",
        "formContents": "secret",
    }
    kind, updated = world.sanitize_message(
        payload, current, 2000, country_source="US")
    public = world.public_presence(updated)

    assert kind == "presence"
    assert set(public) == set(world.WORLD_PUBLIC_FIELDS)
    assert public["id"] == "abcd1234"
    assert public["countryCode"] == "US"
    assert "<" not in public["name"] and ">" not in public["name"]
    assert public["browser"] == "firefox"
    assert public["os"] == "linux"
    assert public["status"] == "available"
    assert public["localTime"] == "09:32"
    assert public["activityCategory"] == "viewing-repository"
    assert public["publicDoor"] == "knock"
    rendered = repr(public)
    for secret in (
        "203.0.113.10", "private-browser-build",
        "secret-owner/private-repo", "private-wallet",
        "https://example.test/private?q=secret",
    ):
        assert secret not in rendered


def test_movement_is_bounded_quantized_and_profile_fields_cannot_change():
    current = world.default_presence("peer", 1000)
    current["countryCode"] = "DE"
    kind, updated = world.sanitize_message({
        "type": "move",
        "x": 999999,
        "y": -999999,
        "z": 12.34567,
        "yaw": 99,
        "moving": True,
        "name": "not accepted in movement",
        "countryCode": "FR",
        "wallet": "never-relay-me",
    }, current, 2000)

    assert kind == "move"
    assert updated["x"] == world.WORLD_COORD_LIMIT
    assert updated["y"] == -world.WORLD_COORD_LIMIT
    assert updated["z"] == 12.35
    assert updated["yaw"] == 3.142
    assert updated["moving"] is True
    assert updated["name"] == current["name"]
    assert updated["countryCode"] == "DE"
    delta = world.movement_delta(updated)
    assert set(delta) == {
        "type", "id", "x", "y", "z", "yaw", "moving", "updatedAt",
    }
    assert "never-relay-me" not in repr(delta)


def test_country_privacy_toggle_hides_and_restores_only_edge_country():
    current = world.default_presence("peer", 1000)
    assert current["countryCode"] == ""
    _, hidden = world.sanitize_message({
        "type": "presence",
        "shareCountry": False,
        "countryCode": "US",
    }, current, 2000, country_source="NZ")
    assert hidden["countryCode"] == ""

    _, shown = world.sanitize_message({
        "type": "presence",
        "shareCountry": True,
        "countryCode": "US",
    }, hidden, 3000, country_source="NZ")
    assert shown["countryCode"] == "NZ"


def test_local_time_is_optional_bounded_and_never_inferred_by_server():
    current = world.default_presence("peer", 1000)
    assert current["localTime"] == ""

    _, shared = world.sanitize_message({
        "type": "presence",
        "localTime": "23:59",
    }, current, 2000)
    assert shared["localTime"] == "23:59"

    for invalid in ("9:32 AM", "24:00", "12:60", "09:32 America/New_York",
                    "<script>", None):
        _, rejected = world.sanitize_message({
            "type": "presence",
            "localTime": invalid,
        }, shared, 3000)
        assert rejected["localTime"] == ""


def test_activity_is_generalized_allowlisted_and_never_accepts_urls():
    current = world.default_presence("peer", 1000)
    assert current["activityCategory"] == "hidden"
    _, shared = world.sanitize_message({
        "type": "presence",
        "activityCategory": "browsing-code-visualization",
    }, current, 2000)
    assert shared["activityCategory"] == "browsing-code-visualization"
    for invalid in (
        "viewing /private/repository",
        "https://example.test/private?q=secret",
        "searching for wallet seed",
    ):
        _, hidden = world.sanitize_message({
            "type": "presence",
            "activityCategory": invalid,
        }, shared, 3000)
        assert hidden["activityCategory"] == "hidden"


def test_emoji_status_is_one_bounded_pictograph_and_one_bounded_word():
    for emoji in (
        "😀",
        "🧑🏽‍💻",
        "👨‍👩‍👧‍👦",
        "🏳️‍🌈",
        "🇺🇸",
        "1️⃣",
        "❤️",
    ):
        assert world.clean_status_emoji(emoji) == emoji
    for invalid in (
        "",
        "hello",
        "😀😀",
        "<script>",
        "https://example.test/",
        "🇺",
        "\u200d",
        "A😀",
        "😀 private",
    ):
        assert world.clean_status_emoji(invalid) == ""

    for note in ("coding", "on-call", "débogage", "コード", "l’équipe"):
        assert world.clean_status_note(note) == note
    for invalid in (
        "two words",
        "https://example.test",
        "<script>",
        "/private",
        "\u202esecret",
        "x" * (world.WORLD_STATUS_NOTE_MAX + 1),
    ):
        assert world.clean_status_note(invalid) == ""

    current = world.default_presence("peer", 1000)
    assert current["statusEmoji"] == ""
    assert current["statusNote"] == ""
    kind, shared = world.sanitize_message({
        "type": "presence",
        "statusEmoji": "🧑🏽‍💻",
        "statusNote": "coding",
        "statusDetail": "private repository URL",
    }, current, 2000)
    assert kind == "presence"
    public = world.public_presence(shared)
    assert public["statusEmoji"] == "🧑🏽‍💻"
    assert public["statusNote"] == "coding"
    assert "statusDetail" not in public
    assert "private repository URL" not in repr(public)

    _, rejected = world.sanitize_message({
        "type": "presence",
        "statusEmoji": "😀😀",
        "statusNote": "two words",
    }, shared, 3000)
    assert rejected["statusEmoji"] == ""
    assert rejected["statusNote"] == ""

    _, restored = world.sanitize_message({
        "type": "presence",
        "statusEmoji": "🚀",
        "statusNote": "shipping",
    }, rejected, 4000)
    _, moved = world.sanitize_message({
        "type": "move",
        "x": 3,
        "statusEmoji": "🔐",
        "statusNote": "secret",
    }, restored, 5000)
    assert moved["statusEmoji"] == "🚀"
    assert moved["statusNote"] == "shipping"


def test_coarse_activity_metadata_is_bounded_and_privacy_gated():
    assert world.WORLD_FIRST_VISIT_AGE_VALUES == {
        "this-session", "today", "this-week", "this-month", "this-year",
        "over-a-year", "hidden",
    }
    current = world.default_presence("peer", 1000)
    assert current["inputActive"] is False
    assert current["visitCount"] == 0
    assert current["firstVisitAge"] == "hidden"

    _, shared = world.sanitize_message({
        "type": "presence",
        "activityCategory": "viewing-repository",
        "inputActive": True,
        "visitCount": 27,
        "firstVisitAge": "this-month",
        "url": "https://example.test/private?q=secret",
    }, current, 2000)
    assert shared["inputActive"] is True
    assert shared["visitCount"] == 27
    assert shared["firstVisitAge"] == "this-month"
    assert "url" not in world.public_presence(shared)
    assert "example.test" not in repr(world.public_presence(shared))

    _, bounded = world.sanitize_message({
        "type": "presence",
        "activityCategory": "exploring-town-square",
        "visitCount": 5000,
        "firstVisitAge": "over-a-year",
    }, current, 3000)
    assert bounded["visitCount"] == 999
    assert bounded["firstVisitAge"] == "over-a-year"
    _, lower_bounded = world.sanitize_message({
        "type": "presence",
        "activityCategory": "exploring-town-square",
        "visitCount": -1,
    }, current, 3500)
    assert lower_bounded["visitCount"] == 0

    _, rejected = world.sanitize_message({
        "type": "presence",
        "activityCategory": "exploring-town-square",
        "inputActive": "true",
        "visitCount": "999",
        "firstVisitAge": "https://example.test/history",
    }, current, 4000)
    assert rejected["inputActive"] is False
    assert rejected["visitCount"] == 0
    assert rejected["firstVisitAge"] == "hidden"

    _, hidden = world.sanitize_message({
        "type": "presence",
        "activityCategory": "hidden",
        "inputActive": True,
        "visitCount": 42,
        "firstVisitAge": "today",
    }, shared, 5000)
    assert hidden["inputActive"] is False
    assert hidden["visitCount"] == 0
    assert hidden["firstVisitAge"] == "hidden"


def test_arrival_slots_fill_unique_forward_facing_rows_of_ten():
    positions = [world.arrival_position(slot) for slot in range(21)]
    assert len({(item["x"], item["z"]) for item in positions}) == 21
    assert [item["z"] for item in positions[:10]] == [30.0] * 10
    assert [item["z"] for item in positions[10:20]] == [27.9] * 10
    assert positions[20]["z"] == 25.8
    assert all(item["yaw"] == 0.0 for item in positions)
    assert all(item["y"] == 0.38 for item in positions)
    assert world.first_available_arrival_slot([0, 2, 3]) == 1
    assert world.first_available_arrival_slot(range(63)) == 63


def test_arrival_cells_occupied_by_standing_visitors_read_as_used_slots():
    # Exact cell centres and near-misses within the clearance both block.
    assert world.arrival_slot_near_position(-8.1, 30.0) == 0
    assert world.arrival_slot_near_position(-6.3, 30.0) == 1
    assert world.arrival_slot_near_position(-7.6, 29.4) == 0
    assert world.arrival_slot_near_position(-8.1, 27.9) == 10
    # Positions away from the grid, and junk, never reserve anything.
    assert world.arrival_slot_near_position(0.0, 10.0) == -1
    assert world.arrival_slot_near_position(-8.1, 30.0 - 1.0) == -1
    assert world.arrival_slot_near_position("junk", None) == -1
    assert world.arrival_slot_near_position(float("nan"), 30.0) == -1
    assert world.arrival_slot_near_position(float("inf"), 30.0) == -1
    # A visitor standing on cell 0 with an unrelated reservation still keeps
    # a newcomer off cell 0.
    assert world.first_available_arrival_slot(
        [7, world.arrival_slot_near_position(-8.1, 30.0)]) == 1


def _world_fetch_runtime(now=50_000):
    class DurableObject:
        pass

    class _FetchSocket:
        def __init__(self):
            self.attachment = None
            self.sent = []

        def serializeAttachment(self, attachment):
            self.attachment = SimpleNamespace(**attachment)

        def deserializeAttachment(self):
            return self.attachment

        def send(self, message):
            self.sent.append(json.loads(message))

        def close(self, code=1000, reason=""):
            pass

    class _Ctx:
        def __init__(self):
            self.sockets = []

        def acceptWebSocket(self, ws, tags=None):
            self.sockets.append(ws)

        def getWebSockets(self, tag=None):
            return list(self.sockets)

    class _Pair:
        @staticmethod
        def new():
            client, server = _FetchSocket(), _FetchSocket()
            return SimpleNamespace(object_values=lambda: (client, server))

    clock = {"now": now}
    counter = {"n": 0}

    def peer_id():
        counter["n"] += 1
        return "peer%04d" % counter["n"]

    namespace = {
        "DurableObject": DurableObject,
        "Date": SimpleNamespace(now=lambda: clock["now"]),
        "world_protocol": world,
        "to_js": lambda value: value,
        "json": json,
        "re": re,
        "urlparse": urlparse,
        "new_world_peer_id": peer_id,
        "json_response": lambda data, status=200, **_kwargs: SimpleNamespace(
            status=status, data=data),
        "WebSocketPair": _Pair,
        "JsResponse": SimpleNamespace(
            new=lambda *args, **kwargs: SimpleNamespace(status=101)),
    }
    for name in ("_ws_attachment", "_ws_attr", "ForkMeshWorld"):
        node = _top_level_node(name)
        module = ast.fix_missing_locations(
            ast.Module(body=[node], type_ignores=[]))
        exec(compile(module, str(ENTRY), "exec"), namespace)
    instance = namespace["ForkMeshWorld"]()
    instance.ctx = _Ctx()
    return instance, clock


class _FetchRequest:
    url = "https://forkmesh.example/api/world/ws"

    class headers:
        @staticmethod
        def get(name):
            return "websocket" if str(name).lower() == "upgrade" else None


def test_connects_land_in_open_grid_cells_never_on_a_standing_visitor():
    instance, clock = _world_fetch_runtime()
    welcomes = []
    for _ in range(3):
        clock["now"] += 15_000
        asyncio.run(instance.fetch(_FetchRequest()))
        welcomes.append(instance.ctx.sockets[-1].sent[0])
    # Sequential fresh joins fill the arrival row without overlap.
    assert [(w["self"]["x"], w["self"]["z"]) for w in welcomes] == [
        (-8.1, 30.0), (-6.3, 30.0), (-4.5, 30.0)]

    # A visitor whose reconnect reserved an unrelated slot while they kept
    # standing on cell 0 still blocks cell 0 for the next newcomer.
    instance.ctx.sockets[0].attachment.arrival_slot = 9
    clock["now"] += 15_000
    asyncio.run(instance.fetch(_FetchRequest()))
    newcomer = instance.ctx.sockets[-1].sent[0]
    assert (newcomer["self"]["x"], newcomer["self"]["z"]) == (-2.7, 30.0)


def test_public_door_state_is_explicit_and_allowlisted():
    current = world.default_presence("peer", 1000)
    assert current["publicDoor"] == "closed"
    for value in ("closed", "knock", "open"):
        _, updated = world.sanitize_message({
            "type": "presence",
            "publicDoor": value,
        }, current, 2000)
        assert updated["publicDoor"] == value
    _, rejected = world.sanitize_message({
        "type": "presence",
        "publicDoor": "bypass-permissions",
    }, current, 2000)
    assert rejected["publicDoor"] == "closed"


def test_knock_interactions_are_targeted_text_free_and_not_impersonable():
    sender = world.default_presence("peer_sender", 1000)
    assert world.sanitize_interaction({
        "type": "interaction",
        "kind": "knock",
        "target": "peer_target",
        "from": "impersonated",
        "text": "private message",
        "url": "https://example.test/private",
    }, sender) == {
        "type": "interaction",
        "kind": "knock",
        "target": "peer_target",
    }
    for invalid in (
        {"type": "interaction", "kind": "chat", "target": "peer_target"},
        {"type": "interaction", "kind": "knock", "target": "peer_sender"},
        {"type": "interaction", "kind": "knock", "target": "../private"},
        {"type": "interaction", "kind": "knock", "target": ""},
    ):
        assert world.sanitize_interaction(invalid, sender) is None


def test_home_grants_and_declines_are_targeted_text_free_consent_frames():
    sender = world.default_presence("peer_owner", 1000)
    for kind in ("home-grant", "home-decline"):
        assert world.sanitize_interaction({
            "type": "interaction",
            "kind": kind,
            "target": "peer_visitor",
            "room": "private-name",
            "text": "ignored",
        }, sender) == {
            "type": "interaction",
            "kind": kind,
            "target": "peer_visitor",
        }
    assert '"pending_knocks": pending_knocks' in ENTRY_TEXT
    assert 'interaction["target"] not in pending' in ENTRY_TEXT
    assert '"kind": interaction["kind"]' in ENTRY_TEXT


def test_emotes_are_broadcast_as_a_tiny_fixed_vocabulary():
    sender = world.default_presence("peer_sender", 1000)
    for emote in ("wave", "idea", "celebrate"):
        assert world.sanitize_interaction({
            "type": "interaction",
            "kind": "emote",
            "emote": emote,
            "text": "must be discarded",
        }, sender) == {
            "type": "interaction",
            "kind": "emote",
            "emote": emote,
        }
    assert world.sanitize_interaction({
        "type": "interaction",
        "kind": "emote",
        "emote": "<script>",
    }, sender) is None


def test_name_and_identity_badge_fields_are_privacy_controlled_not_claims():
    current = world.default_presence("peer1234", 1000)
    assert current["name"] == "Guest peer"
    assert current["status"] == "hidden"

    _, named = world.sanitize_message({
        "type": "presence",
        "name": "Alice",
        "browser": "firefox",
        "os": "linux",
        "status": "available",
        # Guests cannot assert privileged identity or account claims.
        "accountStatus": "organization_admin",
        "verified": True,
        "paid": True,
        "email": "alice@example.test",
    }, current, 2000)
    assert named["name"] == "Alice"
    assert named["status"] == "available"
    assert named["accountStatus"] == "Guest"
    assert named["nodeCount"] == 0

    _, private = world.sanitize_message({
        "type": "presence",
        "shareName": False,
        "browser": "hidden",
        "os": "hidden",
        "status": "hidden",
    }, named, 3000)
    assert private["name"] == "Guest peer"
    assert private["browser"] == "hidden"
    assert private["os"] == "hidden"
    assert private["status"] == "hidden"
    assert "accountStatus" in world.WORLD_PUBLIC_FIELDS
    assert "nodeCount" in world.WORLD_PUBLIC_FIELDS
    for claim in ("verified", "paid", "email"):
        assert claim not in world.WORLD_PUBLIC_FIELDS
        assert claim not in world.public_presence(private)

    trusted = world.trusted_presence_claim(
        "Alice", "Organization admin", 9)
    assert trusted == {
        "name": "Alice",
        "accountStatus": "Organization admin",
        "nodeCount": world.WORLD_NODE_BADGE_MAX,
    }
    current["accountStatus"] = trusted["accountStatus"]
    _, authenticated = world.sanitize_message(
        {
            "type": "presence",
            "name": "Mallory",
            "shareName": True,
            "shareNodes": True,
            "accountStatus": "Verified bot",
            "nodeCount": 99,
            "space": "sky-campus",
        },
        current,
        4000,
        trusted_name=trusted["name"],
        trusted_node_count=trusted["nodeCount"],
    )
    assert authenticated["name"] == "Alice"
    assert authenticated["accountStatus"] == "Organization admin"
    assert authenticated["nodeCount"] == world.WORLD_NODE_BADGE_MAX
    assert authenticated["space"] == "sky-campus"


def test_only_presence_movement_and_heartbeat_frames_are_accepted():
    current = world.default_presence("peer", 1000)
    assert world.sanitize_message({"type": "chat", "text": "secret"},
                                  current, 2000) is None
    assert world.sanitize_message({"type": "repository", "path": "private"},
                                  current, 2000) is None
    assert world.sanitize_message("not-an-object", current, 2000) is None
    kind, unchanged = world.sanitize_message({"type": "ping"}, current, 2000)
    assert kind == "ping"
    assert unchanged == current


def test_rate_window_and_stale_cleanup_are_bounded():
    assert (
        world.WORLD_RATE_MAX_PER_WINDOW
        < world.WORLD_RATE_HARD_MAX_PER_WINDOW
        <= 16
    )
    start = 10_000
    count = 0
    for _ in range(world.WORLD_RATE_MAX_PER_WINDOW):
        allowed, start, count = world.advance_rate_window(start, count, 10_500)
        assert allowed is True
    allowed, start, count = world.advance_rate_window(start, count, 10_500)
    assert allowed is False

    allowed, new_start, new_count = world.advance_rate_window(
        start, count, start + world.WORLD_RATE_WINDOW_MS)
    assert (allowed, new_start, new_count) == (
        True, start + world.WORLD_RATE_WINDOW_MS, 1)

    broadcast_start = 20_000
    broadcast_count = 0
    for _ in range(world.WORLD_BROADCAST_MAX_PER_WINDOW):
        allowed, broadcast_start, broadcast_count = (
            world.advance_broadcast_window(
                broadcast_start, broadcast_count, 20_500))
        assert allowed is True
    assert world.advance_broadcast_window(
        broadcast_start, broadcast_count, 20_500)[0] is False

    connect_start = 30_000
    connect_count = 0
    for _ in range(world.WORLD_CONNECT_MAX_PER_WINDOW):
        allowed, connect_start, connect_count = (
            world.advance_connection_window(
                connect_start, connect_count, 30_500))
        assert allowed is True
    assert world.advance_connection_window(
        connect_start, connect_count, 30_500)[0] is False
    assert world.WORLD_MAX_CONNECTIONS <= 64

    now = 1_000_000
    assert world.presence_is_stale(
        now - world.WORLD_CLIENT_STALE_MS, now) is False
    assert world.presence_is_stale(
        now - world.WORLD_CLIENT_STALE_MS - 1, now) is True
    assert world.presence_is_stale(0, now) is True


class _WorldSocket:
    def __init__(self, state, now, rate_count=0):
        self.attachment = SimpleNamespace(
            **world.public_presence(state),
            country_source="",
            trusted_name="",
            trusted_node_count=0,
            is_admin=False,
            ip_token="",
            agent_token="",
            pending_knocks=[],
            arrival_slot=0,
            last=now,
            rl_start=now,
            rl_count=rate_count,
            departed=False,
        )
        self.sent = []
        self.closed = []

    def serializeAttachment(self, attachment):
        self.attachment = SimpleNamespace(**attachment)

    def send(self, message):
        self.sent.append(message)

    def close(self, code, reason):
        self.closed.append((code, reason))


def _world_socket_runtime(now=50_000):
    class DurableObject:
        pass

    def ws_attachment(socket):
        return socket.attachment

    def ws_attr(socket, key, default=None):
        return getattr(socket.attachment, key, default)

    namespace = {
        "DurableObject": DurableObject,
        "Date": SimpleNamespace(now=lambda: now),
        "world_protocol": world,
        "_ws_attachment": ws_attachment,
        "_ws_attr": ws_attr,
        "to_js": lambda value: value,
        "json": json,
        "re": re,
    }
    node = _top_level_node("ForkMeshWorld")
    module = ast.fix_missing_locations(
        ast.Module(body=[node], type_ignores=[]))
    exec(compile(module, str(ENTRY), "exec"), namespace)
    instance = namespace["ForkMeshWorld"]()
    broadcasts = []
    instance._live_sockets = lambda cleanup=False: []
    instance._broadcast = (
        lambda frame, **_kwargs: broadcasts.append(frame)
    )
    return instance, broadcasts


def _world_socket_message(instance, socket, payload):
    asyncio.run(instance.webSocketMessage(
        socket, json.dumps(payload, separators=(",", ":"))))


def test_legitimate_join_presence_and_movement_burst_is_not_disconnected():
    now = 50_000
    instance, broadcasts = _world_socket_runtime(now)
    socket = _WorldSocket(world.default_presence("peer", now), now)
    frames = [
        {"type": "presence", "status": "available"},
        {"type": "move", "x": 1, "z": 1, "moving": False},
        {"type": "presence", "inputActive": True},
        {"type": "move", "x": 2, "z": 2, "moving": True},
        {"type": "move", "x": 3, "z": 3, "moving": True},
    ]
    for frame in frames:
        _world_socket_message(instance, socket, frame)

    assert socket.closed == []
    assert socket.attachment.rl_count == 5
    assert socket.attachment.x == 2
    assert socket.attachment.z == 2
    assert len(broadcasts) == world.WORLD_RATE_MAX_PER_WINDOW


def test_isolated_excess_movement_and_presence_are_dropped_without_disconnect():
    now = 50_000
    cases = (
        (
            {"type": "move", "x": 40, "z": 50, "moving": True},
            {"x": 4.0, "z": 5.0},
        ),
        (
            {"type": "presence", "status": "away"},
            {"status": "hidden"},
        ),
    )
    for payload, expected in cases:
        state = world.default_presence("peer", now)
        state.update({"x": 4.0, "z": 5.0})
        instance, broadcasts = _world_socket_runtime(now)
        socket = _WorldSocket(
            state, now, rate_count=world.WORLD_RATE_MAX_PER_WINDOW)

        _world_socket_message(instance, socket, payload)

        assert socket.closed == []
        assert socket.attachment.rl_count == (
            world.WORLD_RATE_MAX_PER_WINDOW + 1)
        for field, value in expected.items():
            assert getattr(socket.attachment, field) == value
        assert broadcasts == []


def test_sustained_disposable_frame_flood_still_closes_socket():
    now = 50_000
    instance, broadcasts = _world_socket_runtime(now)
    socket = _WorldSocket(
        world.default_presence("peer", now),
        now,
        rate_count=world.WORLD_RATE_MAX_PER_WINDOW,
    )
    excess_frames = (
        world.WORLD_RATE_HARD_MAX_PER_WINDOW
        - world.WORLD_RATE_MAX_PER_WINDOW
        + 1
    )
    for index in range(excess_frames):
        _world_socket_message(instance, socket, {
            "type": "move", "x": index, "moving": True,
        })
        if socket.closed:
            break

    assert socket.attachment.rl_count == (
        world.WORLD_RATE_HARD_MAX_PER_WINDOW + 1)
    assert socket.closed == [(1008, "sustained rate limit")]
    assert broadcasts == []


def test_world_durable_object_is_transient_and_hibernating():
    source = ast.unparse(_top_level_node("ForkMeshWorld"))
    assert "acceptWebSocket" in source
    assert "getWebSockets" in source
    assert "serializeAttachment" in source
    assert "WORLD_MESSAGE_MAX_BYTES" in source
    assert "advance_rate_window" in source
    assert "advance_connection_window" in source
    assert "advance_broadcast_window" in source
    assert "presence_is_stale" in source
    assert "sanitize_interaction" in source
    assert "_ws_attr(peer, 'publicDoor', 'closed') == 'knock'" in source
    assert "'from': state.get('id')" in source
    assert "webSocketError" in source
    assert "_live_sockets(cleanup=True)" in source
    # No persistence/history subsystem is reachable from this class.
    for forbidden in (
        ".storage", "d1_", "ensure_schema", "chat_history",
        "account_row", "/api/repo/", "wallet",
    ):
        assert forbidden not in source


def test_world_internal_upgrade_does_not_forward_identifying_headers():
    source = _function_without_docstring("world_durable_object_request").lower()
    assert "world_request_country" in source
    assert "x-forkmesh-country" in source
    assert "source_url.path" in source
    assert "sec-websocket-protocol" not in source
    for forbidden in (
        "cf-connecting-ip", "x-forwarded-for", "user-agent",
        "authorization", "cookie", "referer",
    ):
        assert forbidden not in source


def test_browser_world_socket_is_same_origin_only():
    node = _top_level_node("world_websocket_origin_allowed")
    module = ast.fix_missing_locations(ast.Module(body=[node], type_ignores=[]))
    namespace = {"urlparse": urlparse}
    exec(compile(module, str(ENTRY), "exec"), namespace)
    allowed = namespace["world_websocket_origin_allowed"]

    same = SimpleNamespace(
        url="https://forkmesh.test/api/world/ws",
        headers=_Headers(
            {"origin": "https://forkmesh.test"}, allowed={"origin"}))
    foreign = SimpleNamespace(
        url="https://forkmesh.test/api/world/ws",
        headers=_Headers(
            {"origin": "https://evil.example"}, allowed={"origin"}))
    missing = SimpleNamespace(
        url="https://forkmesh.test/api/world/ws",
        headers=_Headers({}, allowed={"origin"}))
    wrong_scheme = SimpleNamespace(
        url="https://forkmesh.test/api/world/ws",
        headers=_Headers(
            {"origin": "http://forkmesh.test"}, allowed={"origin"}))
    default_port = SimpleNamespace(
        url="https://forkmesh.test/api/world/ws",
        headers=_Headers(
            {"origin": "https://forkmesh.test:443"}, allowed={"origin"}))
    wrong_port = SimpleNamespace(
        url="https://forkmesh.test/api/world/ws",
        headers=_Headers(
            {"origin": "https://forkmesh.test:444"}, allowed={"origin"}))
    origin_with_path = SimpleNamespace(
        url="https://forkmesh.test/api/world/ws",
        headers=_Headers(
            {"origin": "https://forkmesh.test/private"},
            allowed={"origin"}))
    assert allowed(same) is True
    assert allowed(foreign) is False
    assert allowed(missing) is False
    assert allowed(wrong_scheme) is False
    assert allowed(default_port) is True
    assert allowed(wrong_port) is False
    assert allowed(origin_with_path) is False


def test_world_route_binding_and_migration_are_registered():
    route_source = ast.unparse(_top_level_node("Default"))
    assert "/api/world/context" in route_source
    assert "/api/world/ws" in route_source
    assert "town-square-v1" in route_source
    assert "world_websocket_origin_allowed" in route_source
    assert "world_durable_object_request" in route_source

    config = tomllib.loads(WRANGLER.read_text(encoding="utf-8"))
    bindings = {
        item["name"]: item["class_name"]
        for item in config["durable_objects"]["bindings"]
    }
    # Multiplayer chat and world presence remain; the repository transport DO
    # is deleted and must never return as a production binding.
    assert bindings == {
        "FORKMESH_MAINNODE_ROOM": "ForkMeshRoom",
        "FORKMESH_WORLD": "ForkMeshWorld",
    }
    dev_bindings = {
        item["name"]: item["class_name"]
        for item in config["env"]["dev"]["durable_objects"]["bindings"]
    }
    assert dev_bindings == {
        "FORKMESH_MAINNODE_ROOM": "ForkMeshRoom",
        "FORKMESH_WORLD": "ForkMeshWorld",
    }
    migrations = {item["tag"]: item for item in config["migrations"]}
    assert migrations["v9"]["new_sqlite_classes"] == ["ForkMeshWorld"]
    assert migrations["v10"]["deleted_classes"] == ["ForkMeshHost"]


def test_world_static_route_is_reserved_and_asset_first():
    redirects = REDIRECTS.read_text(encoding="utf-8").splitlines()
    assert "/world /world/index.html 200" in redirects
    assert "/world/ /world 308" in redirects

    static_source = STATIC_ROUTES.read_text(encoding="utf-8")
    assert '"world",' in static_source
    config = tomllib.loads(WRANGLER.read_text(encoding="utf-8"))
    assert "!/world/*" in config["assets"]["run_worker_first"]


def test_world_peer_id_contains_no_timestamp_or_account_material():
    source = ast.unparse(_top_level_node("new_world_peer_id"))
    assert "getRandomValues" in source
    assert "Date.now" not in source
    assert "account" not in source.lower()
    assert "node" not in source.lower()


def test_consent_only_inactivity_records_are_bounded_and_generalized():
    assert world.sanitize_inactivity_record({
        "status": "inactive",
        "shareInactivity": True,
        "shareName": True,
        "shareNodes": False,
        "url": "https://private.example/path",
        "search": "secret",
    }) == {
        "status": "inactive",
        "shareName": True,
        "shareNodes": False,
    }
    assert world.sanitize_inactivity_record({
        "status": "inactive",
        "shareInactivity": False,
    }) is None
    assert world.sanitize_inactivity_record({
        "status": "precise browsing history",
        "shareInactivity": True,
    }) is None


def test_world_ticket_and_inactive_routes_keep_auth_out_of_the_socket():
    route_source = ast.unparse(_top_level_node("Default"))
    assert "/api/world/ticket" in route_source
    assert "/api/world/inactive" in route_source
    assert "_world_ticket_decode" in route_source

    bridge = _function_without_docstring("world_durable_object_request").lower()
    assert "x-forkmesh-world-claim" in bridge
    assert "trusted_presence_claim" in bridge
    assert "source_url.path" in bridge
    assert "source_url.query" not in bridge
    for forbidden in (
        "authorization", "cookie", "sessiontoken", "cf-connecting-ip",
        "x-forwarded-for", "user-agent",
    ):
        assert forbidden not in bridge

    handler = ast.unparse(_top_level_node("world_inactive_handler"))
    assert "sanitize_inactivity_record" in handler
    assert "WORLD_INACTIVE_RETAIN_MS" in handler
    assert "WORLD_INACTIVE_DELAY_MS" in handler
    assert "Private contributor" in handler
    assert "updated_at<=?" in handler
    assert "visibleAfter" in handler
    assert "world_request_country" not in handler
    assert "request.url" not in handler

    ticket = ast.unparse(_top_level_node("world_ticket_handler"))
    assert "UPDATE world_inactive_presence SET updated_at" in ticket

    claims = ast.unparse(_top_level_node("_world_account_claim"))
    assert "revoked_at=0" in claims
    assert "expires_at=0 OR expires_at>?" in claims

    schema = SCHEMA.read_text(encoding="utf-8")
    migration = INACTIVE_MIGRATION.read_text(encoding="utf-8")
    for source in (schema, migration):
        assert "world_inactive_presence" in source
        assert "expires_at" in source
