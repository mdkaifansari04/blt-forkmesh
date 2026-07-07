from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_SRC = ROOT / "qt_client" / "src"
HEADER = QT_SRC / "MainWindow.h"
SETUP = QT_SRC / "MainWindowSetup.cpp"
SETTINGS = QT_SRC / "MainWindowSettings.cpp"
REPOS = QT_SRC / "MainWindowRepos.cpp"


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
