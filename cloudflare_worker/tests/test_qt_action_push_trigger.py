"""Push-triggered Actions must fire for commits that land through the app.

Workflows with `on: [push]` — "CI tests" and "Deploy Cloudflare Worker" — used
to be queued only from the served bare mirror's post-receive hook. That hook
runs for a real `git push` into the mirror and nothing else, but every commit
this app makes lands in the working copy and reaches the served mirror through
syncRepository's `git fetch`, which runs no hooks at all. Merges made in-app
therefore queued no runs and the Worker stopped being deployed.

The fix watches the served mirror's own default branch, so the trigger no longer
depends on which transport moved it.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ACTIONS = (ROOT / "qt_client/src/MainWindowActions.cpp").read_text(
    encoding="utf-8"
)
HEADER = (ROOT / "qt_client/src/MainWindow.h").read_text(encoding="utf-8")
DEPLOY = (ROOT / ".forkmesh/deploy.yml").read_text(encoding="utf-8")


def _watcher() -> str:
    return ACTIONS.split(
        "void MainWindow::scanServedMirrorHeads(", 1
    )[1].split("\n}\n", 1)[0]


def _spool_sweep() -> str:
    return ACTIONS.split("void MainWindow::scanActionSpool()", 1)[1].split(
        "\nvoid MainWindow::scanServedMirrorHeads", 1
    )[0]


def test_deploy_workflow_is_still_reached_by_push():
    # The whole trigger path below exists to serve this one line; if the deploy
    # ever stops declaring `on: push` the watcher cannot help it.
    assert "on: [push]" in DEPLOY
    assert "name: Deploy Cloudflare Worker" in DEPLOY


def test_spool_sweep_watches_the_served_mirror_head():
    sweep = _spool_sweep()
    assert "scanServedMirrorHeads(pushedCommits);" in sweep
    # The watcher runs before the queue is drained, so a commit it discovers
    # starts in the same sweep rather than waiting for the next tick.
    assert sweep.index("scanServedMirrorHeads(pushedCommits);") < sweep.index(
        "processActionQueue();"
    )
    assert "void scanServedMirrorHeads(const QHash<int, QString> &" in HEADER


def test_a_real_post_receive_event_is_not_queued_twice():
    # The hook path records what it reported so the watcher can tell "already
    # queued in this sweep" from "new", instead of doubling every run whenever
    # someone does push into the served mirror by hand.
    sweep = _spool_sweep()
    assert "pushedCommits.insert(pushedIndex, commit.toLower());" in sweep
    assert "if (pushedCommits.value(index) == commit)" in _watcher()


def test_only_the_working_copy_holder_queues_workflows():
    watcher = _watcher()
    # A pure mirror replicates whatever the source serves. Queueing there too
    # would fan a single merge out into a duplicate run on every mirror node.
    assert "repo.localPath.trimmed().isEmpty()" in watcher
    assert "repo.previewOnly" in watcher
    assert "!repo.actionsEnabled" in watcher
    # Gateway-managed Actions mirrors keep their own trigger elsewhere.
    assert "repo.externallyManagedActions" in watcher


def test_first_sight_of_a_branch_does_not_replay_its_history():
    watcher = _watcher()
    record = watcher.index("settings.setValue(key, commit);")
    first_run = watcher.index("if (previous.isEmpty())")
    queue = watcher.index("enqueuePushEvent(repo.owner, repo.name")
    # Record the tip first, so a run that fails to start cannot leave the
    # watcher re-queueing the same commit on every sweep...
    assert record < first_run < queue
    # ...and never treat the branch's whole history as one enormous push.
    assert "previous == commit" in watcher


def test_watcher_survives_a_mirror_that_is_being_re_sealed():
    watcher = _watcher()
    # An encrypted repository is served from a temporary materialization that
    # every seal replaces, so both the path and the ref lookup can be missing
    # for a moment. Neither may queue a run against an empty commit.
    assert 'QDir(repo.mirrorPath).exists()' in watcher
    assert "if (ref.isEmpty())" in watcher
    assert "if (commit.isEmpty())" in watcher
    # The remembered tip is keyed by repository, not by that unstable path.
    assert "QString servedActionHeadKey(const RepositoryRecord &repo)" in ACTIONS
    assert "actions/servedHeads/" in ACTIONS


def test_head_ref_lookup_rejects_a_detached_or_bogus_head():
    lookup = ACTIONS.split("QString gitHeadRef(const QString &repository)", 1)[
        1
    ].split("\n}\n", 1)[0]
    assert 'QStringLiteral("symbolic-ref")' in lookup
    assert 'QStringLiteral("--quiet")' in lookup
    # Reuses the same refs/heads/... validation the external-source watcher uses.
    assert "kExternalActionRef.match(ref).hasMatch()" in lookup
