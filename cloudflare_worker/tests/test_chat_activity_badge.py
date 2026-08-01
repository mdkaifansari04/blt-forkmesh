#!/usr/bin/env python3
"""New signups reach chat instantly + site-wide chat icon with unread badge.

adhoc #208: a user who signed up on the website was invisible in the chat
people pane until they happened to speak in the room, and nothing outside
/chat hinted there was anyone to welcome. Contracts pinned here:

  * The Worker exposes a cheap public /api/chat/activity counter digest
    (retained #general message count + registered user count) that never
    decrypts anything, and edge-caches it.
  * The public user directory (/api/accounts/users) is edge-cached too — the
    chat page now polls it — and a successful signup invalidates both caches
    so the new account is visible on the very next poll. That cache is
    edge-only: the response the browser receives is no-store (adhoc #434).
  * chat.js seeds/refreshes its roster from the directory and announces a
    freshly-created account in the log so the room can welcome them.
  * site-header.js renders a chat icon with an unread badge on every page
    that mounts the shared header, keyed off the same localStorage "seen"
    baseline the chat page maintains.
"""

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
CHAT = (ROOT / "public" / "chat.js").read_text(encoding="utf-8")
HEADER_JS = (ROOT / "public" / "site-header.js").read_text(encoding="utf-8")
HEADER_CSS = (ROOT / "public" / "site-header.css").read_text(encoding="utf-8")
DASHBOARD_CHAT = (ROOT / "public" / "dashboard-chat.js").read_text(
    encoding="utf-8")


def _function_source(name: str) -> str:
    tree = ast.parse(ENTRY)
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and \
                node.name == name:
            return ast.get_source_segment(ENTRY, node)
    raise AssertionError(f"entry.py must define {name}")




def test_chat_activity_route_registered():
    assert '"/api/chat/activity"' in ENTRY
    assert "await chat_activity_handler(self.env, request)" in ENTRY


def test_chat_activity_counts_without_decrypting():
    source = _function_source("chat_activity_handler")

    assert "decrypt_row" not in source
    assert "COUNT(*)" in source
    assert "FROM chat_history" in source
    assert "FLAGSHIP_ROOM_KEY" in source


    assert "email_bi IS NOT NULL" in source


def test_chat_activity_is_edge_cached():
    source = _function_source("chat_activity_handler")
    assert "edge_cache_match(CHAT_ACTIVITY_CACHE_KEY)" in source
    assert "edge_cache_put(CHAT_ACTIVITY_CACHE_KEY" in source
    assert re.search(r"CHAT_ACTIVITY_CACHE_KEY\s*=", ENTRY)




def test_users_directory_is_edge_cached():
    source = _function_source("_account_users_directory")
    assert "_users_directory_cache_get()" in source
    assert "edge_cache_put(" in source
    assert "USERS_DIRECTORY_CACHE_KEY" in source


def test_users_directory_is_never_browser_cached():




    assert re.search(
        r'USERS_DIRECTORY_CLIENT_CACHE_CONTROL\s*=\s*"no-store,', ENTRY)
    hit = _function_source("_users_directory_cache_get")
    assert '"cache-control": USERS_DIRECTORY_CLIENT_CACHE_CONTROL' in hit
    source = _function_source("_account_users_directory")
    assert "cache_control=USERS_DIRECTORY_CLIENT_CACHE_CONTROL" in source


def test_signup_invalidates_chat_caches():
    source = _function_source("_account_signup")
    assert "edge_cache_delete(USERS_DIRECTORY_CACHE_KEY)" in source
    assert "edge_cache_delete(CHAT_ACTIVITY_CACHE_KEY)" in source




def test_chat_page_seeds_roster_from_directory():
    assert '"/api/accounts/users"' in CHAT
    assert "refreshUsersDirectory" in CHAT


    assert '"account:" + name' in CHAT


def test_chat_page_polls_and_announces_new_signups():
    assert "USERS_DIRECTORY_REFRESH_MS" in CHAT
    assert "just joined ForkMesh" in CHAT


    assert "NEW_USER_ANNOUNCE_WINDOW_MS" in CHAT


def test_chat_page_maintains_seen_baseline():
    assert '"/api/chat/activity"' in CHAT or "CHAT_ACTIVITY_ENDPOINT" in CHAT
    assert "forkmesh.chat.activitySeen" in CHAT




def test_header_renders_chat_icon_linking_to_chat():
    assert 'class="fm-header-chat" href="/chat"' in HEADER_JS
    assert "fm-header-chat-badge" in HEADER_JS


def test_header_badge_uses_activity_counters_and_shared_baseline():
    assert '"/api/chat/activity"' in HEADER_JS


    assert "forkmesh.chat.activitySeen" in HEADER_JS
    assert "renderChatBadge" in HEADER_JS


    assert "writeChatSeen(activity)" in HEADER_JS
    assert '"99+"' in HEADER_JS


def test_header_css_styles_icon_and_badge():
    assert ".fm-header-chat {" in HEADER_CSS
    assert ".fm-header-chat-badge {" in HEADER_CSS




def test_own_messages_advance_the_header_baseline():



    assert "function noteOwnChatActivity()" in CHAT
    assert "noteOwnChatActivity();" in CHAT



    assert (
        'if (envelope.persist && scope === "public-world-general" '
        "&& !plain.file) {"
    ) in CHAT

    assert "if (!raw) return;" in CHAT


    assert "function noteOwnChatActivity()" in DASHBOARD_CHAT
    assert 'const CHAT_ACTIVITY_SEEN_KEY = "forkmesh.chat.activitySeen";' in (
        DASHBOARD_CHAT
    )
    assert (
        "if (envelope.persist && PUBLIC_WORLD_GENERAL && !plain.file) {"
    ) in DASHBOARD_CHAT


def test_own_account_messages_never_bump_a_room_badge():



    assert "function isOwnChatMessage(record)" in CHAT
    assert "} else if (!isOwnChatMessage(record)) {" in CHAT
    assert 'String(userSession()?.nodeName || "").trim().toLowerCase()' in CHAT


    assert "record.senderId !== selfId" in CHAT
