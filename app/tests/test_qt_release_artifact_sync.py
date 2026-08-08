from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "desktop" / "src" / "MainWindow.cpp"
INTERNAL = ROOT / "desktop" / "src" / "MainWindowInternal.h"
REPOS = ROOT / "desktop" / "src" / "MainWindowRepos.cpp"
RELEASES = ROOT / "desktop" / "src" / "MainWindowReleases.cpp"
HEADER = ROOT / "desktop" / "src" / "MainWindow.h"


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


def test_mirror_dropped_event_fallback_uses_the_configured_jittered_interval():
    main = MAIN.read_text(encoding="utf-8")
    internal = INTERNAL.read_text(encoding="utf-8")
    repos = REPOS.read_text(encoding="utf-8")

    assert "constexpr int kMirrorSyncIntervalDefaultMinutes = 5;" in internal
    assert "qint64 mirrorSyncIntervalMs()" in internal
    assert "constexpr int kMirrorSyncJitterPercent = 15;" in internal
    timer = main[
        main.index("void MainWindow::restartMirrorSyncTimer()")
        : main.index("MainWindow::MainWindow(QWidget *parent)")
    ]
    assert "const qint64 baseIntervalMs = mirrorSyncIntervalMs();" in timer
    assert "baseIntervalMs * kMirrorSyncJitterPercent / 100" in timer
    assert "bounded(-jitterSpanMs, jitterSpanMs + 1)" in timer
    assert "m_mirrorSyncTimer->start" in timer
    assert main.count("m_mirrorSyncTimer = new QTimer(this);") == 1
    assert "&MainWindow::autoSyncMirrorsIfRelayHealthy" in main
    assert "restartMirrorSyncTimer();" in main

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
    assert prompt.index("user.email=actions@forkmesh.local") < prompt.index(
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


def test_release_tag_sets_a_tagger_identity_without_global_git_config():
    source = RELEASES.read_text(encoding="utf-8")
    prompt = source[
        source.index("void MainWindow::promptNewRelease()")
        : source.index("void MainWindow::showReleaseDetail")
    ]

    assert '"-c", QStringLiteral("user.email=actions@forkmesh.local")' in prompt
    assert '"-c", QStringLiteral("user.name=ForkMesh Actions")' in prompt
    assert '"tag",\n                        "-a", tag, targetRef, "-m", message' in prompt


def test_releases_table_has_per_release_push_to_mirrors_action():
    source = RELEASES.read_text(encoding="utf-8")
    build = source[
        source.index("QWidget *MainWindow::buildReleasesTab()")
        : source.index("QWidget *MainWindow::buildArtifactsTab()")
    ]
    load = source[
        source.index("void MainWindow::loadReleasesPanel()")
        : source.index("void MainWindow::fetchReleaseDownloadCounts")
    ]

    assert "new QTableWidget(0, 11)" in build
    assert '"Downloads", "Mirrors", ""' in build
    assert 'new QPushButton("Push to mirrors")' in load
    assert "pushReleaseToMirrors(tag);" in load
    assert "setCellWidget(row, 9, pushMirrors)" in load
    assert "setCellWidget(row, 10, del)" in load


def test_release_push_wakes_peers_and_starts_ssh_fanout_immediately():
    source = RELEASES.read_text(encoding="utf-8")
    push = source[
        source.index("void MainWindow::pushReleaseToMirrors")
        : source.index("void MainWindow::promptNewRelease()")
    ]

    assert 'QStringLiteral("refs/tags/") + tag' in push
    assert 'tagRef + QStringLiteral("^{commit}")' in push
    assert "m_backend->notifyMirrorUpdated(" in push
    assert "propagateRepoUpdate(index);" in push
    assert "pushToSshMirrorRemotes(index, /*userInitiated=*/true, tag)" in push
    assert push.index("m_backend->notifyMirrorUpdated(") < push.index(
        "pushToSshMirrorRemotes(index, /*userInitiated=*/true, tag)"
    )
    ssh_source = REPOS.read_text(encoding="utf-8")
    ssh_push = ssh_source[
        ssh_source.index(
            "int MainWindow::pushToSshMirrorRemotes(int index, bool userInitiated"
        )
        : ssh_source.index("void MainWindow::syncRepository")
    ]
    assert "const QString pushSource = repo.localPath;" in ssh_push
    assert 'QStringLiteral("refs/heads/*:refs/heads/*")' in ssh_push
    assert 'QStringLiteral("refs/tags/*:refs/tags/*")' in ssh_push
