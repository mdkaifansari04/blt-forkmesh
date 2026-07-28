"""Headless installs must remain capable of claiming agent work."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "cloudflare_worker" / "public" / "install.sh"
SETUP = ROOT / "qt_client" / "src" / "MainWindowSetup.cpp"


def test_installer_seeds_private_agent_checkout_before_service_start():
    script = INSTALLER.read_text(encoding="utf-8")
    seed = script.index('local flagship_checkout="$checkout_root/forkmesh"')
    launch = script.index("systemctl enable --now forkmesh-node.service")

    assert seed < launch
    assert 'git clone --quiet --branch main --single-branch' in script
    assert "items\\\\1\\\\localPath=%s" in script
    assert 'chmod 0700 "$checkout_root" "$flagship_checkout"' in script
    assert 'chown -R "$service_user:$service_user" "$state_dir"' in script


def test_headless_bootstrap_preserves_only_service_owned_checkout():
    source = SETUP.read_text(encoding="utf-8")

    assert "serviceManagedCheckout" in source
    assert "QFileInfo(QDir::homePath()).canonicalFilePath()" in source
    assert 'QStringLiteral(".git")' in source
    assert "if (!serviceManagedCheckout)" in source
