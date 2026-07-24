"""Full web chat consumes only server-authorized private channel records."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CHAT = (ROOT / "public" / "chat.js").read_text(encoding="utf-8")
HTML = (ROOT / "public" / "chat.html").read_text(encoding="utf-8")
PROTOCOL = (
    ROOT / "public" / "docs" / "protocol" / "index.html"
).read_text(encoding="utf-8")


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


def test_chat_loads_authorized_private_channels_instead_of_fixed_labels():
    assert 'const PRIVATE_CHANNELS_ENDPOINT = "/api/chat/channels"' in CHAT
    assert "async function refreshPrivateChannels(" in CHAT
    assert "function reconcilePrivateChannels(" in CHAT
    assert "Authorization" in CHAT
    assert 'DEFAULT_CHANNELS = ["#general", "#welcome", "#random"]' not in CHAT
    assert "privateChannels" in CHAT
    assert "keyVersion" in CHAT


def test_private_channel_access_uses_ephemeral_server_room_ticket():
    source = _function_source("fetchRoomAccess")
    assert '"/room-access"' in source
    assert "access.webSocketUrl" in CHAT
    assert "access.passphrase" in CHAT
    assert "access.room" in CHAT
    assert "localStorage" not in source


def test_removed_or_rotated_channel_closes_stale_room_and_falls_back():
    source = _function_source("reconcilePrivateChannels")
    assert "keyVersion" in source
    assert "switchChatRoom" in source
    assert 'setActiveChannel("#general")' in source
    assert "privateChannels.delete" in source or "privateChannels.clear" in source


def test_admin_channel_controls_are_accessible_and_hidden_by_default():
    assert 'id="chat-channel-create"' in HTML
    assert 'aria-label="Create channel"' in HTML
    assert 'id="chat-channel-dialog"' in HTML
    assert 'aria-labelledby="chat-channel-dialog-title"' in HTML
    assert 'id="chat-channel-manage"' in HTML
    assert 'id="chat-channel-members"' in HTML
    assert 'id="chat-channel-error"' in HTML
    assert "[hidden] { display: none !important; }" in HTML
    assert "margin: auto;" in HTML
    assert "session?.isAdmin" in CHAT


def test_admin_controls_create_invite_list_and_remove_members():
    assert "async function createChannel(" in CHAT
    assert "async function refreshChannelMembers(" in CHAT
    assert "async function inviteChannelMember(" in CHAT
    assert "async function removeChannelMember(" in CHAT
    assert 'privateChannelRequest(PRIVATE_CHANNELS_ENDPOINT, {' in CHAT
    assert 'method: "POST"' in CHAT
    assert '`/api/chat/channels/${channel.id}/members`' in CHAT
    assert 'method: "DELETE"' in CHAT


def test_create_form_selects_visibility_and_initial_registered_users():
    for marker in (
        'id="chat-channel-visibility"',
        '<option value="private"',
        '<option value="public"',
        'id="chat-channel-initial-members"',
        'id="chat-channel-user-search"',
        'id="chat-channel-user-options"',
        'id="chat-channel-selected-count"',
        'id="chat-channel-visibility-badge"',
    ):
        assert marker in HTML

    create = _function_source("createChannel")
    assert "visibility" in create
    assert "members" in create
    assert "selectedInitialMembers" in CHAT
    assert "registeredUsers" in CHAT
    assert "renderInitialMemberPicker" in CHAT
    assert "syncInitialMemberVisibility" in CHAT


def test_channel_visibility_controls_labels_and_private_member_management():
    reconcile = _function_source("reconcilePrivateChannels")
    controls = _function_source("updateAdminChannelControls")
    rooms = _function_source("renderRooms")

    assert "visibility" in reconcile
    assert 'channel?.visibility === "private"' in controls
    assert "Public channel" in rooms
    assert "Private channel" in rooms


def test_only_opaque_private_channel_id_is_persisted_as_preference():
    source = _function_source("setActiveChannel")
    assert "record.id" in source
    assert "ACTIVE_CHANNEL_KEY" in source
    assert "passphrase" not in source
    assert "webSocketUrl" not in source


def test_protocol_documents_private_access_rotation_and_attachments():
    for marker in (
        'id="private-chat-channels"',
        "/api/chat/channels/&lt;channel-id&gt;/room-access",
        "60-second signed WebSocket ticket",
        "Removing a member atomically increments the key",
        "fileName",
        "fileMime",
        "1 MiB",
        "CHAT_HISTORY_MAX_BYTES_PER_ROOM",
        "16 MiB",
    ):
        assert marker in PROTOCOL
