"""Contracts for the fast native-only Vultr mirror bootstrap."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHAT = (ROOT / "qt_client/src/MainWindowChat.cpp").read_text(encoding="utf-8")
SETUP = (ROOT / "qt_client/src/MainWindowSetup.cpp").read_text(encoding="utf-8")
MIRROR = (ROOT / "qt_client/src/MainWindowMirrorNode.cpp").read_text(
    encoding="utf-8"
)
DEPLOY = (ROOT / "cloudflare_worker/deploy.sh").read_text(encoding="utf-8")
INSTALLER = (ROOT / "cloudflare_worker/public/install.sh").read_text(
    encoding="utf-8"
)


def test_vultr_uses_one_native_upload_instead_of_the_desktop_installer():
    driver = CHAT.split("void MainWindow::startVultrHostInstall(", 1)[1][:9000]
    builder = CHAT.split(
        "bool MainWindow::buildVultrMirrorNodeInstallCommand(", 1
    )[1].split("\nbool MainWindow::buildHostInstallCommand(", 1)[0]
    assert "kMaxInstallAttempts = 1" in driver
    assert "buildVultrMirrorNodeInstallCommand" in CHAT
    assert "__FORKMESH_GO_MIRROR_V1__" in CHAT
    assert 'QStandardPaths::findExecutable(QStringLiteral("forkmesh-mirror-node"))' in builder
    assert "forkmesh-mirror-node.service" in builder
    assert "systemctl enable --now forkmesh-mirror-node.service" in builder
    assert "Go mirror-node installed and running (no Qt/GTK packages)" in builder
    assert "install.sh" not in builder
    assert "apt-get install" in builder
    assert "qt6" not in builder.lower()
    assert "gtk" not in builder.lower().replace("no qt/gtk packages", "")


def test_native_payload_is_checksummed_linked_and_health_checked():
    builder = CHAT.split(
        "bool MainWindow::buildVultrMirrorNodeInstallCommand(", 1
    )[1].split("\nbool MainWindow::buildHostInstallCommand(", 1)[0]
    for contract in (
        "QCryptographicHash::Sha256",
        "sha256sum",
        "--init --config /etc/forkmesh/mirror-node.json",
        "--register-link-code",
        "FORKMESH LINK CODE:",
        "http://127.0.0.1:8791/healthz",
        "https://"):
        assert contract in builder


def test_update_flows_refresh_the_go_companion():
    # "Click Update to retry" must be able to heal a desktop that is missing
    # forkmesh-mirror-node: the source rebuild builds the companion with the
    # local Go toolchain and installs it beside the client, and the prebuilt
    # auto-update fetches the published companion asset by SHA-256.
    assert "buildMirrorNodeCompanion" in SETUP
    assert "./cmd/forkmesh-mirror-node" in SETUP
    assert "installMirrorNodeCompanionFile" in SETUP
    assert "installMirrorCompanionThenRelaunch" in SETUP
    # The companion asset shares the client asset's os/arch, so the auto-update
    # client slot must select by name and never install the companion as the
    # client binary.
    assert 'name.startsWith(QStringLiteral("forkmesh-mirror-node"))' in SETUP


def test_companion_lookup_includes_the_installed_bin_directory():
    # GUI launches often lack ~/.local/bin in PATH, so both companion lookups
    # must also check the installer-owned directory beside the installed client.
    builder = CHAT.split(
        "bool MainWindow::buildVultrMirrorNodeInstallCommand(", 1
    )[1].split("\nbool MainWindow::buildHostInstallCommand(", 1)[0]
    assert "runningClientExecutable()" in builder
    assert "runningClientExecutable()" in MIRROR.split(
        "QString managedMirrorNodeBinary()", 1
    )[1]


def test_signed_release_publishes_and_installs_the_go_companion():
    assert "forkmesh-mirror-node-${os}-${arch}" in DEPLOY
    assert "CGO_ENABLED=0 go build -trimpath" in DEPLOY
    assert 'publish_args+=("cloudflare_worker/$mirror_asset")' in DEPLOY
    assert 'mirror_name="forkmesh-mirror-node-${ASSET_OS}-${ASSET_ARCH}"' in INSTALLER
    assert "_install_mirror_node_binary" in INSTALLER
