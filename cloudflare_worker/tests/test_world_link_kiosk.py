"""Reach scoring, URL safety, persistence, and lobby wiring for Link Lab."""

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "forkmesh_world_link_kiosk", ROOT / "src/world_link_kiosk.py"
)
KIOSK = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(KIOSK)


def test_public_url_normalization_never_accepts_local_or_credentialed_targets():
    assert KIOSK.normalize_public_url("http://example.com/post") == ("", "")
    assert KIOSK.normalize_public_url("https://localhost/post") == ("", "")
    assert KIOSK.normalize_public_url("https://127.0.0.1/post") == ("", "")
    assert KIOSK.normalize_public_url("https://user:pass@example.com/") == ("", "")
    assert KIOSK.normalize_public_url("https://example.com:8443/") == ("", "")

    url, host = KIOSK.normalize_public_url(
        "https://Example.COM/launch?utm_source=world&token=secret#private"
    )
    assert host == "example.com"
    assert url == (
        "https://example.com/launch?"
        "utm_source=world&token=%5Bredacted%5D"
    )


def test_reach_estimate_is_bounded_monotonic_and_explainable():
    small = KIOSK.reach_estimate(10, 0, "social")
    larger = KIOSK.reach_estimate(10_000, 500, "social", True)

    assert 0 <= small["score"] < larger["score"] <= 100
    assert larger["potentialTraffic"]["high"] >= larger["potentialTraffic"]["low"]
    assert larger["factors"] == {
        "followerPoints": 60,
        "trafficPoints": 27,
        "verifiedDomainPoints": 10,
        "followers": 10_000,
        "observedAggregateVisits": 500,
        "channel": "social",
    }


def test_kiosk_is_migrated_routed_audited_and_reachable_from_the_lobby():
    migration = (
        ROOT / "migrations/0103_world_lobby_link_kiosk.sql"
    ).read_text(encoding="utf-8")
    schema = (ROOT / "src/schema.py").read_text(encoding="utf-8")
    entry = (ROOT / "src/entry.py").read_text(encoding="utf-8")
    scene = (ROOT / "public/world/world-scene.js").read_text(encoding="utf-8")
    shell = (ROOT / "public/world/world.js").read_text(encoding="utf-8")

    assert "CREATE TABLE IF NOT EXISTS world_lobby_links" in migration
    assert "UNIQUE(account_bi, url_bi)" in migration
    assert "world_lobby_links" in schema
    assert "world_link_kiosk_handler" in entry
    assert '"/api/world/link-kiosk"' in entry
    assert "world.link_kiosk_submit" in (
        ROOT / "src/world_link_kiosk.py"
    ).read_text(encoding="utf-8")
    assert 'userData.interactive = "office-link-kiosk"' in scene
    assert "onLobbyLinkKioskSelect" in scene
    assert "openLobbyLinkKiosk" in shell
    assert "never controls merges, access, rewards, or governance" in shell


def test_team_onboarding_documents_roles_review_gate_and_link_lab():
    docs = (
        ROOT / "public/docs/onboarding/index.html"
    ).read_text(encoding="utf-8")
    home = (ROOT / "public/docs/index.html").read_text(encoding="utf-8")

    for phrase in (
        "QA onboarding",
        "Developer onboarding",
        "Marketing onboarding",
        "Peer-review and merge policy",
        "Lobby Link Lab",
    ):
        assert phrase in docs
    assert "/docs/onboarding" in home
