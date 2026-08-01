"""Safety contracts for unattended Qt-to-headless-mirror propagation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REPOS = (ROOT / "qt_client/src/MainWindowRepos.cpp").read_text(
    encoding="utf-8"
)
ACTIONS = (ROOT / "qt_client/src/MainWindowActions.cpp").read_text(
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
    assert 'QStringLiteral("--no-force")' in push
    assert 'QStringLiteral("--no-prune")' in push


def test_automatic_ssh_mirror_push_uses_explicit_fast_forward_policy():
    push = REPOS.split(
        "void MainWindow::pushToSshMirrorRemotes(int index)", 1
    )[1].split("\nvoid MainWindow::syncRepository", 1)[0]
    command = push.split('process->start(QStringLiteral("git"),', 1)[1]




    assert command.index('QStringLiteral("--no-force")') < command.index(
        'QStringLiteral("refs/heads/*:refs/heads/*")'
    )
    assert command.index('QStringLiteral("--no-prune")') < command.index(
        'QStringLiteral("refs/heads/*:refs/heads/*")'
    )
    assert 'QStringLiteral("+refs/' not in command


def test_direct_push_fans_out_to_ssh_mirrors_immediately():
    spool = ACTIONS.split("void MainWindow::scanActionSpool()", 1)[1].split(
        "\nvoid MainWindow::syncMirrorActionsConfiguration", 1
    )[0]




    assert "m_backend->notifyMirrorUpdated(" in spool
    assert "pushToSshMirrorRemotes(idx);" in spool
    assert spool.index("m_backend->notifyMirrorUpdated(") < spool.index(
        "pushToSshMirrorRemotes(idx);"
    )
