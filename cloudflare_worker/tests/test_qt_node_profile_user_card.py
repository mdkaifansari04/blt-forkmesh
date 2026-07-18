from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_SRC = ROOT / "qt_client" / "src"
CHAT = QT_SRC / "MainWindowChat.cpp"
HEADER = QT_SRC / "MainWindow.h"
THEME = QT_SRC / "Theme.h"


def _body(source: str, start: str, end: str) -> str:
    i = source.index(start)
    j = source.index(end, i)
    return source[i:j]


def test_node_profile_features_user_card_before_node_details():
    header = HEADER.read_text(encoding="utf-8")
    chat = CHAT.read_text(encoding="utf-8")
    build = _body(
        chat,
        "QWidget *MainWindow::buildNodeProfilePanel()",
        "void MainWindow::hideNodeProfile()",
    )

    assert "QListWidget *m_profileUserNodesList = nullptr;" in header
    # The user card no longer repeats the node's own avatar/name (shown in the
    # left-column header); those widgets are gone.
    assert "m_profileUserAvatar" not in header
    assert "m_profileUserName" not in header
    assert 'm_profileAccountSection->setObjectName("profileUserCard");' in build
    assert 'auto *accountLabel = makeProfileSection("USER PROFILE");' in build
    # The sibling-nodes card now lives at the top of the right column so the
    # nodes read as their own panel on the right, not a full-width top banner.
    assert "rightColumn->addWidget(m_profileAccountSection);" in build
    assert "layout->addWidget(m_profileAccountSection);" not in build
    assert build.index("rightColumn->addWidget(m_profileAccountSection);") < build.index(
        "rightColumn->addWidget(m_profileHostingLabel);"
    )


def test_node_profile_user_card_lists_owned_nodes_with_icons():
    chat = CHAT.read_text(encoding="utf-8")
    render = _body(
        chat,
        "void MainWindow::renderProfileAccountStatus()",
        "void MainWindow::refreshProfileAccountStatus()",
    )

    assert "m_profileUserNodesList->clear();" in render
    assert "nodeMachineFavicon(nodeName, 28)" in render
    assert "roundedRectPixmap(nodeMachineFavicon(nodeName, 28), 28, 8)" in render
    assert 'nodeName + QStringLiteral(" (this node)")' in render
    assert "m_profileUserNodesList->setFixedHeight" in render


def test_node_profile_user_card_has_dark_and_light_styles():
    theme = THEME.read_text(encoding="utf-8")

    assert theme.count("#profileUserCard") >= 2
    assert theme.count("#profileNodeList::item") >= 2
