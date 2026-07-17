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
DASHBOARD_HTML = (PUBLIC / "dashboard" / "chat" / "index.html").read_text(encoding="utf-8")
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


# --- /chat three-pane layout (rooms | conversation | people) -----------------

def test_public_chat_has_rooms_conversation_and_people_panes():
    assert 'id="chat-rooms"' in PUBLIC_CHAT_HTML
    assert 'id="chat-people"' in PUBLIC_CHAT_HTML
    assert 'id="chat-channel-title"' in PUBLIC_CHAT_HTML
    assert ".chat-rooms-pane" in PUBLIC_CHAT_HTML
    assert ".chat-people-pane" in PUBLIC_CHAT_HTML
    # Channels multiplex over the one room socket via each message's channel
    # field, exactly like the desktop; sends carry the active room.
    assert "channel: activeChannel" in PUBLIC_CHAT
    assert "function setActiveChannel(" in PUBLIC_CHAT
    assert 'DEFAULT_CHANNELS = ["#general", "#welcome", "#random"]' in PUBLIC_CHAT


def test_public_chat_sends_presence_keepalive_at_desktop_cadence():
    # The room DO reaps sockets with no frames for 3 minutes; the presence beat
    # (same 60s cadence as ServerNode's kPresenceIntervalMs) keeps the web
    # client connected AND keeps its roster entry fresh for peers. The old web
    # client sent nothing while idle and kept getting disconnected.
    assert "PRESENCE_INTERVAL_MS = 60000" in PUBLIC_CHAT
    assert "PEER_STALE_MS = 180000" in PUBLIC_CHAT  # ServerNode kPeerStaleMs
    assert 'send(makePlain("presence"))' in PUBLIC_CHAT
    assert "function scheduleReconnect(" in PUBLIC_CHAT


def test_public_chat_reactions_speak_the_desktop_protocol():
    # Same frame shape as ServerNode::sendReaction so toggles converge across
    # web and desktop clients, and reactions persist for late joiners (the
    # "reaction" kind is already in DURABLE_TYPES).
    reaction = PUBLIC_CHAT[
        PUBLIC_CHAT.index("function toggleReaction("):
        PUBLIC_CHAT.index("function ensureEmojiPicker(")
    ]
    assert 'makePlain("reaction"' in reaction
    assert "conversation:" in reaction
    assert "target: messageId" in reaction
    assert "reactorId: selfId" in reaction
    assert "reactorName: displayName()" in reaction
    assert "added: !mine" in reaction
    assert ".chat-reaction-chip" in PUBLIC_CHAT_HTML
    assert ".chat-emoji-picker" in PUBLIC_CHAT_HTML


def test_public_chat_people_pane_tracks_online_status_from_frame_ts():
    # Presence derives from each frame's own timestamp so replayed history
    # can't paint a days-old author as online, and an old frame never demotes
    # a peer heard from more recently.
    assert "function noteRoster(" in PUBLIC_CHAT
    assert "Math.min(Number(plain.ts) || Date.now(), Date.now())" in PUBLIC_CHAT
    assert "function personIsOnline(" in PUBLIC_CHAT
    assert "chat-presence-dot" in PUBLIC_CHAT
    assert ".chat-presence-dot.is-online" in PUBLIC_CHAT_HTML


def test_public_chat_orders_messages_by_ts_with_avatars_and_time():
    assert "function insertMessage(" in PUBLIC_CHAT
    assert "function buildRow(" in PUBLIC_CHAT
    assert "function makeAvatar(" in PUBLIC_CHAT
    assert "function fmtTime(" in PUBLIC_CHAT
    assert "GROUP_WINDOW_MS" in PUBLIC_CHAT
    assert ".chat-avatar" in PUBLIC_CHAT_HTML
    assert ".chat-time" in PUBLIC_CHAT_HTML


def test_public_chat_mention_autocomplete_accepts_with_tab():
    # Typing "@partial" pops a roster-backed suggestion list; Tab (or Enter /
    # click) accepts, arrows navigate, Escape dismisses. The token charset
    # matches CHAT_MENTION_RE so an accepted mention always renders linked.
    assert "function mentionTokenAtCaret(" in PUBLIC_CHAT
    assert "function mentionCandidates(" in PUBLIC_CHAT
    assert "function acceptMentionSuggest(" in PUBLIC_CHAT
    assert 'event.key === "Tab" || event.key === "Enter"' in PUBLIC_CHAT
    assert "acceptMentionSuggest()" in PUBLIC_CHAT
    assert 'input.addEventListener("input", updateMentionSuggest)' in PUBLIC_CHAT
    assert ".chat-mention-suggest" in PUBLIC_CHAT_HTML
    assert ".chat-mention-suggest-item.is-active" in PUBLIC_CHAT_HTML


def test_public_chat_usernames_link_to_relay_profile_pages():
    # Clicking a message author, their avatar, or a people-pane row opens
    # /@username — a RELATIVE URL, so the link lands on whichever relay is
    # serving the page (forkmesh.com or a self-hosted mainnode). ForkBot is
    # not an account, so its rows stay unlinked.
    assert 'return key ? "/@" + encodeURIComponent(key) : "#";' in PUBLIC_CHAT
    assert "author.href = mentionProfilePath(record.sender)" in PUBLIC_CHAT
    assert "avatarLink.href = mentionProfilePath(record.sender)" in PUBLIC_CHAT
    assert "row.href = mentionProfilePath(person.name)" in PUBLIC_CHAT
    assert 'document.createElement(isBot ? "span" : "a")' in PUBLIC_CHAT
    assert 'document.createElement(isBot ? "div" : "a")' in PUBLIC_CHAT
    assert "a.chat-author:hover" in PUBLIC_CHAT_HTML
    assert "a.chat-person:hover" in PUBLIC_CHAT_HTML


def test_dashboard_side_chat_orders_by_ts_with_avatar_and_time():
    # The rail's mini chat renders like the full chat — avatar + name + time —
    # and inserts each message by its own timestamp so replayed history and
    # live traffic interleave with the newest at the bottom.
    assert "function fmtChatTime(" in CHAT
    assert "tsMs: Number(tsMs) || Date.now()" in CHAT
    assert "sideEntries.splice(index, 0, entry)" in CHAT
    assert "fmtChatTime(message.tsMs)" in CHAT
    assert "avatarLetter(message.who)" in CHAT
    # Call sites hand the epoch timestamp through (formatting happens at
    # render), so ordering never depends on arrival order.
    assert "appendMessage(kind, who, text, entry.id, entry.senderId,\n                  Number(entry.ts) || Date.now())" in CHAT


def test_dashboard_side_chat_keeps_its_socket_alive_and_reconnects():
    # The room DO reaps sockets that send nothing for 3 minutes; the dashboard
    # chat used to go silently stale on idle tabs (no keepalive, no reconnect)
    # so new messages just stopped arriving. Mirrors the /chat page fix: 60s
    # presence beat (desktop cadence) + reconnect with backoff.
    assert 'send(makePlain("presence"))' in CHAT
    assert "}, 60000);" in CHAT
    assert "function scheduleReconnect(" in CHAT
    assert "reconnectDelayMs = Math.min(reconnectDelayMs * 2, 30000);" in CHAT
    close_handler = CHAT[
        CHAT.index('socket.addEventListener("close"'):
        CHAT.index('socket.addEventListener("error"')
    ]
    assert "scheduleReconnect();" in close_handler
