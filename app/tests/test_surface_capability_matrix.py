#!/usr/bin/env python3
"""Cross-surface capability and safe-deep-link contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_world_has_entry_points_from_flutter_and_dashboard():
    flutter = (
        ROOT / "mobile" / "lib" / "screens" / "home_shell.dart"
    ).read_text(encoding="utf-8")
    settings = (
        ROOT / "mobile" / "lib" / "services" / "settings_service.dart"
    ).read_text(encoding="utf-8")
    header = (
        ROOT
        / "app"
        / "public"
        / "dashboard"
        / "partials"
        / "header.html"
    ).read_text(encoding="utf-8")
    assert "label: 'World'" in flutter
    assert "LaunchMode.externalApplication" in flutter
    assert "worldUriForServerUrl" in settings
    assert "relay.userInfo.isNotEmpty" in settings
    assert 'title="Open ForkMesh World"' not in header
    assert 'href="https://world.forkmesh.com/"' not in header
