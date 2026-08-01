from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_SRC = ROOT / "qt_client" / "src"
HEADER = QT_SRC / "MainWindow.h"
SETUP = QT_SRC / "MainWindowSetup.cpp"
SETTINGS = QT_SRC / "MainWindowSettings.cpp"
REPOS = QT_SRC / "MainWindowRepos.cpp"
MESSAGES = QT_SRC / "MainWindowMessages.cpp"
ISSUES = QT_SRC / "MainWindowIssues.cpp"
MESSAGE_ROW_HEADER = QT_SRC / "MessageRow.h"
MESSAGE_ROW = QT_SRC / "MessageRow.cpp"


def _body(source: str, start: str, end: str) -> str:
    i = source.index(start)
    j = source.index(end, i)
    return source[i:j]


def test_chat_backend_starts_with_user_display_name_not_node_name():
    header = HEADER.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")

    assert "QString chatDisplayName() const;" in header
    assert "QString nodeOwnerDisplayName() const;" in header
    assert "void updateChatIdentity();" in header
    assert "new ServerNode(chatDisplayName(), machineNodeName()," in setup
    assert "nodeOwnerDisplayName()," in setup
    assert "m_profileIdentity.publicKey()" in setup


def test_chat_identity_pushes_user_name_and_user_avatar():
    setup = SETUP.read_text(encoding="utf-8")
    body = _body(
        setup,
        "void MainWindow::updateChatIdentity()",
        "bool MainWindow::accountEmailVerified",
    )

    assert "const QString name = chatDisplayName();" in body
    assert "m_backend->setUserName(name);" in body
    assert "m_backend->setNodeIdentity(machineNodeName(), nodeOwnerDisplayName());" in body
    assert "const QByteArray avatar = effectiveUserAvatar();" in body
    assert "m_backend->setAvatar(avatar);" in body
    assert "effectiveAvatar()" not in body


def test_profile_name_and_avatar_edits_refresh_chat_identity():
    repos = REPOS.read_text(encoding="utf-8")
    name_body = _body(
        repos,
        "void MainWindow::onProfileNameChanged",
        "void MainWindow::onAvatarChosen",
    )
    avatar_body = _body(
        repos,
        "void MainWindow::onAvatarChosen",
        "void MainWindow::logout",
    )

    assert "m_backend->setUserName(trimmed)" not in name_body
    assert "m_backend->setAvatar(pngData)" not in avatar_body
    assert "updateChatIdentity();" in name_body
    assert "updateChatIdentity();" in avatar_body


def test_user_avatar_prefers_chosen_avatar_before_generated_identicon():
    settings = SETTINGS.read_text(encoding="utf-8")
    body = _body(
        settings,
        "QByteArray MainWindow::effectiveUserAvatar()",
        "void MainWindow::updateAvatarButton()",
    )

    assert "if (!m_userAvatar.isEmpty())" in body
    assert "return m_userAvatar;" in body
    assert body.index("return m_userAvatar;") < body.index("forkMeshAvatarPng(seed)")


def test_chat_member_column_is_user_directory_not_online_nodes():
    header = HEADER.read_text(encoding="utf-8")
    messages = MESSAGES.read_text(encoding="utf-8")
    roster_body = _body(
        messages,
        "void MainWindow::refreshChatMembers()",
        "void MainWindow::removeChatMember",
    )
    directory_body = _body(
        messages,
        "void MainWindow::refreshChatUserDirectory()",
        "void MainWindow::mergeChatUserDirectory",
    )

    assert "QHash<QString, MemberInfo> m_chatDirectoryUsers" in header
    assert 'accountsApiUrl(QStringLiteral("users"))' in directory_body
    assert "for (const MemberInfo &member : std::as_const(m_chatDirectoryUsers))" in roster_body
    assert "if (!online)" not in roster_body
    assert "USERS \\xE2\\x80\\x94 %1 \\xC2\\xB7 %2 online" in roster_body
    assert "chatUserDisplayName(member)" in roster_body


def test_self_row_folds_into_its_own_account_group():



    messages = MESSAGES.read_text(encoding="utf-8")
    roster_body = _body(
        messages,
        "void MainWindow::refreshChatMembers()",
        "void MainWindow::removeChatMember",
    )


    assert 'if (m.self)\n            return QStringLiteral("\\x01self");' not in roster_body
    assert "const QString selfOwner = accountOwner().trimmed().toLower();" in roster_body


    assert "member.self = member.self || g.primary.self;" in roster_body


