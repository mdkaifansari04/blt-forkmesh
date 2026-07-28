"""Contracts for the Hosts-tab Claude Code/Codex mirror installer."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHAT = (ROOT / "qt_client/src/MainWindowChat.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "qt_client/src/MainWindow.h").read_text(encoding="utf-8")
# The remote installer script itself lives in the pure control-node helper, so
# the Hosts button and the one-click Vultr flow cannot drift apart (adhoc #418).
CONTROL = (ROOT / "qt_client/src/ControlNode.cpp").read_text(encoding="utf-8")


def test_hosts_table_has_a_selected_host_agent_cli_install_action():
    assert "hostInstallAgentClisButton" in CHAT
    assert "Install Claude + Codex" in CHAT
    assert "installAgentClisForHost(i)" in CHAT
    assert "void MainWindow::installAgentClisForHost(int row)" in CHAT
    assert "m_hostAgentInstallProcess" in HEADER
    assert 'QStringLiteral("Claude"), QStringLiteral("Codex")' in CHAT


def test_agent_install_uses_pinned_ssh_and_official_user_scoped_installers():
    installer = CHAT.split(
        "void MainWindow::installAgentClisForHost(int row)", 1
    )[1][:12000]
    assert "forkmesh::control::buildHostSshCommand" in installer
    assert "savedHostIdentityFile(node, ip, user)" in installer
    assert "forkmesh::control::agentCliBootstrapRemoteCommand" in installer
    assert "does not copy tokens" in CHAT
    assert "identityFile.isEmpty() ? pass : QString()" in installer
    assert "Using the ForkMesh-managed SSH identity" in installer
    assert "SSH could not reach %1 on port 22" in installer
    assert "The mirror rejected its saved ForkMesh SSH " in installer
    assert '"key. Re-provision or replace that host key, "' in installer
    # The per-host button installs binaries only; it never copies a login.
    assert "/*copyCredentials=*/false" in installer

    script = CONTROL.split(
        "QString agentCliBootstrapRemoteCommand(bool withCredentials)", 1
    )[1][:6000]
    assert "https://claude.ai/install.sh" in script
    assert "https://chatgpt.com/codex/install.sh" in script
    assert "claude --version" in script
    assert "codex --version" in script
    # Split over two source lines, so match the half that carries the meaning.
    assert "still required on this mirror." in script


def test_agent_logins_are_copied_only_on_stdin_and_only_when_asked():
    bootstrap = CONTROL.split(
        "QByteArray buildAgentCliBootstrapPayload(", 1
    )[1][:4000]
    # Credentials are base64 sections on stdin; nothing reaches argv.
    assert "toBase64()" in bootstrap
    script = CONTROL.split(
        "QString agentCliBootstrapRemoteCommand(bool withCredentials)", 1
    )[1][:6000]
    assert 'cat > \\"$tmp\\"' in script
    assert "$agent_home/.claude/.credentials.json" in script
    assert "$agent_home/.codex/auth.json" in script
    assert "id forkmesh-node" in script
    assert "/usr/local/bin/$program" in script
    assert "$agent_home/.forkmesh/agent-env" in script
    assert "umask 077" in script
    assert "chmod 600" in script

    driver = CHAT.split("void MainWindow::runAgentCliInstall(", 1)[1][:8000]
    assert "proc->write(payload)" in driver
    assert "payload.fill('\\0')" in driver
    # Only names are logged, never a credential value.
    assert "forkmesh::control::describeAgentCliCredentials" in driver


def test_one_click_vultr_mirror_can_sign_the_new_node_in():
    assert "vultrInstallAgentClisCheck" in CHAT
    assert "m_vultrAgentClisCheck" in HEADER
    vultr = CHAT.split("void MainWindow::startVultrHostInstall(", 1)[1][:8000]
    assert "m_vultrInstallAgentClis" in vultr
    assert "runAgentCliInstall(" in vultr
    # A missing login installs the binaries anyway; it never blocks the mirror.
    assert "agentCliCredentialsAreEmpty" in vultr


def test_saved_host_key_path_fails_closed_instead_of_falling_back_to_password():
    lookup = CHAT.split(
        "QString MainWindow::savedHostIdentityFile(", 1
    )[1].split("\n// --- One-click Vultr mirror provisioning", 1)[0]
    assert "return identity;" in lookup
    assert "QFileInfo(identity).isFile()" not in lookup
    assert "silently fall back to a session password" in lookup


def test_saved_hosts_are_reprobed_until_online_with_agent_capabilities():
    for contract in (
        "void MainWindow::probeSavedHosts()",
        "void MainWindow::probeSavedHost(",
        "m_hostProbeTimer->setInterval(30000)",
        "m_hostProbesInFlight.contains(key)",
        "FORKMESH=%s CLAUDE=%s CODEX=%s",
        'probe_home=\\"$HOME\\"',
        'export PATH=\\"$probe_home/.local/bin:$probe_home/.claude/bin:$PATH\\"',
        "Provisioning \\xC2\\xB7 waiting for SSH",
        "Attention \\xC2\\xB7 SSH key rejected",
        "Online \\xC2\\xB7 ForkMesh missing",
        "\\xE2\\x9C\\x93 Installed",
        "Not installed",
        "QTimer::singleShot(0, this, &MainWindow::probeSavedHosts)",
    ):
        assert contract in CHAT
    for contract in (
        "QTimer *m_hostProbeTimer",
        "QSet<QString> m_hostProbesInFlight",
        "QHash<QString, QString> m_hostReachability",
        "QHash<QString, QString> m_hostClaudeAvailability",
        "QHash<QString, QString> m_hostCodexAvailability",
    ):
        assert contract in HEADER


def test_orphaned_headless_node_redeems_its_installer_link_code():
    setup = (
        ROOT / "qt_client/src/MainWindowSetup.cpp"
    ).read_text(encoding="utf-8")
    assert "const bool installerLinkPending" in setup
    assert (
        "hasOwnerSigningCapability(accountName) && !installerLinkPending"
        in setup
    )
    assert (
        "if (shouldHost && (!hostingReady || installerLinkPending))"
        in setup
    )
    assert (
        'if (installerLinkPending &&\n'
        '            lookup.value("owner").toString().trimmed().isEmpty())'
        in setup
    )
    assert (
        "hasOwnerSigningCapability(accountName) &&\n"
        "                        !installerLinkPending"
        in setup
    )
    assert "if (shouldHost && hostingReady)" in setup
    assert "returning headless node after every service" in setup


def test_vultr_success_waits_for_mirror_nodes_and_world_catalog():
    for contract in (
        "void MainWindow::waitForVultrMirrorPublication(",
        "/api/repo/forkmesh/forkmesh/mirrors",
        "kMaxPublicationPolls = 30",
        "mirror.value(QStringLiteral(\"integrity\"))",
        "mirror.value(QStringLiteral(\"lastSync\"))",
        "waitForVultrMirrorPublication(node, done)",
        "Verified %1 in the public Mirror nodes / World",
    ):
        assert contract in CHAT
    assert "waitForVultrMirrorPublication" in HEADER
