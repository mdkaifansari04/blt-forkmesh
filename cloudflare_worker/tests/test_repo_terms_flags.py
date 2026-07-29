"""Repository Terms flags are explicit, audited, and visible without notes."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
SCHEMA = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
DASHBOARD = (ROOT / "public" / "dashboard.js").read_text(encoding="utf-8")
WORLD = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)


def test_terms_flag_schema_and_admin_control_are_purpose_built():
    migration = (
        ROOT / "migrations" / "0102_repo_terms_flags.sql"
    ).read_text(encoding="utf-8")
    for source in (migration, SCHEMA):
        assert "CREATE TABLE IF NOT EXISTS repo_terms_flags" in source
        assert "spam" in source
        assert "malware" in source
    assert '"repo_terms_flags",' in ENTRY
    assert 'action="set_repo_terms_flag"' in ENTRY
    assert "await encrypt_row(self.env" in ENTRY
    assert "await purge_catalog_related_caches()" in ENTRY
    assert '"admin.console_" + action' in ENTRY


def test_public_catalog_projects_only_flag_and_category():
    assert "SELECT repo_bi,category FROM repo_terms_flags WHERE active=1" in ENTRY
    assert 'rec["termsFlagged"] = bool(terms_category)' in ENTRY
    assert 'rec["termsCategory"] = terms_category' in ENTRY
    catalog_projection = ENTRY[
        ENTRY.index("async def catalog_handler"):
        ENTRY.index("# --- Release download counts")
    ]
    assert 'rec["termsNote"]' not in catalog_projection


def test_dashboard_and_world_show_visible_terms_flags():
    for contract in (
        "function repositoryTermsBadge(repo",
        "Terms of Service moderation flag",
        "Terms flag",
    ):
        assert contract in DASHBOARD
    assert "termsFlagged: repo.termsFlagged === true" in WORLD
    assert "record.termsFlagged" in SCENE
    assert "repository-terms-flag:" in SCENE
