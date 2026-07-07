from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
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
