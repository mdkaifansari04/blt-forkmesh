"""Headless installs must remain capable of claiming agent work."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "cloudflare_worker" / "public" / "install.sh"
SETUP = ROOT / "qt_client" / "src" / "MainWindowSetup.cpp"
MAIN_WINDOW = ROOT / "qt_client" / "src" / "MainWindow.cpp"
CONTROL_NODE = ROOT / "qt_client" / "src" / "MainWindowControlNode.cpp"


def test_restart_cleans_only_managed_mirror_runtime_temp_directories():
    script = INSTALLER.read_text(encoding="utf-8")

    assert "cleanup_stale_service_mirror_tmp" in script
    assert "-name 'ForkMesh-*'" in script
    assert "-name 'forkmesh-mirror-runtime-*'" in script
    cleanup = script.split("cleanup_stale_service_mirror_tmp()", 1)[1]
    cleanup = cleanup.split("\n}", 1)[0]
    assert "forkmesh-agent-*" not in cleanup
    restart = script.split('if [ "$FORKMESH_RESTART" = "1" ]', 1)[1]
    assert restart.index("stop_forkmesh_daemons") < restart.index(
        "cleanup_stale_service_mirror_tmp")


def test_installer_seeds_private_agent_checkout_before_service_start():
    script = INSTALLER.read_text(encoding="utf-8")
    seed = script.index('local flagship_checkout="$checkout_root/forkmesh"')
    launch = script.index("systemctl enable --now forkmesh-node.service")

    assert seed < launch
    assert '-c pack.deltaCacheSize=16m -c pack.windowMemory=16m' in script
    assert 'clone --quiet --branch main --single-branch' in script
    assert "items\\\\1\\\\localPath=%s" in script
    assert 'chmod 0700 "$checkout_root" "$flagship_checkout"' in script
    assert 'chown -R "$service_user:$service_user" "$state_dir"' in script


def test_headless_bootstrap_preserves_only_service_owned_checkout():
    source = SETUP.read_text(encoding="utf-8")

    assert "serviceManagedCheckout" in source
    assert "QFileInfo(QDir::homePath()).canonicalFilePath()" in source
    assert 'QStringLiteral(".git")' in source
    assert "if (!serviceManagedCheckout(repo.localPath))" in source


def test_failed_headless_install_is_resumable_only_with_root_owned_marker():
    script = INSTALLER.read_text(encoding="utf-8")

    assert 'SYSTEMD_BOOTSTRAP_MARKER=' in script
    assert "forkmesh-installing-service-v1" in script
    assert 'grep -Fqx "path=$SYSTEMD_STATE_DIR" "$SYSTEMD_BOOTSTRAP_MARKER"' in script
    assert '[_path_owner_uid "$SYSTEMD_BOOTSTRAP_MARKER"' not in script
    assert '[ "$(_path_owner_uid "$SYSTEMD_BOOTSTRAP_MARKER")" = "0" ]' in script
    assert 'rm -f -- "$SYSTEMD_BOOTSTRAP_MARKER"' in script


def test_tunnel_helpers_are_executable_by_the_unprivileged_service():
    script = INSTALLER.read_text(encoding="utf-8")

    assert 'cloudflare_bootstrap.py cloudflared_install.py' in script
    assert 'chmod 0755 "$(dirname "$tools_dir")" "$tools_dir"' in script
    assert 'chmod 0755 "$cfd_dest"' in script
    assert '--mirror-public-key="$node_pubkey"' in script


def test_provisioned_mirror_services_autostart_without_opening_control_page():
    source = MAIN_WINDOW.read_text(encoding="utf-8")

    assert "&MainWindow::maybeAutoStartDirectMirrorServices" in source
    assert source.index("m_profileIdentity.load()") < source.index(
        "&MainWindow::maybeAutoStartDirectMirrorServices"
    )


def test_direct_endpoint_registration_retries_until_repository_health_is_active():
    source = CONTROL_NODE.read_text(encoding="utf-8")

    assert "{1500, 5000, 15000, 60000}" in source
    assert 'QLatin1String("active")' in source
    assert 'QStringLiteral("--protocol"), QStringLiteral("http2")' in source
