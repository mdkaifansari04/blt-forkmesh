"""Cross-runtime agent owner-encryption contract regressions.

The executable Qt crypto test proves seal/open authentication.  These source
contract checks make sure the bytes produced by that helper are the bytes the
Worker accepts/stores and that no browser plaintext fallback is reintroduced.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / "cloudflare_worker" / "src" / "entry.py"
SECURITY = ROOT / "cloudflare_worker" / "src" / "security_controls.py"
QT_CRYPTO = ROOT / "qt_client" / "src" / "MirrorCrypto.cpp"
QT_AGENTS = ROOT / "qt_client" / "src" / "MainWindowAgents.cpp"
QT_SETTINGS = ROOT / "qt_client" / "src" / "MainWindowSettings.cpp"
QT_HEADLESS = ROOT / "qt_client" / "src" / "HeadlessConsole.cpp"
QT_RUNNER = ROOT / "qt_client" / "src" / "AgentRunner.cpp"
QT_AGENT_STORE = ROOT / "qt_client" / "src" / "AgentStore.cpp"
QT_CMAKE = ROOT / "qt_client" / "CMakeLists.txt"
QT_TRANSFER_SOURCE = ROOT / "qt_client" / "src" / "ClaudeAccountTransfer.cpp"
QT_TRANSFER_HEADER = ROOT / "qt_client" / "src" / "ClaudeAccountTransfer.h"
DASHBOARD_SOURCE = (
    ROOT / "cloudflare_worker" / "public" / "dashboard" / "js"
    / "06-repo-content.js"
)
DASHBOARD_BUNDLE = ROOT / "cloudflare_worker" / "public" / "dashboard.js"
DASHBOARD_HEADER = (
    ROOT / "cloudflare_worker" / "public" / "dashboard" / "partials"
    / "header.html"
)


def _section(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first)]


def test_qt_and_worker_share_complete_single_recipient_hybrid_envelope():
    qt = QT_CRYPTO.read_text(encoding="utf-8")
    security = SECURITY.read_text(encoding="utf-8")

    qt_seal = _section(
        qt,
        "QJsonObject MirrorCrypto::sealOwnerPayload(",
        "QString MirrorCrypto::ownerPayloadKeyId(",
    )
    for field in (
        '"kind"), QStringLiteral("forkmesh.owner-sealed")',
        '"x25519"',
        '"mlkem768"',
        '"nonce"',
        '"tag"',
        '"key"',
        '"body"',
        '"recipients"',
    ):
        assert field in qt_seal
    assert '"kid"), recipient.value' in qt_seal

    validator = _section(
        security,
        "def validate_owner_envelope(",
        "def owner_envelope_key_id(",
    )
    assert "len(recipients) != 1" in validator
    assert "_valid_b64url(ephemeral, 32, 32)" in validator
    assert "_valid_b64url(kem_ciphertext, 1088, 1088)" in validator
    assert "_valid_b64url(wrapped_key, 32, 32)" in validator
    assert '"recipients": [{' in validator

    assert 'kid = str(value.get("kid")' not in validator


def test_qt_posts_only_encrypted_sessions_and_locally_opens_prompts():
    qt = QT_AGENTS.read_text(encoding="utf-8")
    push = _section(
        qt,
        "void MainWindow::pushAgentSessionsForRepo(",
        "void MainWindow::scheduleAgentSessionsPush(",
    )
    assert "MirrorCrypto::sealOwnerPayload(" in push
    assert 'QStringLiteral("encryptedSessions")' in push
    body = _section(
        push,
        "const QByteArray body =",
        "const QString owner =",
    )
    assert "encryptedSessions" in body
    assert '"sessions"' not in body
    assert "m_networkAccess->post(request, body)" in push
    assert "ensureAgentE2EEControlPlane(" in push
    assert '"forkmesh.agent-session"' in push
    assert '"repositoryOwner"' in push
    assert '"repositoryName"' in push

    apply_prompt = _section(
        qt,
        "void MainWindow::applyAgentPromptsPayload(",
        "void MainWindow::deliverQueuedAgentPrompt(",
    )
    assert "MirrorCrypto::openOwnerPayload(" in apply_prompt
    assert "rememberAcceptedAgentPrompt(" in apply_prompt
    assert "acknowledgeAgentPrompts(" in apply_prompt
    assert "Rejected a relay-readable or malformed agent prompt." in apply_prompt
    assert "routedAgentId" in apply_prompt
    assert '"forkmesh.agent-prompt"' in apply_prompt
    assert "repositoryOwner" in apply_prompt
    assert "repositoryName" in apply_prompt


def test_worker_agent_boundary_has_no_silent_missing_policy_downgrade():
    worker = WORKER.read_text(encoding="utf-8")
    agents = _section(
        worker,
        "async def agents_handler(",
        "async def agents_list_handler(",
    )
    assert '"requireAgentE2EE": True' in agents
    assert 'data.get("encryptedSessions")' in agents
    assert '"error": "owner_encryption_required"' in agents
    assert "owner_envelope_key_id(envelope)" in agents

    prompt = _section(
        worker,
        "async def agents_prompt_handler(",
        "async def agents_ack_handler(",
    )
    assert '"requireAgentE2EE": True' in prompt
    assert "validate_owner_envelope" in prompt
    assert '"error": "owner_encryption_required"' in prompt


def test_browser_agent_surface_is_explicitly_owner_device_only():
    source = DASHBOARD_SOURCE.read_text(encoding="utf-8")
    bundle = DASHBOARD_BUNDLE.read_text(encoding="utf-8")
    header = DASHBOARD_HEADER.read_text(encoding="utf-8")
    for text in (source, bundle):
        assert "/agents/new/prompt" not in text
        assert "/agents/${encodeURIComponent(agentId)}/prompt" not in text
        assert "${repoApiBase(repo)}/agents/list" not in text
        assert "Owner-device encrypted agents" in text
        assert "administrators do not receive an override" in text
    assert "data-repo-agent-new-form" not in header
    assert "agent-recipient private keys stay on your device" in header
    assert "browser deliberately has no owner private key" in header


def test_provider_credentials_have_no_forkmesh_transfer_surface():
    """The old reversible-base64 account bundle must stay retired."""
    assert not QT_TRANSFER_SOURCE.exists()
    assert not QT_TRANSFER_HEADER.exists()

    cmake = QT_CMAKE.read_text(encoding="utf-8")
    settings = QT_SETTINGS.read_text(encoding="utf-8")
    headless = QT_HEADLESS.read_text(encoding="utf-8")
    assert "ClaudeAccountTransfer" not in cmake
    assert "ClaudeAccountTransfer" not in settings
    assert "forkmesh-claude-account" not in settings

    setup = _section(
        settings,
        "void MainWindow::showClaudeCodeDeviceSetup()",
        "QStringList MainWindow::headlessClaudeAuth(",
    )
    for forbidden in (
        "QFileDialog",
        "QApplication::clipboard",
        "QGuiApplication::clipboard",
        "toBase64",
        "fromBase64",
        "accessToken",
        "refreshToken",
        "API key input",
    ):
        assert forbidden not in setup
    assert "run <code>claude</code>" in setup
    assert "owner-sealed task, session-state, transcript, and" in setup
    assert "logins remain isolated " in setup
    assert "to the owner device that runs the agent" in setup

    auth = _section(
        settings,
        "QStringList MainWindow::headlessClaudeAuth(",
        "QByteArray MainWindow::effectiveAvatar()",
    )
    assert 'sub == QLatin1String("export")' in auth
    assert 'sub == QLatin1String("import")' in auth
    assert "Refused: ForkMesh does not export or import" in auth
    for forbidden in (
        "QFile(",
        "QSaveFile",
        "clipboard",
        "toBase64",
        "fromBase64",
        "accessToken",
        "refreshToken",
    ):
        assert forbidden not in auth
    assert "claude-auth export" not in headless
    assert "claude-auth import" not in headless
    assert "credential transfer is unsupported" in headless


def test_agent_workspace_serialization_excludes_live_provider_credentials():
    agents = QT_AGENTS.read_text(encoding="utf-8")
    store = QT_AGENT_STORE.read_text(encoding="utf-8")
    runner = QT_RUNNER.read_text(encoding="utf-8")

    metadata = _section(
        store,
        "QJsonObject AgentSession::toJson() const",
        "AgentSession AgentSession::fromJson(",
    )
    for forbidden in (
        '"accessToken"',
        '"refreshToken"',
        '"oauth"',
        '"apiKey"',
        '"anthropicApiKey"',
        '"openAiApiKey"',
        '"credentials"',
    ):
        assert forbidden not in metadata

    push = _section(
        agents,
        "void MainWindow::pushAgentSessionsForRepo(",
        "void MainWindow::scheduleAgentSessionsPush(",
    )
    assert '"credentialBoundary", "owner-device-only"' in push
    assert (
        '"sharedWorkspaceBoundary",\n'
        '             "owner-sealed-task-state-artifacts-only"'
    ) in push
    assert "redactProviderCredentials(" in push
    assert push.index("redactProviderCredentials(") < push.index(
        "MirrorCrypto::sealOwnerPayload("
    )
    assert '"encryptedSessions"' in push
    assert "m_networkAccess->post(request, body)" in push

    ingest = _section(
        agents,
        "void MainWindow::applyTranscriptEvent(",
        "void MainWindow::renderTranscriptForSession(",
    )
    assert "redactProviderCredentials(" in ingest
    assert ingest.index("redactProviderCredentials(") < ingest.index(
        "m_agentStore->appendEvent("
    )

    for variable in (
        "ANTHROPIC_API_KEY",
        "ANTHROPIC_AUTH_TOKEN",
        "ANTHROPIC_ADMIN_KEY",
        "CLAUDE_CODE_OAUTH_TOKEN",
        "CODEX_API_KEY",
        "OPENAI_API_KEY",
        "OPENAI_ACCESS_TOKEN",
        "OPENAI_ADMIN_KEY",
    ):
        assert f'env.remove(QStringLiteral("{variable}"))' in runner
