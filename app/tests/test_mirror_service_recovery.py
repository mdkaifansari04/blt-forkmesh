from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTROL_NODE = ROOT / "desktop" / "src" / "MainWindowControlNode.cpp"


def test_app_managed_gateway_and_tunnel_restart_after_child_exit():
    source = CONTROL_NODE.read_text(encoding="utf-8")
    start = source.index("void MainWindow::startDirectMirrorServices()")
    end = source.index("void MainWindow::stopDirectMirrorServices()", start)
    body = source[start:end]

    assert '"Mirror gateway stopped (exit %1).' in body
    assert '"Cloudflare Tunnel connector stopped (exit %1).' in body
    assert body.count("QTimer::singleShot(5000, this, [this]") == 2
    assert body.count('"control/autoStartMirrorServices"') >= 2
    assert body.count("startDirectMirrorServices();") >= 3
    assert body.count("if (m_nodeOffline ||") >= 2


def test_endpoint_renewal_waits_for_fresh_loopback_health_result():
    source = CONTROL_NODE.read_text(encoding="utf-8")
    timer_start = source.index(
        "void MainWindow::ensureDirectMirrorRegistrationTimer")
    timer_end = source.index("\nvoid MainWindow::", timer_start + 10)
    timer = source[timer_start:timer_end]
    health_start = source.index(
        "void MainWindow::checkDirectMirrorGatewayHealth")
    health_end = source.index("\nvoid MainWindow::", health_start + 10)
    health = source[health_start:health_end]

    assert "checkDirectMirrorGatewayHealth();" in timer
    assert "registerDirectMirrorEndpoint();" not in timer
    assert "if (m_directMirrorGatewayHealthy)" in health
    assert "registerDirectMirrorEndpoint();" in health
