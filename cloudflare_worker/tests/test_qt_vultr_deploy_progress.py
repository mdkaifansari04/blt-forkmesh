"""Durable six-stage Vultr mirror deployment UI contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHAT = (ROOT / "qt_client/src/MainWindowChat.cpp").read_text()
HEADER = (ROOT / "qt_client/src/MainWindow.h").read_text()


def _slice(source, start, end):
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def test_vultr_deploy_page_has_six_numbered_stages_and_live_badge():
    page = _slice(
        CHAT,
        "// --- Create a Vultr mirror",
        "// --- Live session / install output",
    )
    assert "kVultrProvisionStageCount = 6" in CHAT
    for label in (
        "Credentials",
        "Plan + image",
        "Create server",
        "Boot + connect",
        "Install",
        "Live traffic",
    ):
        assert f'QStringLiteral("{label}")' in page
    assert "Deployment stage %1" in page
    assert "vultrMirrorLiveBadge" in page
    assert "mirror is serving repository traffic" in CHAT


def test_checkpoint_is_durable_and_contains_no_credentials():
    persist = _slice(
        CHAT,
        "void MainWindow::persistVultrProvisionState",
        "void MainWindow::setVultrProvisionStage",
    )
    for field in (
        "state",
        "stage",
        "node",
        "dnsHostname",
        "instanceId",
        "ip",
        "identityFile",
        "installAgentClis",
        "hostMetadata",
        "updatedAt",
    ):
        assert f'QStringLiteral("{field}")' in persist
    assert "QSettings" in persist
    assert "settings.sync()" in persist
    for secret in (
        "apiKey",
        "cloudflareToken",
        "m_vultrTunnelApiToken",
        "VULTR_API_KEY",
    ):
        assert secret not in persist


def test_restart_restores_log_and_resumes_from_each_safe_boundary():
    restore = _slice(
        CHAT,
        "void MainWindow::restoreVultrProvision()",
        "void MainWindow::resumeVultrProvision()",
    )
    assert "vultrProvisionLogPath()" in restore
    assert "setPlainText" in restore
    assert "m_vultrResumeRequested" in restore
    assert "QTimer::singleShot(0, this, &MainWindow::resumeVultrProvision)" in restore

    resume = _slice(
        CHAT,
        "void MainWindow::resumeVultrProvision()",
        "void MainWindow::findVultrProvisionInstance",
    )
    assert "m_vultrProvisionStage >= 6" in resume
    assert "waitForVultrMirrorPublication" in resume
    assert "m_vultrProvisionStage >= 5" in resume
    assert "startVultrHostInstall" in resume
    assert "pollVultrInstance" in resume
    assert "m_vultrResumeChain = true" in resume
    assert "createVultrMirrorFromForm()" in resume


def test_restart_cannot_duplicate_a_billable_instance():
    lookup = _slice(
        CHAT,
        "void MainWindow::findVultrProvisionInstance",
        "void MainWindow::finishVultrProvision",
    )
    assert 'QStringLiteral("/v2/instances?per_page=500")' in lookup
    assert 'QStringLiteral("label")' in lookup
    assert 'QStringLiteral("hostname")' in lookup

    create = _slice(
        CHAT,
        "void MainWindow::createVultrMirrorFromForm()",
        "void MainWindow::pollVultrInstance",
    )
    lookup_at = create.index("findVultrProvisionInstance(")
    post_at = create.index('QStringLiteral("/v2/instances")', lookup_at)
    assert lookup_at < post_at
    assert "resuming instead of creating a duplicate" in create


def test_log_is_bounded_and_saved_while_deployment_runs():
    save = _slice(
        CHAT,
        "void MainWindow::saveVultrProvisionLog()",
        "void MainWindow::scheduleVultrProvisionLogSave()",
    )
    assert "1024 * 1024" in save
    assert "QSaveFile" in save
    assert "toPlainText()" in save
    append = _slice(
        CHAT,
        "void MainWindow::appendHostInstallLog",
        "void MainWindow::appendHostDeployLog",
    )
    assert "m_vultrProvisionActive" in append
    assert "scheduleVultrProvisionLogSave()" in append


def test_green_live_state_requires_real_public_traffic_health():
    verify = _slice(
        CHAT,
        "void MainWindow::waitForVultrMirrorPublication",
        "void MainWindow::vultrApiCall",
    )
    for requirement in (
        'QStringLiteral("online")',
        'QStringLiteral("ok")',
        'QStringLiteral("lastSync")',
        'QStringLiteral("cloneAvailable")',
        'QStringLiteral("endpointHealthy")',
        'QStringLiteral("endpointFresh")',
    ):
        assert requirement in verify
    assert "finishVultrProvision(true" in verify
    assert "Verification continues automatically" in verify
    assert "60000" in verify


def test_header_declares_durable_ui_and_resume_state():
    for symbol in (
        "setVultrProvisionStage",
        "persistVultrProvisionState",
        "restoreVultrProvision",
        "resumeVultrProvision",
        "findVultrProvisionInstance",
        "m_vultrStageNumbers",
        "m_vultrLiveBadge",
        "m_vultrProvisionStage",
        "m_vultrInstanceId",
        "m_vultrInstanceIp",
    ):
        assert symbol in HEADER
