#!/usr/bin/env python3
"""The website chat shares the same retained history as the desktop nodes.

The relay only retains (and later replays to joining clients) a room frame whose
envelope carries `persist: true` — see ForkMeshRoom._maybe_retain. The desktop
node tags its durable frames via kDurableTypes (ServerNode::sendEncrypted). If
the website's dashboard chat omits the flag, a message typed on the site is
relayed live but never becomes part of the shared history, so nodes and later
web visitors never see it — leaving the website out of the shared chat. This
contract pins the website tagging the same durable set the node does.
"""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
CHAT = (PUBLIC / "dashboard-chat.js").read_text(encoding="utf-8")
PUBLIC_CHAT = (PUBLIC / "chat.js").read_text(encoding="utf-8")


def test_send_marks_durable_frames_for_relay_retention():
    send = CHAT[CHAT.index("function send(") : CHAT.index("async function connect(")]
    # The durable frame gets the persist flag the relay checks before retaining.
    assert "envelope.persist = true" in send
    assert "DURABLE_TYPES.has(" in send


def test_durable_type_set_matches_the_node():
    # Same set as the desktop node's kDurableTypes (ServerNode.cpp).
    for kind in ("chat", "edit", "delete", "reaction", "admin-delete"):
        assert f'"{kind}"' in CHAT[CHAT.index("DURABLE_TYPES") : CHAT.index("function send(")]


def test_dashboard_chat_requires_user_session_and_marks_user_frames():
    assert "function userSession()" in CHAT
    assert "function isUserLikeSession(session)" in CHAT
    assert 'session.kind === "user"' in CHAT
    assert 'session.kind === "node"' in CHAT
    assert "return Boolean(session.email)" in CHAT
    assert "async function hydrateUserSession()" in CHAT
    assert 'fetch("/api/accounts/" + encodeURIComponent(session.nodeName)' in CHAT
    assert 'accountKind: "user"' in CHAT
    assert 'plain.accountKind !== "user"' in CHAT
    assert 'entry.accountKind !== "user"' in CHAT


def test_public_chat_requires_user_session_and_marks_user_frames():
    assert "function userSession()" in PUBLIC_CHAT
    assert "function isUserLikeSession(session)" in PUBLIC_CHAT
    assert 'session.kind === "user"' in PUBLIC_CHAT
    assert 'session.kind === "node"' in PUBLIC_CHAT
    assert "return Boolean(session.email)" in PUBLIC_CHAT
    assert "async function hydrateUserSession()" in PUBLIC_CHAT
    assert 'fetch("/api/accounts/" + encodeURIComponent(session.nodeName)' in PUBLIC_CHAT
    assert 'accountKind: "user"' in PUBLIC_CHAT
    assert 'plain.accountKind !== "user"' in PUBLIC_CHAT
    assert 'entry.accountKind !== "user"' in PUBLIC_CHAT
    assert "lockChatForNonUser" in PUBLIC_CHAT


def test_public_chat_uses_shared_site_theme_keys():
    chat_html = (PUBLIC / "chat.html").read_text(encoding="utf-8")

    assert '"forkmesh.dashboard.theme", "forkmesh.theme"' in chat_html
    assert 'meta name="color-scheme" content="light dark"' in chat_html
    assert "html:not(.dark)" in chat_html
    assert 'theme === "light" ? "#f6f8fb" : "#090909"' in chat_html
