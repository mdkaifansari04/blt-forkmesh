from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_SRC = ROOT / "qt_client" / "src"
RELEASES = QT_SRC / "MainWindowReleases.cpp"
REPOS = QT_SRC / "MainWindowRepos.cpp"


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
    assert "nodeDisplay.toLower()" in load


def test_published_mirror_metadata_carries_owner_user():
    source = REPOS.read_text(encoding="utf-8")
    publish = _body(
        source,
        "void MainWindow::publishRepositoryNow",
        "namespace {",
    )

    assert '{"ownerUser", nodeOwnerDisplayName()}' in publish
