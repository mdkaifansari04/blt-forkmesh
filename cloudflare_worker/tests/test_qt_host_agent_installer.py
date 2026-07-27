"""Contracts for the Hosts-tab Claude Code/Codex mirror installer."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHAT = (ROOT / "qt_client/src/MainWindowChat.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "qt_client/src/MainWindow.h").read_text(encoding="utf-8")


def test_hosts_table_has_a_selected_host_agent_cli_install_action():
    assert "hostInstallAgentClisButton" in CHAT
    assert "Install Claude + Codex" in CHAT
    assert "installAgentClisForHost(i)" in CHAT
    assert "void MainWindow::installAgentClisForHost(int row)" in CHAT
    assert "m_hostAgentInstallProcess" in HEADER


def test_agent_install_uses_pinned_ssh_and_official_user_scoped_installers():
    installer = CHAT.split(
        "void MainWindow::installAgentClisForHost(int row)", 1
    )[1][:9000]
    assert "forkmesh::control::buildHostSshCommand" in installer
    assert "savedHostIdentityFile(node, ip, user)" in installer
    assert "https://claude.ai/install.sh" in installer
    assert "https://chatgpt.com/codex/install.sh" in installer
    assert "claude --version" in installer
    assert "codex --version" in installer
    assert "does not copy tokens" in CHAT
    assert "Provider login is still required" in installer
