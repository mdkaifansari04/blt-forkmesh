"""Threaded room chat stays interoperable across web, World, and Qt."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PUBLIC = ROOT / "cloudflare_worker" / "public"
WEB_CHAT = (PUBLIC / "chat.js").read_text(encoding="utf-8")
DASHBOARD_CHAT = (PUBLIC / "dashboard-chat.js").read_text(encoding="utf-8")
WORLD = (PUBLIC / "world" / "world.js").read_text(encoding="utf-8")
QT_BACKEND = (ROOT / "qt_client" / "src" / "ChatBackend.h").read_text(
    encoding="utf-8"
)
QT_NODE = (ROOT / "qt_client" / "src" / "ServerNode.cpp").read_text(
    encoding="utf-8"
)
QT_MESSAGES = (ROOT / "qt_client" / "src" / "MainWindowMessages.cpp").read_text(
    encoding="utf-8"
)
QT_ROW = (ROOT / "qt_client" / "src" / "MessageRow.cpp").read_text(
    encoding="utf-8"
)


def test_every_client_sends_the_same_thread_reply_envelope():
    for source in (WEB_CHAT, DASHBOARD_CHAT):
        assert 'makePlain("thread-reply"' in source
        assert "rootId:" in source
        assert '"thread-reply"' in source[source.index("DURABLE_TYPES") :]

    assert "virtual void sendThreadReply" in QT_BACKEND
    send_reply = QT_NODE[
        QT_NODE.index("void ServerNode::sendThreadReply") :
        QT_NODE.index("void ServerNode::setAccountKind")
    ]
    assert 'makeMessage("thread-reply")' in send_reply
    assert 'message.insert("rootId", rootId)' in send_reply
    assert 'message.insert("channel", channel)' in send_reply
    assert "storeHistory(message)" in send_reply
    assert "sendEncrypted(message, true)" in send_reply


def test_qt_keeps_replies_out_of_the_room_timeline_and_opens_a_thread_view():
    assert 'type == "chat" || type == "thread-reply"' in QT_NODE
    assert 'out.threadRootId = message.value("rootId")' in QT_NODE
    assert "if (!message.threadRootId.isEmpty())" in QT_MESSAGES
    assert "void MainWindow::openChatThread" in QT_MESSAGES
    assert "void MainWindow::sendChatThreadReply" in QT_MESSAGES
    assert "m_backend->sendThreadReply" in QT_MESSAGES
    assert 'menu->addAction("Reply in thread")' in QT_ROW
    assert 'thread->setObjectName("threadSummary")' in QT_ROW


def test_world_embedded_chat_renders_and_sends_threads_without_top_level_bubbles():
    # World opens /dashboard/chat in its in-world panel, so this controller is
    # the actual World chat implementation as well as the dashboard one.
    assert 'this.openWorldChat("/dashboard/chat")' in WORLD
    assert "function ensureThreadDialog()" in DASHBOARD_CHAT
    assert "function renderThreadEntry(" in DASHBOARD_CHAT
    assert 'messageActionButton("Reply", () => openThread(record)' in DASHBOARD_CHAT
    assert "threadRepliesById.set(record.id, record)" in DASHBOARD_CHAT
    assert "renderThreadEntry(plain, \"peer\", !historyReplay)" in DASHBOARD_CHAT
    handler = DASHBOARD_CHAT[
        DASHBOARD_CHAT.index('} else if (type === "thread-reply")') :
        DASHBOARD_CHAT.index('} else if (type === "history")')
    ]
    assert "renderChatEntry" not in handler
    assert "emitWorldChatBubble" not in handler
