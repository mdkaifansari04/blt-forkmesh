"""Blog share drafts, aggregate reach, and BLT shared-header contracts."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
ENTRY = (ROOT / "src/entry.py").read_text(encoding="utf-8")
SCHEMA = (ROOT / "src/schema.py").read_text(encoding="utf-8")
HEADER = (PUBLIC / "site-header.js").read_text(encoding="utf-8")


def test_blog_reach_is_aggregate_only_and_referrers_are_host_only():
    migration = (
        ROOT / "migrations/0094_blog_post_metrics.sql"
    ).read_text(encoding="utf-8")
    for table in (
        "blog_post_metrics",
        "blog_post_unique_hll",
        "blog_post_referrers",
    ):
        assert table in migration
        assert table in SCHEMA
    for forbidden in ("ip_address", "user_agent", "session_id", "visitor_id"):
        assert forbidden not in migration
    assert "async def record_blog_visit" in ENTRY
    assert "world_visitor_metrics.hll_register" in ENTRY
    assert "precision=BLOG_POST_UNIQUE_PRECISION" in ENTRY
    assert "async def blog_post_metrics" in ENTRY
    assert '"uniqueViews"' in ENTRY
    assert '"referrers": board' in ENTRY
    assert "/api/blog/metrics/" in ENTRY


def test_shared_header_is_blt_focused_without_world_chrome():
    assert 'class="fm-header-app"' in HEADER
    assert 'href="/dashboard"' in HEADER
    assert "fm-header-brand-text" in HEADER
    assert 'class="fm-header-world"' not in HEADER
    assert 'href="https://world.forkmesh.com/"' not in HEADER
    assert 'href="https://app.forkmesh.com/"' not in HEADER