def test_chat_mentions_highlight_and_open_user_profiles():
    header = MESSAGE_ROW_HEADER.read_text(encoding="utf-8")
    row = MESSAGE_ROW.read_text(encoding="utf-8")
    messages = MESSAGES.read_text(encoding="utf-8")
    add_row_body = _body(
        messages,
        "MessageRow *MainWindow::addMessageRow",
        "void MainWindow::renderConversationRows",
    )
    directory_body = _body(
        messages,
        "void MainWindow::mergeChatUserDirectory",
        "void MainWindow::refreshChatMembers",
    )

    assert "const QHash<QString, MemberInfo> &mentionProfiles" in header
    assert "void mentionClicked(const QString &id, const QString &name);" in header
    assert "renderMentionedText(message.text, m_mentionProfiles)" in row
    assert "forkmesh-mention:" in row
    assert "QLabel::linkHovered" in row
    assert "QToolTip::showText" in row
    assert "QLabel::linkActivated" in row



    assert "std::as_const(m_chatDirectoryUsers)" in add_row_body
    assert "std::as_const(m_homeRoster)" in add_row_body
    assert add_row_body.index("std::as_const(m_chatDirectoryUsers)") < add_row_body.index(
        "std::as_const(m_homeRoster)"
    )
    assert "connect(row, &MessageRow::mentionClicked" in add_row_body
    assert "chatMentionProfileUrl" in add_row_body
    assert "QDesktopServices::openUrl(url)" in add_row_body
    assert 'url.setPath(QStringLiteral("/@")' in messages



    assert "renderConversationRows();" in directory_body


def test_chat_composer_stays_visible_and_rooms_scroll_independently():
    issues = ISSUES.read_text(encoding="utf-8")
    body = _body(
        issues,
        "QWidget *MainWindow::buildChatSection()",
        "void MainWindow::updateHomeStats()",
    )

    assert "auto *roomsScroll = new QScrollArea;" in body
    assert "roomsScroll->setWidgetResizable(true);" in body
    assert "roomsScroll->setMinimumHeight(0);" in body
    assert "roomsScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);" in body
    assert "sidebarLayout->addWidget(roomsScroll, 1);" in body

    for list_name in ("m_channelList", "m_dmList"):
        assert f"{list_name}->setMinimumHeight(0);" in body
        assert f"{list_name}->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);" in body
        assert f"{list_name}->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);" in body

    assert "m_messageScroll->setMinimumHeight(0);" in body
    assert "m_messageScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);" in body
    assert "composer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);" in body
    assert "m_messageInput->setMinimumWidth(0);" in body
    assert "auto *mainColumnHost = new QWidget;" in body
    assert "mainColumnHost->setMinimumHeight(0);" in body
    assert "layout->addWidget(mainColumnHost, 1);" in body


def test_desktop_frames_carry_account_kind_for_web_display():





    server_node = (QT_SRC / "ServerNode.cpp").read_text(encoding="utf-8")
    server_node_h = (QT_SRC / "ServerNode.h").read_text(encoding="utf-8")
    chat_backend = (QT_SRC / "ChatBackend.h").read_text(encoding="utf-8")
    chat_win = (QT_SRC / "MainWindowChat.cpp").read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")

    assert "virtual void setAccountKind(const QString &kind)" in chat_backend
    assert "void setAccountKind(const QString &kind) override;" in server_node_h
    assert 'message.insert("accountKind", m_accountKind);' in server_node
    switcher = _body(
        chat_win,
        "void MainWindow::updateUserSwitcher()",
        "const QString user = topBarUserName();",
    )
    assert "m_profileIsUserAccount || !m_profileLinkedNodes.isEmpty()" in switcher
    assert "m_backend->setAccountKind(userLike" in switcher
    assert "server->setAccountKind(" in setup


def test_desktop_composer_bridges_forkbot_like_the_web_chat():





    header = HEADER.read_text(encoding="utf-8")
    messages = MESSAGES.read_text(encoding="utf-8")
    server_node = (QT_SRC / "ServerNode.cpp").read_text(encoding="utf-8")
    chat_backend = (QT_SRC / "ChatBackend.h").read_text(encoding="utf-8")

    assert "void maybeAskForkbot(const QString &conversation, const QString &text);" in header
    assert "maybeAskForkbot(m_currentConversation, text);" in messages
    bridge = _body(
        messages,
        "void MainWindow::maybeAskForkbot",
        "void MainWindow::onComposerEdited",
    )
    assert '"(?:^|[^A-Za-z0-9_-])@?forkbot\\\\b"' in bridge
    assert "m_privateChannels.contains(conversation)" in bridge
    assert '/api/forkbot/chat' in bridge
    assert '{QStringLiteral("context"), context}' in bridge
    assert "m_backend->sendBotChat(channel, botMessage);" in bridge

    assert "virtual void sendBotChat(const QString &channel, const QString &text)" in chat_backend
    bot_chat = _body(
        server_node,
        "void ServerNode::sendBotChat",
        "void ServerNode::sendDirect",
    )
    assert '{"senderId", QStringLiteral("forkbot")}' in bot_chat
    assert '{"accountKind", QStringLiteral("user")}' in bot_chat


    assert "m_privateChannels.contains(channel)" in bot_chat


def test_own_account_messages_never_mark_unread_or_notify():





    messages = MESSAGES.read_text(encoding="utf-8")
    body = _body(
        messages,
        "void MainWindow::onMessage(const ChatMessage &message)",
        "void MainWindow::onReaction(",
    )
    assert "const QString ownChatName = chatDisplayName().trimmed();" in body
    assert "const bool ownMessage =" in body
    assert "message.self ||" in body
    assert "Qt::CaseInsensitive" in body

    assert "if (!ownMessage &&" in body
    assert "if (!ownMessage) {" in body
    assert "if (!message.self &&" not in body
    assert "if (!message.self) {" not in body
