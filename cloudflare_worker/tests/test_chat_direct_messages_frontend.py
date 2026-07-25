"""Full web-chat contracts for one-to-one direct messages."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CHAT = (ROOT / "public" / "chat.js").read_text(encoding="utf-8")
HTML = (ROOT / "public" / "chat.html").read_text(encoding="utf-8")


def _function_source(name):
    marker = f"function {name}("
    async_marker = f"async function {name}("
    start = CHAT.find(async_marker)
    if start < 0:
        start = CHAT.index(marker)
    next_function = CHAT.find("\nfunction ", start + 1)
    next_async = CHAT.find("\nasync function ", start + 1)
    ends = [value for value in (next_function, next_async) if value >= 0]
    return CHAT[start:min(ends) if ends else len(CHAT)]


def test_chat_has_accessible_direct_message_sidebar_and_picker():
    for marker in (
        'id="chat-direct-section"',
        'aria-labelledby="chat-direct-title"',
        'id="chat-direct-create"',
        'aria-label="New direct message"',
        'id="chat-direct-list"',
        'id="chat-direct-dialog"',
        'aria-labelledby="chat-direct-dialog-title"',
        'id="chat-direct-search"',
        'aria-label="Search people to message"',
        'id="chat-direct-options"',
        'id="chat-direct-error"',
    ):
        assert marker in HTML


def test_chat_loads_and_reconciles_authorized_direct_messages():
    assert (
        'const DIRECT_MESSAGES_ENDPOINT = "/api/chat/direct-messages"'
        in CHAT
    )
    assert "const directMessages = new Map()" in CHAT
    assert "function directMessageForKey(" in CHAT
    assert "function reconcileDirectMessages(" in CHAT
    assert "async function refreshDirectMessages(" in CHAT
    assert "data.conversations || []" in CHAT


def test_direct_room_access_uses_ephemeral_server_credentials():
    source = _function_source("fetchRoomAccess")
    assert 'DIRECT_MESSAGES_ENDPOINT + "/"' in source
    assert '"/room-access"' in source
    assert "access.webSocketUrl" in CHAT
    assert "access.passphrase" in CHAT
    assert "localStorage" not in source


def test_starting_a_direct_message_reuses_the_server_conversation():
    source = _function_source("startDirectMessage")
    assert "DIRECT_MESSAGES_ENDPOINT" in source
    assert 'method: "POST"' in source
    assert "JSON.stringify({ username" in source
    assert "conversation.id" in source
    assert "directMessageKey(conversation.id)" in source


def test_people_keep_profile_links_and_add_separate_message_actions():
    source = _function_source("renderPeople")
    assert 'document.createElement("a")' in source
    assert "mentionProfilePath(person.name)" in source
    assert 'message.className = "chat-person-message"' in source
    assert "startDirectMessage(person.name)" in source
    assert "currentName" in source


def test_only_opaque_direct_id_is_saved_as_a_preference():
    source = _function_source("setActiveChannel")
    assert "ACTIVE_DIRECT_KEY" in source
    assert "direct.id" in source
    assert "passphrase" not in source
    assert "webSocketUrl" not in source


def test_direct_message_rendering_uses_distinct_labels_and_no_forkbot():
    assert 'return "@" + record.otherUser' in CHAT
    assert 'channelVisibilityBadge.textContent = direct ? "direct"' in CHAT
    assert "isDirectMessageKey(activeChannel)" in _function_source(
        "maybeAskForkbot"
    )
    assert "isDirectMessageKey(scope)" in _function_source(
        "frameMatchesScope"
    )
