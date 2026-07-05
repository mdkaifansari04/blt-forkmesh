#!/usr/bin/env python3
"""Headless flagship mirror bootstrap contracts.

The Hosts installer can leave a headless node with a local/old repo record named
``forkmesh`` before the canonical network mirror exists. ensureFlagshipRepo must
repair that record into forkmesh/forkmesh instead of returning early, and the
mirror advert cache must notice owner/source changes so peers see the repaired
metadata immediately.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SETUP = ROOT / "qt_client" / "src" / "MainWindowSetup.cpp"
REPOS = ROOT / "qt_client" / "src" / "MainWindowRepos.cpp"
ENTRY = ROOT / "cloudflare_worker" / "src" / "entry.py"


def test_headless_forkmesh_record_is_repaired_into_canonical_flagship():
    setup = SETUP.read_text(encoding="utf-8")
    start = setup.index("void MainWindow::ensureFlagshipRepo()")
    end = setup.index("bool MainWindow::ensureNodeAccount", start)
    body = setup[start:end]

    assert "const QString canonicalOwner = QStringLiteral(\"forkmesh\")" in body
    assert "const QString canonicalMirrorPath =" in body
    assert "m_headless ||" in body
    assert "repo.owner = canonicalOwner" in body
    assert "repo.cloneUrl = canonicalClone" in body
    assert "repo.localPath.clear()" in body
    assert "repo.mirrorPath = canonicalMirrorPath" in body
    assert "repo.publishToNetwork = true" in body
    assert "syncRepository(i, /*quiet=*/true)" in body
    assert "publishRepository(i, false)" in body
    assert "startRepoHosts()" in body


def test_mirror_advert_cache_includes_namespace_and_publish_state():
    repos = REPOS.read_text(encoding="utf-8")
    start = repos.index("QString advertSig;")
    end = repos.index("if (advertSig != m_mirrorAdvertSig)", start)
    body = repos[start:end]

    assert "repo.owner + QLatin1Char('|') + repo.name" in body
    assert "repo.cloneUrl + QLatin1Char('|')" in body
    assert "repo.publishToNetwork ? 1 : 0" in body


def test_public_account_row_tolerates_identity_table_repair_failure():
    entry = ENTRY.read_text(encoding="utf-8")
    start = entry.index("async def _account_row")
    end = entry.index("async def _owner_pubkey", start)
    body = entry[start:end]

    assert "try:" in body
    assert "await _mirror_account_identity_tables" in body
    assert "except Exception:" in body
    assert "accounts row" in body
