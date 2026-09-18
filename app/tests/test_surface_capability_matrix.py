#!/usr/bin/env python3
"""Cross-surface capability and safe-deep-link contracts."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATRIX_PATH = ROOT / "www" / "docs" / "capability-matrix.json"
SURFACES = {"worker", "web", "world", "qt", "flutter"}
ALLOWED_MODES = {
    "native",
    "read-only",
    "deep-link",
    "api",
    "device-local",
    "not-applicable",
}


def test_capability_matrix_has_no_implicit_or_missing_surface():
    matrix = json.loads(MATRIX_PATH.read_text(encoding="utf-8"))
    assert set(matrix["surfaces"]) == SURFACES
    assert set(matrix["modes"]) == ALLOWED_MODES
    ids = [item["id"] for item in matrix["capabilities"]]
    assert len(ids) == len(set(ids))
    assert {
        "world",
        "repositories",
        "collaboration",
        "organizations",
        "mirror-control",
        "cloudflare-bootstrap",
        "agents",
        "security",
        "rewards",
        "fediverse",
        "speech-to-text",
    } <= set(ids)
    for item in matrix["capabilities"]:
        assert str(item["canonicalPath"]).startswith("/")
        for surface in SURFACES:
            assert item[surface] in ALLOWED_MODES
        # Every product concept is represented in the World directly, as
        # authorized read-only state, or as an explicit safe portal.
        assert item["world"] in {"native", "read-only", "deep-link"}


def test_owner_device_capabilities_never_claim_browser_or_worker_secret_control():
    matrix = json.loads(MATRIX_PATH.read_text(encoding="utf-8"))
    sensitive = [
        item for item in matrix["capabilities"] if item.get("sensitiveOwnerDevice")
    ]
    assert sensitive
    for item in sensitive:
        assert item["qt"] == "device-local"
        assert item["worker"] in {"api", "not-applicable"}
        assert item["web"] in {"read-only", "deep-link", "native"}
        assert item["world"] in {"read-only", "deep-link", "native"}
        assert item.get("exception")


def test_capability_inventory_has_no_retired_quarantine_surface():
    matrix = json.loads(MATRIX_PATH.read_text(encoding="utf-8"))
    rendered = json.dumps(matrix).lower()
    for retired in ("quarantine", "visual jail", "security appeal"):
        assert retired not in rendered


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
