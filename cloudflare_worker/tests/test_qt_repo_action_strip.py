from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
QT_ACTIONS = ROOT / "qt_client" / "src" / "MainWindowActions.cpp"


def test_action_strip_is_parented_to_repo_detail_page_not_tab_viewport():
    source = QT_ACTIONS.read_text(encoding="utf-8")
    start = source.index("void MainWindow::ensureActionStrip()")
    end = source.index("void MainWindow::updateActionStrip()")
    body = source[start:end]

    assert "QWidget *page = m_repoDetailSection;" in body
    assert "m_repoActionsTab->parentWidget()" not in body
    assert "tab-bar scroll" in body
