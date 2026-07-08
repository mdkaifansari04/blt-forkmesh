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
DASHBOARD_HTML = (PUBLIC / "dashboard.html").read_text(encoding="utf-8")
PUBLIC_CHAT_HTML = (PUBLIC / "chat.html").read_text(encoding="utf-8")
STYLES = (PUBLIC / "styles.css").read_text(encoding="utf-8")


def test_send_marks_durable_frames_for_relay_retention():
    send = CHAT[CHAT.index("function send(") : CHAT.index("async function connect(")]
    # The durable frame gets the persist flag the relay checks before retaining.
    assert "envelope.persist = true" in send
    assert "DURABLE_TYPES.has(" in send


def test_public_chat_marks_durable_frames_for_relay_retention():
    send = PUBLIC_CHAT[
        PUBLIC_CHAT.index("function send("):PUBLIC_CHAT.index("function makeForkbotPlain(")
    ]
    assert "envelope.persist = true" in send
    assert "DURABLE_TYPES.has(" in send


def test_durable_type_set_matches_the_node():
    # Same set as the desktop node's kDurableTypes (ServerNode.cpp).
    for kind in ("chat", "edit", "delete", "reaction", "admin-delete"):
        assert f'"{kind}"' in CHAT[CHAT.index("DURABLE_TYPES") : CHAT.index("function send(")]
        assert f'"{kind}"' in PUBLIC_CHAT[
            PUBLIC_CHAT.index("DURABLE_TYPES"):PUBLIC_CHAT.index("function send(")
        ]


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
    assert '"forkmesh.dashboard.theme", "forkmesh.theme"' in PUBLIC_CHAT_HTML
    assert 'meta name="color-scheme" content="light dark"' in PUBLIC_CHAT_HTML
    assert "html:not(.dark)" in PUBLIC_CHAT_HTML
    assert 'theme === "light" ? "#f6f8fb" : "#090909"' in PUBLIC_CHAT_HTML


def test_web_chat_mentions_link_to_public_profiles_with_hover_cards():
    for source in (CHAT, PUBLIC_CHAT):
        assert "const CHAT_MENTION_RE" in source
        assert "function appendMentionText" in source
        assert "function renderMessageText" in source
        assert 'fetch("/api/accounts/" + encodeURIComponent(key)' in source
        assert 'anchor.href = mentionProfilePath(name)' in source
        assert "anchor.dataset.chatMention = name" in source
        assert 'mentionCardEl.className = "chat-mention-card"' in source
        assert "showMentionCard(anchor, name)" in source

    assert "renderMessageText(rec.body, plain.text || \"\")" in PUBLIC_CHAT
    assert "renderMessageText(rec.textEl, plain.text || \"\")" in CHAT
    assert "sideEntry.text = plain.text || \"\"" in CHAT


def test_chat_mention_styles_are_available_on_all_chat_surfaces():
    for source in (DASHBOARD_HTML, PUBLIC_CHAT_HTML, STYLES):
        assert ".chat-mention {" in source
        assert ".chat-mention-card {" in source
        assert ".chat-mention-card[hidden]" in source
