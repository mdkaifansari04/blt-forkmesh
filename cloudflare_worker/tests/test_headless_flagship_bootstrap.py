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


def test_headless_flagship_bootstrap_uses_canonical_fallback_without_catalog():
    setup = SETUP.read_text(encoding="utf-8")
    start = setup.index("void MainWindow::ensureFlagshipRepo()")
    end = setup.index("bool MainWindow::ensureNodeAccount", start)
    body = setup[start:end]

    assert "QString owner = canonicalOwner" in body
    assert "QString cloneUrl = canonicalClone" in body
    assert "candidateOwner.compare(canonicalOwner" in body
    assert "m_pendingAutoOpenRepoKey = canonicalOwner + \"/forkmesh\"" in body
    assert "mirrorCatalogRepo(canonicalOwner, QStringLiteral(\"forkmesh\"), cloneUrl)" in body
    assert "if (owner.isEmpty() || cloneUrl.isEmpty())" not in body


def test_headless_flagship_bootstrap_runs_after_account_session_is_ready():
    main = (ROOT / "qt_client" / "src" / "MainWindow.cpp").read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")
    deferred_start = main.index("void MainWindow::runDeferredStartup()")
    deferred_end = main.index("void MainWindow::applyTheme()", deferred_start)
    deferred_body = main[deferred_start:deferred_end]
    start = setup.index("void MainWindow::startSession()")
    end = setup.index("void MainWindow::sendNodeHeartbeat()", start)
    start_session = setup[start:end]
    retry_start = setup.index("void MainWindow::scheduleHeadlessRegisterRetry")
    retry_end = setup.index("bool MainWindow::verifyTotpLogin", retry_start)
    retry_body = setup[retry_start:retry_end]

    assert "if (m_headless)\n                ensureFlagshipRepo();" in start_session
    assert "QTimer::singleShot(0, this, &MainWindow::ensureFlagshipRepo)" in start_session
    assert "if (m_headless)\n                            ensureFlagshipRepo();" in retry_body
    assert "Startup: checking forkmesh/forkmesh mirror bootstrap." in deferred_body
    assert "Startup: rechecking forkmesh/forkmesh mirror bootstrap." in deferred_body
    assert deferred_body.count("ensureFlagshipRepo();") >= 2


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
