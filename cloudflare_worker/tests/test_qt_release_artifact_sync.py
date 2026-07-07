from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REPOS = ROOT / "qt_client" / "src" / "MainWindowRepos.cpp"


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
