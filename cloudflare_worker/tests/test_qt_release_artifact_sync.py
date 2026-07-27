from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "qt_client" / "src" / "MainWindow.cpp"
INTERNAL = ROOT / "qt_client" / "src" / "MainWindowInternal.h"
REPOS = ROOT / "qt_client" / "src" / "MainWindowRepos.cpp"
RELEASES = ROOT / "qt_client" / "src" / "MainWindowReleases.cpp"
HEADER = ROOT / "qt_client" / "src" / "MainWindow.h"


def test_roster_artifact_adverts_trigger_release_blob_replication():
    source = REPOS.read_text(encoding="utf-8")
    start = source.index("void MainWindow::syncMirrorsBehindRoster()")
    end = source.index("void MainWindow::propagateRepoUpdate(int index)")
    body = source[start:end]

    assert "const int localArtifactCount = mirrorArtifactCount(repo.mirrorPath);" in body
    assert "bool artifactsBehind = false;" in body
    assert "m.artifactCount > localArtifactCount" in body
    assert "replicateReleaseArtifacts(i);" in body
    assert body.index("syncRepository(i, /*quiet=*/true);") < body.index(
        "replicateReleaseArtifacts(i);"
    )


def test_mirror_dropped_event_fallback_is_jittered_three_minutes():
    main = MAIN.read_text(encoding="utf-8")
    internal = INTERNAL.read_text(encoding="utf-8")
    repos = REPOS.read_text(encoding="utf-8")

    assert "constexpr qint64 kMirrorSyncIntervalMs = 3LL * 60 * 1000;" in internal
    assert "constexpr int kMirrorSyncJitterPercent = 15;" in internal
    assert (
        "int(kMirrorSyncIntervalMs * kMirrorSyncJitterPercent / 100)"
        in main
    )
    assert (
        "m_mirrorSyncTimer->start(int(kMirrorSyncIntervalMs) +"
        in main
    )
    assert "-mirrorJitterSpanMs, mirrorJitterSpanMs + 1" in main
    mirror_timer = main[
        main.index("// Keep mirrors fresh:")
        : main.index("// Source-of-truth nodes pick up issues")
    ]
    assert mirror_timer.count("m_mirrorSyncTimer = new QTimer(this);") == 1
    assert "autoSyncMirrorsIfRelayHealthy" in mirror_timer
    assert "performRelaySync" not in mirror_timer

    periodic = repos[
        repos.index("void MainWindow::autoSyncMirrors()")
        : repos.index("void MainWindow::syncMirrorsBehindRoster()")
    ]
    roster = repos[
        repos.index("void MainWindow::syncMirrorsBehindRoster()")
        : repos.index("void MainWindow::propagateRepoUpdate(int index)")
    ]
    peer = repos[
        repos.index("void MainWindow::onPeerMirrorUpdated(")
        : repos.index("void MainWindow::onPeerMirrorSynced(")
    ]
    assert "!m_syncingRepos.contains(i)" in periodic
    assert "m_syncingRepos.contains(i)" in roster
    assert "syncRepository(i, /*quiet=*/true);" in roster
    assert "if (!m_syncingRepos.contains(matchIndex))" in peer
    assert "syncRepository(matchIndex, /*quiet=*/true);" in peer


def test_release_dialog_can_prune_previous_artifacts_after_publish():
    source = RELEASES.read_text(encoding="utf-8")
    prompt = source[
        source.index("void MainWindow::promptNewRelease()")
        : source.index("void MainWindow::showReleaseDetail")
    ]
    prune = source[
        source.index("void MainWindow::pruneReleaseArtifactsForCurrentRepo")
        : source.index("void MainWindow::loadReleasesPanel")
    ]
    header = HEADER.read_text(encoding="utf-8")

    assert "void pruneReleaseArtifactsForCurrentRepo(const QString &releaseTag);" in header
    assert "new QCheckBox(" in prompt
    assert "Delete previous release artifacts from this node" in prompt
    assert "forkmesh-releases/sha256" in prompt
    assert "form->addRow(QString(), pruneArtifactsCheck);" in prompt
    assert "pruneArtifactsCheck->isChecked()" in prompt
    assert "pruneReleaseArtifactsForCurrentRepo(tag);" in prompt
    assert prompt.index("runGitCapture(dir, {\"tag\", \"-a\", tag") < prompt.index(
        "pruneReleaseArtifactsForCurrentRepo(tag);"
    )

    assert "mirrorReleaseBlobs(mirrorPath)" in prune
    assert "hashDir.removeRecursively()" in prune
    assert "QLocale().formattedDataSize(bytesDeleted)" in prune
    assert "m_mirrorAdvertSig.clear();" in prune
    assert "refreshRepositoryList();" in prune
    assert "candidate.owner == repo.owner" in prune
    assert "candidate.name == repo.name" in prune
    assert "candidate.mirrorPath == mirrorPath" in prune
    assert "publishRepository(i, false);" in prune
