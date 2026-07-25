#!/usr/bin/env python3
"""Headless flagship mirror bootstrap contracts.

The Hosts installer can leave a headless node with a local/old repo record named
``forkmesh`` before the canonical network mirror exists. ensureFlagshipRepo must
repair that record into forkmesh/forkmesh instead of returning early, and the
mirror advert cache must notice owner/source changes so peers see the repaired
metadata immediately.
"""

import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SETUP = ROOT / "qt_client" / "src" / "MainWindowSetup.cpp"
WINDOW = ROOT / "qt_client" / "src" / "MainWindow.cpp"
SHARED = ROOT / "qt_client" / "src" / "MainWindowShared.cpp"
MAIN = ROOT / "qt_client" / "src" / "main.cpp"
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
    main = WINDOW.read_text(encoding="utf-8")
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
    assert "accountNameFromInput(savedProfileName(), QString())" in deferred_body
    assert "if (m_headless && !headlessBootstrapQueued)" in deferred_body


def test_headless_autoconnect_is_not_gated_on_a_visible_pubkey_label():
    main = WINDOW.read_text(encoding="utf-8")
    start = main.index("if (!m_profileIdentity.load())")
    end = main.index("logStartup(QStringLiteral(\"identity loaded\"))", start)
    body = main[start:end]

    assert "} else {" in body
    assert "if (m_pubkeyLabel)" in body
    assert "m_pendingSilentAuth = true;" in body
    assert body.index("if (m_pubkeyLabel)") < body.index("m_pendingSilentAuth = true;")


def test_headless_shutdown_skips_hidden_widget_tree_teardown():
    main = MAIN.read_text(encoding="utf-8")
    assert "auto *window = new MainWindow" in main
    assert "const int exitCode = app.exec();" in main
    assert "if (headless)" in main
    assert "std::_Exit(exitCode);" in main
    assert "delete window;" in main


def test_mirror_advert_cache_includes_namespace_and_publish_state():
    repos = REPOS.read_text(encoding="utf-8")
    start = repos.index("QString advertSig;")
    end = repos.index("if (advertSig != m_mirrorAdvertSig)", start)
    body = repos[start:end]

    assert "repo.owner + QLatin1Char('|') + repo.name" in body
    assert "repo.cloneUrl + QLatin1Char('|')" in body
    assert "repo.publishToNetwork ? 1 : 0" in body


def test_mirror_publish_requires_a_served_commit_and_sockets_are_retired():
    repos = REPOS.read_text(encoding="utf-8")
    assert "bool mirrorHasServedCommit(const QString &mirrorPath)" in repos
    start = repos.index("void MainWindow::startRepoHosts()")
    end = repos.index("void MainWindow::onRequestServed", start)
    start_hosts = repos[start:end]
    assert "per-repository persistent socket is retired" in start_hosts
    assert "new RepoHost" not in start_hosts

    publish_start = repos.index("void MainWindow::publishRepositoryNow")
    publish_end = repos.index("QNetworkRequest request(catalogApiUrl())", publish_start)
    publish_body = repos[publish_start:publish_end]
    assert "servedHeadCommit.isEmpty()" in publish_body
    assert "until its mirror has a served commit" in publish_body
    assert "syncRepository(index, /*quiet=*/true)" in publish_body


def test_mirror_metadata_resolves_remote_refs_like_repo_host():
    shared = SHARED.read_text(encoding="utf-8")
    start = shared.index("QString mirrorHeadBranch")
    end = shared.index("QString actionStatusText", start)
    body = shared[start:end]

    assert '"refs/remotes/"' in body
    assert "displayMirrorBranchNameForRef" in body
    assert 'QStringLiteral("refs/remotes/") + raw' in body
    assert 'raw == QLatin1String("HEAD")' in body


def test_public_browse_failover_walks_healthy_direct_https_endpoints():
    entry = ENTRY.read_text(encoding="utf-8")
    start = entry.index("async def _https_mirror_proxy")
    end = entry.index("\n\nclass Default", start)
    body = entry[start:end]

    assert "for endpoint in candidates:" in body
    assert "_https_mirror_repository_proof(" in body
    assert "status in HTTPS_MIRROR_RETRY_STATUSES" in body
    assert "_https_mirror_route_advance(" in body


def test_room_stale_sockets_are_not_counted_or_forwarded():
    entry = ENTRY.read_text(encoding="utf-8")
    assert "ROOM_CLIENT_STALE_MS = 3 * 60 * 1000" in entry
    tree = ast.parse(entry)
    room = next(
        node for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "ForkMeshRoom"
    )
    body = ast.unparse(room)

    assert "'last': int(Date.now())" in body
    assert "return len(self._live_chat_sockets(close_stale=True))" in body
    assert "def _live_chat_sockets(self, close_stale=False):" in body
    assert "self._safe_close(peer, 1001, 'stale')" in body
    assert "for peer in self._live_chat_sockets(close_stale=True):" in body


def test_public_account_row_reads_only_the_identity_tables():
    # The legacy accounts table is gone (migration 0042): a public account read
    # is a pure users/nodes lookup with no mirror-repair write behind it.
    entry = ENTRY.read_text(encoding="utf-8")
    start = entry.index("async def _account_row")
    end = entry.index("async def _owner_pubkey", start)
    body = entry[start:end]

    assert "await _account_identity_rec_by_bi(env, name_bi)" in body
    assert "_mirror_account_identity_tables" not in body
    assert "FROM accounts" not in body
