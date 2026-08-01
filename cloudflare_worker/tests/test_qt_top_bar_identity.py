from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_SRC = ROOT / "qt_client" / "src"
CHAT = QT_SRC / "MainWindowChat.cpp"
HEADER = QT_SRC / "MainWindow.h"
SETTINGS = QT_SRC / "MainWindowSettings.cpp"
SETUP = QT_SRC / "MainWindowSetup.cpp"


def _body(source: str, start: str, end: str) -> str:
    i = source.index(start)
    j = source.index(end, i)
    return source[i:j]


def test_top_bar_merges_user_identity_into_node_name_and_avatar():
    header = HEADER.read_text(encoding="utf-8")
    chat = CHAT.read_text(encoding="utf-8")





    assert "QLabel *m_userLabel = nullptr;" not in header
    assert "QPushButton *m_userMenuButton = nullptr;" not in header
    assert "QPushButton *m_avatarNavButton = nullptr;" not in header
    assert "QPushButton *m_userAvatarNavButton = nullptr;" in header
    assert "QLabel *m_navNodeName = nullptr;" in header

    assert "mainRow->addWidget(m_userAvatarNavButton);" in chat
    assert "m_connectionDot = new QLabel(m_userAvatarNavButton);" in chat


def test_top_bar_user_identity_prefers_linked_owner_over_node_name():
    setup = SETUP.read_text(encoding="utf-8")
    body = _body(
        setup,
        "QString MainWindow::topBarUserName() const",
        "bool MainWindow::accountEmailVerified",
    )

    assert body.index("m_nodeOwnerUser.trimmed().toLower()") < body.index(
        "settingsAccountName()"
    )
    assert "return accountNameFromInput(m_userName, QString());" in body


def test_user_avatar_uses_user_identicon_not_node_machine_avatar():
    settings = SETTINGS.read_text(encoding="utf-8")
    user_body = _body(
        settings,
        "QByteArray MainWindow::effectiveUserAvatar()",
        "void MainWindow::updateAvatarButton()",
    )
    node_body = _body(
        settings,
        "QByteArray MainWindow::effectiveAvatar()",
        "QByteArray MainWindow::effectiveUserAvatar()",
    )

    assert "topBarUserName()" in user_body
    assert "forkMeshAvatarPng(seed)" in user_body
    assert "forkMeshNodeAvatarPng(seed)" in node_body
