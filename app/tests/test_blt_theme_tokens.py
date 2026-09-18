"""BLT palette contract for dashboard theme tokens (inspo.html)."""
from pathlib import Path

PUBLIC = Path(__file__).resolve().parents[1] / "public"
DASHBOARD_SHELLS = (
    PUBLIC / "dashboard" / "index.html",
    PUBLIC / "dashboard" / "shell.html",
    PUBLIC / "dashboard" / "repo.html",
    PUBLIC / "dashboard" / "chat" / "index.html",
    PUBLIC / "dashboard" / "network" / "index.html",
    PUBLIC / "dashboard" / "notes" / "index.html",
    PUBLIC / "dashboard" / "profile" / "index.html",
    PUBLIC / "dashboard" / "profile" / "repositories" / "index.html",
    PUBLIC / "dashboard" / "repos" / "index.html",
    PUBLIC / "dashboard" / "settings" / "index.html",
    PUBLIC / "dashboard" / "tasks" / "index.html",
)

# inspo.html: red-600 / red-500, slate-950 / slate-900 / slate-800
BLT = {
    "dark_primary_rgb": "--dashboard-primary-rgb: 239 68 68;",
    "light_primary_rgb": "--dashboard-primary-rgb: 220 38 38;",
    "dark_primary_hex": "--primary: #ef4444;",
    "light_primary_hex": "--primary: #dc2626;",
    "dark_bg_rgb": "--dashboard-background-rgb: 9 9 11;",
    "dark_sidebar_rgb": "--dashboard-sidebar-rgb: 24 24 27;",
    "dark_border_rgb": "--dashboard-border-rgb: 39 39 42;",
    "dark_ring_rgb": "--dashboard-ring-rgb: 239 68 68;",
    "light_ring_rgb": "--dashboard-ring-rgb: 220 38 38;",
}


def _blocks(html: str) -> tuple[str, str]:
    dark = html[html.index('[data-dashboard-theme="dark"] {'): html.index('[data-dashboard-theme="light"] {')]
    light = html[html.index('[data-dashboard-theme="light"] {'): html.index("* {", html.index('[data-dashboard-theme="light"] {'))]
    return dark, light


def test_dashboard_shells_use_blt_primary_and_surfaces():
    for path in DASHBOARD_SHELLS:
        html = path.read_text(encoding="utf-8")
        dark, light = _blocks(html)
        assert BLT["dark_primary_rgb"] in dark, path
        assert BLT["dark_primary_hex"] in dark, path
        assert BLT["dark_bg_rgb"] in dark, path
        assert BLT["dark_sidebar_rgb"] in dark, path
        assert BLT["dark_border_rgb"] in dark, path
        assert BLT["dark_ring_rgb"] in dark, path
        assert BLT["light_primary_rgb"] in light, path
        assert BLT["light_primary_hex"] in light, path
        assert BLT["light_ring_rgb"] in light, path
        # Old ForkMesh greens must be gone from theme tokens
        assert "--dashboard-primary-rgb: 35 134 54;" not in dark
        assert "--dashboard-primary-rgb: 22 163 74;" not in light
        assert "--primary: #238636;" not in dark
        assert "--primary: #16a34a;" not in light


PRIMARY_ACTION_FILES = (
    PUBLIC / "dashboard.js",
    PUBLIC / "dashboard" / "js" / "04-account.js",
    PUBLIC / "dashboard" / "settings" / "index.html",
    PUBLIC / "dashboard" / "partials" / "views" / "settings.html",
)


def test_primary_action_hexes_are_not_forkmesh_green():
    for path in PRIMARY_ACTION_FILES:
        text = path.read_text(encoding="utf-8")
        assert "bg-[#238636]" not in text, path
        assert "hover:bg-[#2ea043]" not in text, path


def test_dashboard_theme_color_meta_matches_blt_surfaces():
    for path in DASHBOARD_SHELLS:
        html = path.read_text(encoding="utf-8")
        assert 'content="#09090b"' in html
        assert 'theme === "light" ? "#ffffff" : "#09090b"' in html
        assert "#f6f8fb" not in html
        assert "#090909" not in html
