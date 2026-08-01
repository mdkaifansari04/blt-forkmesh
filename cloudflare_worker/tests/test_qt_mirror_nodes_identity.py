from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_SRC = ROOT / "qt_client" / "src"
RELEASES = QT_SRC / "MainWindowReleases.cpp"
REPOS = QT_SRC / "MainWindowRepos.cpp"
BACKEND = QT_SRC / "ChatBackend.h"
SERVER_NODE = QT_SRC / "ServerNode.cpp"
SERVER_NODE_H = QT_SRC / "ServerNode.h"
MAIN_WINDOW_H = QT_SRC / "MainWindow.h"
SETTINGS = QT_SRC / "MainWindowSettings.cpp"


def _body(source: str, start: str, end: str) -> str:
    i = source.index(start)
    j = source.index(end, i)
    return source[i:j]


def test_mirror_nodes_table_has_separate_owner_column():
    source = RELEASES.read_text(encoding="utf-8")
    build = _body(
        source,
        "QWidget *MainWindow::buildMirrorNodesTab()",
        "void MainWindow::loadMirrorNodesPanel()",
    )
    load = _body(
        source,
        "void MainWindow::loadMirrorNodesPanel()",
        "void MainWindow::fetchCatalogMirrors",
    )

    assert "MirrorNodeColOwner" in source
    assert '{"Node", "Owner", "Latest commit"' in build
    assert "new QTableWidget(0, MirrorNodeColumnCount)" in build
    assert "m_mirrorNodesTable->setItem(row, MirrorNodeColOwner" in load
    assert 'm.value(QStringLiteral("ownerUser"))' in load


def test_mirror_nodes_show_the_latest_commit_message_and_author():
    # A hash alone doesn't say what a node is serving, so every row names the
    # commit's subject and author (adhoc #337). Peers advertise the identity
    # themselves; our own mirror is only the fallback for older peers.
    source = RELEASES.read_text(encoding="utf-8")
    backend = BACKEND.read_text(encoding="utf-8")
    server = SERVER_NODE.read_text(encoding="utf-8")
    repos = REPOS.read_text(encoding="utf-8")
    build = _body(
        source,
        "QWidget *MainWindow::buildMirrorNodesTab()",
        "void MainWindow::requestMirrorNodesRefresh()",
    )
    load = _body(
        source,
        "void MainWindow::loadMirrorNodesPanel()",
        "void MainWindow::fetchCatalogMirrors",
    )

    assert '"Latest commit", "Message", "Author"' in build
    assert "MirrorNodeColMessage" in source and "MirrorNodeColAuthor" in source
    # Both row builders (live roster and catalog-backed) fill the columns.
    assert load.count("setCommitIdentityCells(row,") == 2
    assert "resolveCommitIdentity(advert->commit, advert->commitIdentity)" in load
    assert 'm.value(QStringLiteral("lastCommitMessage"))' in load
    assert 'm.value(QStringLiteral("lastCommitAuthorName"))' in load

    # The advert carries the identity across the wire so a node that doesn't
    # hold a peer's commit can still name it.
    assert "struct CommitIdentity" in backend
    assert "CommitIdentity commitIdentity;" in backend
    assert 'head.insert("m", m.commitIdentity.subject' in server
    assert 'head.insert("n", m.commitIdentity.author' in server
    assert "advert.commitIdentity.subject =" in server

    # ...and into the signed catalog record, so an offline node still names it.
    assert '{"commitSubject", headIdentity.subject}' in repos
    assert '{"commitAuthorName", headIdentity.author}' in repos
    assert 'record.insert(QStringLiteral("commitSubject"), commitSubject)' in repos


def test_mirror_nodes_use_node_account_not_chat_name():
    source = RELEASES.read_text(encoding="utf-8")
    load = _body(
        source,
        "void MainWindow::loadMirrorNodesPanel()",
        "void MainWindow::fetchCatalogMirrors",
    )

    assert "advert->ownerName.section(QLatin1Char('/'), 0, 0)" in load
    assert "name = node.nodeName.trimmed();" in load
    assert "name = accountOwner().trimmed();" in load
    assert "const QString nodeDisplay = displayNodeName(node, advert);" in load
    assert "shownNames.insert(nodeDisplay.trimmed().toLower());" in load
    assert "nodeDisplay.trimmed().toLower()" in load


def test_published_mirror_metadata_carries_owner_user():
    source = REPOS.read_text(encoding="utf-8")
    publish = _body(
        source,
        "void MainWindow::publishRepositoryNow",
        "namespace {",
    )

    assert '{"ownerUser", nodeOwnerDisplayName()}' in publish


def test_mirror_nodes_can_request_live_peer_refresh():
    releases = RELEASES.read_text(encoding="utf-8")
    backend = BACKEND.read_text(encoding="utf-8")
    server = SERVER_NODE.read_text(encoding="utf-8")
    server_h = SERVER_NODE_H.read_text(encoding="utf-8")
    window_h = MAIN_WINDOW_H.read_text(encoding="utf-8")
    settings = SETTINGS.read_text(encoding="utf-8")

    build = _body(
        releases,
        "QWidget *MainWindow::buildMirrorNodesTab()",
        "void MainWindow::requestMirrorNodesRefresh()",
    )
    request = _body(
        releases,
        "void MainWindow::requestMirrorNodesRefresh()",
        "void MainWindow::loadMirrorNodesPanel()",
    )

    assert "setAccessibleName(QStringLiteral(\"Refresh nodes\"))" in build
    assert "refreshNodesButton->setFixedSize(32, 30)" in build
    assert "&MainWindow::requestMirrorNodesRefresh" in build
    assert "m_backend->requestMirrorRefresh(source, ownerName);" in request
    assert "m_backend->advertiseMirrorsNow();" in request
    assert "m_mirrorAdvertSig.clear();" in request

    # Keep the expensive catalog re-fetch scoped to the visible operator page,
    # while still updating the displayed mirror state once per minute.
    assert "auto *panelRefresh = new QTimer(m_mirrorNodesTable);" in build
    assert "panelRefresh->setInterval(60 * 1000);" in build
    assert "if (!m_mirrorNodesTable->isVisible())" in build
    assert "m_catalogMirrorsFetchedMs = 0;" in build
    assert "A manual refresh is an operator request for fresh catalog state" in build

    assert "virtual void requestMirrorRefresh" in backend
    assert "virtual void advertiseMirrorsNow()" in backend
    assert "void mirrorRefreshRequested" in backend
    assert "void requestMirrorRefresh" in server_h
    assert "void advertiseMirrorsNow() override" in server_h
    assert 'makeMessage("mirror-refresh")' in server
    assert 'type == "mirror-refresh"' in server
    assert "emit mirrorRefreshRequested" in server
    assert "sendHello(true, false);" in server
    assert "&ChatBackend::mirrorRefreshRequested" in settings
    assert "&MainWindow::onMirrorRefreshRequested" in settings
    assert "void requestMirrorNodesRefresh();" in window_h
    assert "void onMirrorRefreshRequested" in window_h
