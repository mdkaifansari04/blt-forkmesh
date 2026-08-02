from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
SCENE = (PUBLIC / "world" / "world-scene.js").read_text(encoding="utf-8")
WORLD = (PUBLIC / "world" / "world.js").read_text(encoding="utf-8")
PAGE = (PUBLIC / "leaderboards.html").read_text(encoding="utf-8")
CLIENT = (PUBLIC / "leaderboards.js").read_text(encoding="utf-8")
SCHEMA = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
QT_ROOT = ROOT.parent / "qt_client" / "src"


def _wallet_leaderboard_body():
    return ENTRY.split("async def wallet_leaderboard", 1)[1].split(
        "async def leaderboards_overview", 1)[0]


def test_shared_public_endpoint_exposes_every_board():
    assert '"/api/leaderboards"' in ENTRY
    assert "await asyncio.gather(" in ENTRY.split(
        "async def leaderboards_overview", 1
    )[1].split("async def prune_site_referrers", 1)[0]
    for board_id in (
        "activity",
        "uptime",
        "node-storage",
        "repos",
        "mirrors",
        "hosted",
        "largest",
        "data-hosted",
        "contributors",
        "referrals",
        "referring-sites",
        "wallets",
        "funds-mainnodes",
        "funds-contributors",
        "funds-projects",
    ):
        assert f'"{board_id}"' in ENTRY


def test_member_sol_wallet_board_ranks_published_addresses_only():
    body = _wallet_leaderboard_body()
    # Only public, active, non-private user profiles carrying a valid address.
    assert 'rec.get("profile_private")' in body
    assert '_account_kind(rec) != "user"' in body
    assert 'rec.get("status") != "active"' in body
    assert 'SOLANA_RE.match(wallet)' in body
    # Balance is the lowest-level public read, ranked highest first.
    assert "_solana_balance_lamports(env, member[\"wallet\"])" in body
    assert 'board.sort(key=lambda entry: (-entry["lamports"], entry["name"]))' in body
    # Bounded on-chain work: an edge-cached response plus a rotating refresh
    # slice, with the un-refreshed remainder reported rather than hidden.
    assert "WALLET_LEADERBOARD_CACHE_KEY" in body
    assert "WALLET_BALANCE_REFRESH_PER_REBUILD" in body
    assert '"refreshed": len(refresh)' in body
    # A failed RPC keeps the last stored reading instead of publishing zero.
    assert "if lamports is None:\n            continue" in body
    assert '"/api/leaderboards/wallets"' in ENTRY
    assert "CREATE TABLE IF NOT EXISTS wallet_balances" in SCHEMA
    assert (
        "CREATE TABLE IF NOT EXISTS wallet_balances"
        in (ROOT / "migrations" / "0118_wallet_balances.sql").read_text(
            encoding="utf-8")
    )


def test_member_sol_wallet_board_reaches_the_world_and_the_hub():
    assert 'id: "wallets",' in SCENE
    assert 'title: "MEMBER SOL WALLETS",' in SCENE
    assert 'board.id === "wallets"' in CLIENT


def test_website_has_a_searchable_filterable_leaderboard_hub():
    assert "<title>Leaderboards · ForkMesh</title>" in PAGE
    assert "data-leaderboards-grid" in PAGE
    assert "data-leaderboards-search" in PAGE
    for category in ("all", "network", "repositories", "community", "historical"):
        assert f'data-category="{category}"' in PAGE
    assert 'fetch("/api/leaderboards"' in CLIENT
    assert "formatValue(board, row)" in CLIENT
    assert "/leaderboards /leaderboards.html 200" in (
        PUBLIC / "_redirects"
    ).read_text(encoding="utf-8")


def test_world_has_the_full_connected_leaderboard_district():
    assert 'fetchJSON("/api/leaderboards"' in WORLD
    assert "this.world.updateLeaderboards" in WORLD
    assert "const WORLD_LEADERBOARD_BOARD_STUBS" in SCENE
    assert 'leaderboardSuperPanel.name = "forkmesh-leaderboard-super-panel"' in SCENE
    assert 'continuousCityLand.name = "forkmesh-continuous-city-land"' in SCENE
    assert 'leaderboardConnection.name = "forkmesh-leaderboard-island-connection"' in SCENE
    assert 'leaderboardPromenade.name = "forkmesh-leaderboard-promenade"' in SCENE
    assert "function leaderboardGridTexture(THREE, state = {})" in SCENE
    assert '"5 × 5 LIVE GRID · EACH CATEGORY LISTS MEMBERS OR NODES VERTICALLY"' in SCENE
    assert "new THREE.BoxGeometry(23, 23, 0.45)" in SCENE
    assert "new THREE.PlaneGeometry(22.4, 22.4)" in SCENE
    assert "new THREE.BoxGeometry(58, 4.2, 0.35)" not in SCENE
    assert "leaderboardSuperPanel.position.set(-42, 0.22, 0)" in SCENE
    assert "footing.position.set(x, 0.25, 0)" in SCENE
    assert 'leaderboardGridFace.userData.interactive = "leaderboard-grid"' in SCENE
    assert '"forkmesh-leaderboard-ring-walk"' not in SCENE
    physical = SCENE.split(
        "// One square 5×5 wall preserves", 1
    )[1].split("let leaderboardGridKey", 1)[0]
    assert "makeActiveLeaderboardSign" not in physical
    assert "makeReferralLeaderboardSign" not in physical
    assert "makeSiteReferrerLeaderboardSign" not in physical
    assert "makeLeaderboardStatSign" not in physical
    assert "function updateLeaderboards(boards = [])" in SCENE
    assert "updateLeaderboards," in SCENE


def test_qt_has_no_leaderboard_navigation_or_network_fetch():
    sources = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in QT_ROOT.glob("*")
        if path.suffix in {".cpp", ".h"}
    )
    assert "m_leaderboardNavButton" not in sources
    assert "buildLeaderboardsSection" not in sources
    assert 'QStringLiteral("Leaderboards")' not in sources
    assert "/api/network/leaderboards" not in sources
