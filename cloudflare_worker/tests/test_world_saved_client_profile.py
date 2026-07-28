#!/usr/bin/env python3
"""The coarse client profile an account keeps while it is away.

Every registered account owns a bench around the campfire whether or not it is
signed in, and an away member publishes no presence frame. /api/world/client
stores the same three coarse values a live frame carries — approximate
country, browser family, OS family — so the bench figure keeps the member's
flag shirt and client badge instead of sitting there blank.
"""

import ast
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WORLD_PATH = ROOT / "src" / "world.py"
APP = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")

spec = importlib.util.spec_from_file_location(
    "forkmesh_world_protocol_profile", WORLD_PATH)
world = importlib.util.module_from_spec(spec)
spec.loader.exec_module(world)


def _top_level_node(name):
    for node in ast.parse(ENTRY_TEXT).body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                             ast.ClassDef)) and node.name == name:
            return node
    raise AssertionError("%s not found in entry.py" % name)


def test_stored_profile_keeps_only_the_three_coarse_values():
    assert world.clean_client_profile("de", "firefox", "linux") == {
        "countryCode": "DE",
        "browser": "firefox",
        "os": "linux",
    }
    # Cloudflare's unknown/sentinel codes are not countries.
    assert world.clean_client_profile("XX", "chrome", "macos")[
        "countryCode"] == ""
    assert world.clean_client_profile("T1", "chrome", "macos")[
        "countryCode"] == ""


def test_hidden_and_unknown_client_values_store_as_nothing():
    # "hidden" is the privacy sentinel of a live presence frame: storing it
    # would leave a stale value behind after the toggle went off.
    hidden = world.clean_client_profile("", "hidden", "hidden")
    assert hidden == {"countryCode": "", "browser": "", "os": ""}
    # A raw user agent, a version, or any unrecognized family is dropped.
    junk = world.clean_client_profile(
        "", "Mozilla/5.0 (X11; Linux x86_64)", "windows 11")
    assert junk == {"countryCode": "", "browser": "", "os": ""}


def test_client_profile_endpoint_trusts_the_edge_for_country():
    handler = ast.unparse(_top_level_node("world_client_profile_handler"))
    # The country comes from request.cf, never from the posted body: the
    # client only says whether it may be shared.
    assert "world_request_country(request)" in handler
    assert "data.get('shareCountry') is True" in handler
    assert "clean_client_profile" in handler
    assert "data.get('country')" not in handler
    # Session-proven account only, and same-origin enforced by the shared
    # session helper rather than a self-asserted name in the body.
    assert "_account_session_record(env, request, data)" in handler
    assert "invalid_session" in handler
    assert "method_name(request) != 'POST'" in handler


def test_client_profile_writes_only_when_the_value_changed():
    handler = ast.unparse(_top_level_node("world_client_profile_handler"))
    # One write per change, not one per visit: the endpoint is on the World
    # bootstrap path for every signed-in visitor.
    assert "if profile != _account_world_client_fields(rec):" in handler
    body = handler.split(
        "if profile != _account_world_client_fields(rec):", 1)[1]
    assert "_save_account(env, name_bi, rec)" in body
    # The bench figures come from the edge-cached public directory.
    assert "edge_cache_delete(USERS_DIRECTORY_CACHE_KEY)" in body


def test_public_directory_carries_the_saved_client_profile():
    payload = ast.unparse(_top_level_node("_account_chat_user_payload"))
    assert "_account_world_client_fields(rec)" in payload
    assert "emailVerified" in payload
    fields = ast.unparse(_top_level_node("_account_world_client_fields"))
    # Read back through the same normalizer, so a record edited outside this
    # endpoint can still only publish the coarse allowlisted values.
    assert "clean_client_profile" in fields
    assert "world_country" in fields
    assert "world_browser" in fields
    assert "world_os" in fields


def test_client_profile_route_is_registered():
    routes = ast.unparse(_top_level_node("Default"))
    assert "/api/world/client" in routes
    assert "world_client_profile_handler" in routes


def test_world_client_saves_the_profile_once_per_change():
    sync = APP.split("async syncWorldClientProfile()", 1)[1].split(
        "\n  }\n", 1)[0]
    assert '"/api/world/client"' in sync
    assert "presenceBrowser(" in sync and "presenceOS(" in sync
    assert "shareCountry" in sync
    # Guests own no account record to save onto.
    assert 'this.identity.accountStatus === "Guest"' in sync
    # A fingerprint in localStorage keeps an ordinary reload from rewriting
    # the same value.
    assert "CLIENT_PROFILE_KEY" in sync
    assert "this.memberDirectoryFetchedAt = 0" in sync
    assert "refreshMemberDirectory(True)" in sync or "refreshMemberDirectory(true)" in sync
    # The privacy toggles all funnel through saveSettings, so hiding a value
    # clears the saved copy immediately.
    saved = APP.split("  saveSettings() {", 1)[1].split("\n  }\n", 1)[0]
    assert "syncWorldClientProfile()" in saved


def test_signed_out_visitors_keep_their_country_locally():
    assert 'const COUNTRY_KEY = "forkmesh.world.countryCode.v1";' in APP
    context = APP.split("async loadContext()", 1)[1].split(
        "this.identity.flag = flagEmoji", 1)[0]
    # A slow or unreachable /api/world/context must not drop the flag back to
    # "no country" for a visitor who has no account to read it from.
    assert "rememberCountryCode(country)" in context
    assert "rememberedCountryCode()" in context
    remember = APP.split("function rememberCountryCode(code)", 1)[1].split(
        "\n}\n", 1)[0]
    assert "/^[A-Z]{2}$/.test(clean)" in remember


def test_away_member_bench_figure_wears_the_saved_profile():
    lounge = SCENE.split("function updateMemberLounge", 1)[1].split(
        "campfire.userData.seatByName", 1)[0]
    assert "flagEmoji(memberCountry)" in lounge
    assert "countryCode: memberCountry," in lounge
    assert 'browser: String(member.browser || "Hidden"),' in lounge
    assert 'os: String(member.os || "Hidden"),' in lounge
    # The directory tokens are lowercase families; the avatar wants labels.
    directory = APP.split("function normalizeMemberDirectory", 1)[1].split(
        "\n}\n", 1)[0]
    assert 'browser: presenceLabel(user?.browser, "Hidden", "Hidden"),' in (
        directory)
    assert 'os: presenceLabel(user?.os, "Hidden", "Hidden"),' in directory
