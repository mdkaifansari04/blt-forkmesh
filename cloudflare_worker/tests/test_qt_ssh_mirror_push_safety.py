"""Safety contracts for unattended Qt-to-headless-mirror propagation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REPOS = (ROOT / "qt_client/src/MainWindowRepos.cpp").read_text(
    encoding="utf-8"
)


def test_automatic_ssh_mirror_push_cannot_rewind_or_prune_remote_refs():
    push = REPOS.split(
        "void MainWindow::pushToSshMirrorRemotes(int index)", 1
    )[1].split("\nvoid MainWindow::syncRepository", 1)[0]

    assert 'QStringLiteral("refs/heads/*:refs/heads/*")' in push
    assert 'QStringLiteral("refs/tags/*:refs/tags/*")' in push
    assert 'QStringLiteral("+refs/heads/*:refs/heads/*")' not in push
    assert 'QStringLiteral("+refs/tags/*:refs/tags/*")' not in push
    assert 'QStringLiteral("--prune")' not in push
    assert "never let an unattended desktop" in push
