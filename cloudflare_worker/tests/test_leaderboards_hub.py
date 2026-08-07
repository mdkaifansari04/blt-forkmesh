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
    overview = ENTRY.split(
        "async def leaderboards_overview", 1
    )[1].split("async def prune_site_referrers", 1)[0]
    # One source at a time. Gathering the five put five Pyodide tasks in
    # flight inside one request, and a re-entered task wedges the isolate into
    # answering 1101 for every later request (see
    # tests/test_worker_task_concurrency.py). Each source keeps its own cache,
    # so the endpoint stays cheap despite the sequential reads.
    assert "asyncio.gather(" not in overview
    assert "result = await source()" in overview
    # A single failing board still must not take the hub down (adhoc #225).
    assert "degraded.append(name)" in overview
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
    assert 'continuousCityLand.name = "forkmesh-continuous-city-land"' in SCENE
    assert 'leaderboardConnection.name = "forkmesh-leaderboard-island-connection"' in SCENE
    assert 'leaderboardPromenade.name = "forkmesh-leaderboard-promenade"' in SCENE
    assert "new THREE.BoxGeometry(58, 4.2, 0.35)" not in SCENE
    assert '"forkmesh-leaderboard-ring-walk"' not in SCENE
    # One card per board, on the district's own leaderboard circle.
    physical = SCENE.split(
        "// Every public ranking is a card of its own again", 1
    )[1].split("function repaintActiveLeaderboardCard", 1)[0]
    assert "makeActiveLeaderboardSign" in physical
    assert "makeReferralLeaderboardSign" in physical
    assert "makeSiteReferrerLeaderboardSign" in physical
    assert "makeLeaderboardStatSign(THREE, board)" in physical
    assert 'circle: "leaderboards",' in physical
    assert 'face.userData.interactive = "leaderboard-card"' in physical
    assert "face.userData.boardId = id" in physical
    # The 5x5 wall that replaced the cards, and its packed texture, are gone.
    assert "leaderboardSuperPanel" not in SCENE
    assert "leaderboardGridTexture" not in SCENE
    assert "5 × 5 LIVE GRID" not in SCENE
    assert "function updateLeaderboards(boards = [])" in SCENE
    assert "updateLeaderboards," in SCENE


def test_the_shared_stat_painter_leaves_the_three_richer_cards_alone():
    # /api/leaderboards ships activity/referrals/referring-sites too. Those
    # three faces are painted by their own textures (live world activity, the
    # viewer's own referral link, redacted referrer URLs), so letting the
    # shared statistic painter also claim them would make the two repaints
    # fight over one face on every snapshot.
    assert "const LEADERBOARD_CARDS_WITH_OWN_PAINTER = new Set([" in SCENE
    guard = SCENE.split("const LEADERBOARD_CARDS_WITH_OWN_PAINTER = new Set([", 1)[1]
    guard = guard.split("]);", 1)[0]
    for board_id in ("active-members", "referrals", "http-referrers"):
        assert f'"{board_id}",' in guard
    update = SCENE.split("function updateLeaderboards(boards = [])", 1)[1]
    update = update.split("function updateReferralLeaderboard", 1)[0]
    assert "if (LEADERBOARD_CARDS_WITH_OWN_PAINTER.has(id)) return;" in update
    assert "leaderboardStatTexture(THREE, board)" in update


def test_the_leaderboard_circle_only_fetches_when_a_visitor_walks_onto_it():
    # Nothing on the circle is read at boot; the approach callback is the only
    # thing that starts a fetch, and each card spins while it is open.
    assert "void this.loadReferralLeaderboard();\n      void this.loadLobbyLinkBoard();" not in WORLD
    assert "startLeaderboardCircleWatch({ refetch = true } = {}) {" in WORLD
    assert "if (refetch) void this.loadReferralLeaderboard();" in WORLD
    assert 'onLeaderboardCircleNearby: ({ refetch } = {}) =>' in WORLD
    assert 'this.world?.setBillboardRefreshing?.("leaderboards", true);' in WORLD
    assert 'this.world?.setBillboardRefreshing?.("leaderboards", false);' in WORLD
    assert "onEnter: (refetch) => onLeaderboardCircleNearby({ refetch })," in SCENE
    assert "function setBillboardRefreshing(feed, loading)" in SCENE
    assert "setBillboardRefreshing," in SCENE


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
