"""Contracts for the in-world recent #general message board."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
CHAT = (PUBLIC / "dashboard-chat.js").read_text(encoding="utf-8")
WORLD = (PUBLIC / "world" / "world.js").read_text(encoding="utf-8")
SCENE = (PUBLIC / "world" / "world-scene.js").read_text(encoding="utf-8")


def test_chat_embed_forwards_bounded_history_and_thumbnail_metadata():
    for contract in (
        "function worldAttachmentPreview(attachment)",
        'canvas.toDataURL("image/webp", 0.7)',
        "preview.length <= 120_000",
        'type: "forkmesh:world-chat"',
        "attachmentName:",
        "attachmentMime:",
        "reactionCount:",
        "attachmentPreview",
        "emitWorldChatHistory({",
    ):
        assert contract in CHAT
    assert "if (ts < newestHistoryTs) return" not in CHAT


def test_world_accepts_chat_only_from_its_same_origin_chat_frames():
    for contract in (
        'this.$("[data-world-chat-frame]")?.contentWindow',
        'this.$("[data-world-chat-terminal-frame]")?.contentWindow',
        "if (!chatSources.includes(event.source)) return",
        "this.recentWorldChatMessages.slice(-40)",
        "this.world?.updateWorldGeneralChat?.(",
        "attachmentPreview.length <= 120_000",
        "this.chatTerminalNewestAt",
    ):
        assert contract in WORLD


def test_physical_chat_board_is_beside_events_and_opens_full_chat():
    for contract in (
        "function worldGeneralChatTexture(THREE, messages = [])",
        '"#GENERAL · RECENT CHAT"',
        '"forkmesh-world-general-chat-board"',
        '"forkmesh-world-general-chat-board-face"',
        '"world-general-chat-board"',
        'registerMovableObject("world-general-chat-board"',
        "function updateWorldGeneralChat(messages = [])",
        "updateWorldGeneralChat,",
        "onWorldGeneralChatSelect()",
    ):
        assert contract in SCENE
    assert 'onWorldGeneralChatSelect: () => {' in WORLD
    assert 'this.openWorldChat("/dashboard/chat")' in WORLD

