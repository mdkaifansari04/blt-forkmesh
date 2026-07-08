from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_SRC = ROOT / "qt_client" / "src"
HEADER = QT_SRC / "MainWindow.h"
SETUP = QT_SRC / "MainWindowSetup.cpp"
SETTINGS = QT_SRC / "MainWindowSettings.cpp"
REPOS = QT_SRC / "MainWindowRepos.cpp"
MESSAGES = QT_SRC / "MainWindowMessages.cpp"
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
    assert "new ServerNode(chatDisplayName(), accountOwner()," in setup
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
    assert "m_backend->setNodeIdentity(accountOwner(), nodeOwnerDisplayName());" in body
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

    # Directory users carry the canonical /@name profile target; live roster
    # entries are a fallback when the directory has not seen that user yet.
    assert "std::as_const(m_chatDirectoryUsers)" in add_row_body
    assert "std::as_const(m_homeRoster)" in add_row_body
    assert add_row_body.index("std::as_const(m_chatDirectoryUsers)") < add_row_body.index(
        "std::as_const(m_homeRoster)"
    )
    assert "connect(row, &MessageRow::mentionClicked" in add_row_body
    assert "chatMentionProfileUrl" in add_row_body
    assert "QDesktopServices::openUrl(url)" in add_row_body
    assert 'url.setPath(QStringLiteral("/@")' in messages

    # Existing visible rows are rerendered after the async user directory lands,
    # otherwise old messages would stay plain text until the next full refresh.
    assert "renderConversationRows();" in directory_body
