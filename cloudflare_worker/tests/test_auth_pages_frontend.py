#!/usr/bin/env python3
"""Static contracts for the simplified auth pages."""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
AUTH_PAGES = (
    PUBLIC / "login.html",
    PUBLIC / "signup.html",
)


def _read(page: Path) -> str:
    return page.read_text(encoding="utf-8")


def test_auth_pages_use_simple_no_nav_shell():
    for page in AUTH_PAGES:
        html = _read(page)

        assert 'class="site-header"' not in html
        assert 'class="global-nav"' not in html
        assert 'id="theme-toggle"' not in html
        assert 'class="auth-shell"' in html
        assert 'class="auth-card"' in html


def test_auth_pages_keep_single_logo_home_link():
    for page in AUTH_PAGES:
        html = _read(page)

        assert html.count('class="brand" href="/" aria-label="ForkMesh home"') == 1
        assert '<img class="brand-mark" src="/assets/logo.png" alt="" aria-hidden="true" />' in html


def test_auth_pages_use_landing_inspired_small_controls():
    for page in AUTH_PAGES:
        html = _read(page)

        assert ".auth-button-primary" in html
        assert ".auth-input-sm" in html
        assert "height: 2.25rem;" in html
        assert "border-radius: 0.75rem;" in html


def test_auth_pages_use_white_primary_actions_and_neutral_focus():
    for page in AUTH_PAGES:
        html = _read(page)

        assert "radial-gradient(circle at top, rgba(74, 222, 128" not in html
        assert "rgba(255, 255, 255, 0.16)" in html
        assert "background: #f5f5f5;" in html
        assert "color: #050505;" in html
        assert "border-color: rgba(255, 255, 255, 0.34);" in html
        assert "box-shadow: 0 0 0 2px rgba(255, 255, 255, 0.12);" in html


def test_auth_links_are_white_not_green():
    for page in AUTH_PAGES:
        html = _read(page)

        assert ".row-links a { color: #ffffff; }" in html
        assert "color: var(--accent-bright);" not in html
        assert "background: var(--accent-bright);" not in html
        assert ".auth-button-primary { color: var(--accent-bright)" not in html
        assert ".join-eyebrow {\n        display: inline-flex; align-items: center; gap: 8px;\n        color: var(--accent-bright);" not in html


def test_login_and_signup_cross_links_remain():
    login = _read(PUBLIC / "login.html")
    signup = _read(PUBLIC / "signup.html")

    assert 'href="/signup.html"' in login
    assert 'href="/login.html"' in signup


def test_signup_card_is_centered_vertically_like_login():
    signup = _read(PUBLIC / "signup.html")

    shell_css = signup[
        signup.index(".auth-shell {")
        : signup.index(".auth-card {")
    ]

    assert "place-items: center;" in shell_css
    assert "place-items: start center;" not in shell_css
    assert "padding: 28px 16px;" in shell_css
    assert "padding: 28px 16px 56px;" not in shell_css


def test_signup_intro_uses_compact_donation_note_instead_of_stats_grid():
    signup = _read(PUBLIC / "signup.html")

    assert 'class="signup-donation-note"' in signup
    # Signup is free: the note advertises that, not a required donation amount.
    assert "Free to join" in signup
    assert "No payment required" in signup
    assert 'class="stat-grid"' not in signup
    assert 'id="stat-nodes"' not in signup
    assert 'id="stat-repos"' not in signup
    assert 'id="stat-clients"' not in signup


def test_signup_stats_do_not_render_zero_on_api_failure():
    signup_js = _read(PUBLIC / "signup.js")
    load_stats = signup_js[
        signup_js.index("async function loadStats")
        : signup_js.index("// Show exactly how a join donation is divided")
    ]

    assert 'const { ok, body } = await api("/api/network/stats");' in load_stats
    assert "if (!ok) return;" in load_stats


def test_login_persists_returned_session_details():
    login_js = _read(PUBLIC / "login.js")

    assert 'localStorage.setItem("forkmesh.session", JSON.stringify({' in login_js
    assert "nodeName: body.nodeName" in login_js
    assert "email: body.email" in login_js
    assert "status: body.status" in login_js
    assert "pubkey: body.pubkey" in login_js
    assert "emailVerified: Boolean(body.emailVerified)" in login_js
    assert "isAdmin: Boolean(body.isAdmin)" in login_js


def test_login_exposes_localhost_only_demo_credentials():
    login = _read(PUBLIC / "login.html")
    login_js = _read(PUBLIC / "login.js")

    assert 'id="demo-credentials"' in login
    assert "demo@forkmesh.local" in login
    assert "forkmesh-demo" in login
    assert 'const DEMO_EMAIL = "demo@forkmesh.local";' in login_js
    assert 'const DEMO_PASSWORD = "forkmesh-demo";' in login_js
    assert 'location.hostname === "localhost"' in login_js
    assert 'location.hostname === "127.0.0.1"' in login_js
    assert 'location.href = "/dashboard"' in login_js
    assert 'location.href = "/dashboard.html"' not in login_js
    assert 'nodeName: "demo-node"' in login_js
