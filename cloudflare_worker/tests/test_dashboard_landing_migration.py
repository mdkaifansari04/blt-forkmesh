#!/usr/bin/env python3
"""Static contracts for the landing/dashboard split."""

from pathlib import Path
import tomllib


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WRANGLER = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
REDIRECTS = (PUBLIC / "_redirects").read_text(encoding="utf-8")


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_feature_landing_is_promoted_to_index_with_signed_in_redirect():
    index = _read(PUBLIC / "index.html")

    assert "Keep source code alive across the mesh" in index
    assert "forkmesh.session" in index
    assert 'location.replace("/dashboard")' in index
    assert 'location.replace("/dashboard.html")' not in index
    assert "Code hosting that lives on the network." not in index


def test_dashboard_exposes_live_hydration_targets():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")

    assert dashboard == _read(PUBLIC / "dashboard.html")
    assert 'src="/dashboard.js"' in dashboard
    assert 'src="/dashboard-chat.js"' in dashboard
    for marker in (
        "data-dashboard-profile-name",
        "data-sidebar-repo-list",
        "data-sidebar-repo-count",
        "data-logout-button",
        "data-repo-count",
        "data-network-node-list",
        "data-network-summary",
        "data-network-rail-summary",
    ):
        assert marker in dashboard

    for placeholder in (
        'data-repo="you/meshcore"',
        'data-repo="relay-eu-1/relay-monitor"',
        "1,204",
    ):
        assert placeholder not in dashboard


def test_dashboard_hydrator_uses_existing_worker_apis():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for endpoint in (
        "/api/repositories",
        "/api/network/stats",
        "/api/network/leaderboards",
        "/api/network/online-history",
    ):
        assert endpoint in dashboard_js

    assert "forkmesh.session" in dashboard_js
    assert "renderRepositories" in dashboard_js
    assert "renderNetwork" in dashboard_js
    assert "requestedRepoKey()" in dashboard_js
    assert "if (!requested)" in dashboard_js
    assert 'renderProfile(session || { nodeName: "guest" })' in dashboard_js
    assert "localStorage.removeItem(\"forkmesh.session\")" in dashboard_js
    assert "forkmesh_session=; Path=/; Max-Age=0" in dashboard_js


def test_dashboard_defaults_to_repositories_and_hides_unready_home_desktop_nav():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")

    assert '<!-- Home dashboard tab is intentionally hidden' in dashboard
    assert '<!-- Desktop Client is intentionally hidden' in dashboard
    assert 'data-section="repos" data-nav-link aria-current="page"' in dashboard
    visible = _strip_html_comments(dashboard)
    assert 'data-view="repos" class="view active' in visible
    assert 'data-view="home" class="view active' not in visible
    rendered_nav = dashboard[
        dashboard.index('<nav data-top-nav')
        : dashboard.index('</nav>', dashboard.index('<nav data-top-nav'))
    ]
    visible_nav = rendered_nav.replace(
        rendered_nav[rendered_nav.index('<!-- Home dashboard tab'):rendered_nav.index('-->', rendered_nav.index('<!-- Home dashboard tab')) + 3],
        '',
    ).replace(
        rendered_nav[rendered_nav.index('<!-- Desktop Client'):rendered_nav.index('-->', rendered_nav.index('<!-- Desktop Client')) + 3],
        '',
    )
    assert "Home" not in visible_nav
    assert "Desktop Client" not in visible_nav


def _strip_html_comments(html: str) -> str:
    while "<!--" in html:
        start = html.index("<!--")
        end = html.index("-->", start) + 3
        html = html[:start] + html[end:]
    return html


def test_dashboard_repository_detail_keeps_code_comments_issues_shell():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    dashboard_js = _read(PUBLIC / "dashboard.js")

    for marker in (
        'data-view="repos"',
        'data-view="explore"',
        'data-repo-detail',
        'id="dashboard-repo-feature-template"',
        'data-state="open"',
        'data-state="closed"',
        'data-state="all"',
    ):
        assert marker in dashboard
    for marker in (
        "function repositoryCard",
        "function renderRepoDetail",
        "loadRepositoryTree(repo",
        "loadRepositoryBlob(repo",
        "loadRepoCollection(repo, \"issues\"",
        "loadRepoCollection(repo, \"pulls\"",
        "loadRepoCollection(repo, \"discussions\"",
        "loadRepoMirrors(repo)",
        "Copy clone",
        "Open clean URL",
        "data-dashboard-repo-tab=\"${tab}\"",
        '"code", "issues", "pulls", "discussions", "mirrors"',
    ):
        assert marker in dashboard_js


def test_dashboard_network_chat_uses_real_room_integration_without_mock_messages():
    dashboard = _read(PUBLIC / "dashboard" / "index.html")
    chat_js = _read(PUBLIC / "dashboard-chat.js")
    visible = _strip_html_comments(dashboard)

    assert 'src="/dashboard-chat.js"' in dashboard
    assert 'CHAT_WS_PATH = "/api/repo/mainnode/forkmesh/rooms/general/ws"' in chat_js
    assert 'ROOM_PASSPHRASE = "forkmesh-shared-room-key-v1"' in chat_js
    assert "deriveRoomKey" in chat_js
    assert "encryptObject" in chat_js
    assert "decryptObject" in chat_js
    assert "WebSocket" in chat_js
    assert "Chat mock data removed" not in dashboard
    assert "const fullMessages" not in dashboard
    assert "const sideMessages" not in dashboard
    assert "notificationToggle" not in visible
    assert "notificationModal" not in visible
    assert "data-notification-count" not in visible
    for mock in (
        "alice-node",
        "relay-eu-1",
        "devbox-kr",
        "sysop-eu",
        "morning - anyone else seeing",
        "Cloudflare had a blip",
    ):
        assert mock not in visible
        assert mock not in chat_js


def test_clean_marketing_routes_target_static_pages():
    for redirect in (
        "/dashboard /dashboard/index.html 200",
        "/dashboard.html /dashboard 308",
        "/desktop /desktop.html 200",
        "/docs /docs/index.html 200",
        "/blog /blogs 308",
        "/:owner/:repo /dashboard?repo=:owner/:repo 308",
        "/:owner/:repo/tree/:splat /dashboard?repo=:owner/:repo 308",
        "/:owner/:repo/blob/:splat /dashboard?repo=:owner/:repo 308",
    ):
        assert redirect in REDIRECTS

    run_worker_first = WRANGLER["assets"]["run_worker_first"]
    for route in ("/blog", "/docs"):
        assert route in run_worker_first
    for route in ("/desktop", "/blogs"):
        assert route not in run_worker_first


def test_desktop_client_stub_exists():
    html = _read(PUBLIC / "desktop.html")

    assert "<title>Desktop Client" in html


def test_homepage_network_selector_hides_mobile_beam_and_bars():
    index = _read(PUBLIC / "index.html")
    network = index[
        index.index('<section\n        id="network"')
        : index.index('<section\n        id="solution"')
    ]

    assert "hidden lg:block" in network
    assert "ForkMesh interactive node selector" in network
    assert "top-[-0.5px] h-px" in network


def test_homepage_mobile_mockups_do_not_overflow_by_default():
    index = _read(PUBLIC / "index.html")

    assert "-mr-56" not in index
    assert "relative top-6 w-full" in index
    assert "relative -right-8 top-11" not in index
    assert ".fm-tabs .fm-tab:nth-child(n + 6)" in index
