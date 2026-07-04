#!/usr/bin/env python3
"""Static contracts for the simplified auth pages."""

from pathlib import Path

from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js


PUBLIC = Path(__file__).resolve().parents[1] / "public"
AUTH_PAGES = (
    PUBLIC / "login.html",
    PUBLIC / "signup.html",
)


def _read(page: Path) -> str:
    # dashboard.js is split into ordered public/dashboard/js/*.js fragments the
    # Worker concatenates into one /dashboard.js (see src/dashboard_bundle.py).
    if page.name == "dashboard.js":
        return assembled_dashboard_js()
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
        assert 'class="brand-mark"' in html
        assert 'src="/assets/logo.png"' in html
        assert 'aria-hidden="true"' in html


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

        assert ".row-links a" in html
        assert "color: #ffffff;" in html
        assert "color: var(--accent-bright);" not in html
        assert "background: var(--accent-bright);" not in html
        assert ".auth-button-primary { color: var(--accent-bright)" not in html
        assert ".join-eyebrow {\n        display: inline-flex; align-items: center; gap: 8px;\n        color: var(--accent-bright);" not in html


def test_login_and_signup_cross_links_remain():
    login = _read(PUBLIC / "login.html")
    signup = _read(PUBLIC / "signup.html")

    assert ('href="/signup.html"' in login) or ('href="/signup"' in login)
    assert ('href="/login.html"' in signup) or ('href="/login"' in signup)


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


def test_signup_is_single_step_email_password_name_form():
    signup = _read(PUBLIC / "signup.html")

    assert 'id="signup-form"' in signup
    assert 'id="node-name"' in signup
    assert 'id="acct-email"' in signup
    assert 'id="acct-pass"' in signup
    assert 'id="signup-create"' in signup
    assert "Join ForkMesh" in signup
    assert "Add a Solana payout address later from your dashboard profile" not in signup
    assert 'id="step-account"' not in signup
    assert 'id="name-continue"' not in signup
    assert "Name reserved" not in signup


def test_signup_posts_single_signup_request_not_payment_or_reserve_flow():
    signup_js = _read(PUBLIC / "signup.js")

    assert 'api("/api/accounts/signup"' in signup_js
    assert 'api("/api/accounts/reserve"' not in signup_js
    assert 'api("/api/accounts/donation-address"' not in signup_js
    assert "solana" not in signup_js.lower()
    assert 'localStorage.setItem("forkmesh.session", JSON.stringify({' in signup_js


def test_dashboard_profile_has_email_verification_and_payout_wallet_controls():
    # Assert against the composed dashboard shell (see _dashboard_shell).
    dashboard = assembled_dashboard()
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert "data-profile-settings-button" in dashboard
    assert "data-profile-modal" in dashboard
    assert "data-profile-email-status" in dashboard
    assert "data-profile-verify-email" in dashboard
    assert "data-profile-solana" in dashboard
    assert "data-profile-password" in dashboard
    assert "data-profile-save" in dashboard
    assert "/api/accounts/profile" in dashboard_js


def test_dashboard_profile_page_exposes_account_settings_and_danger_zone():
    dashboard = assembled_dashboard()

    assert 'data-view="profile"' in dashboard
    assert "data-profile-page-avatar" in dashboard
    assert "data-profile-page-node-name" in dashboard
    assert "data-profile-page-email" in dashboard
    assert "data-profile-page-email-status" in dashboard
    assert "data-profile-page-verify-email" in dashboard
    assert "data-profile-page-solana" in dashboard
    assert "data-profile-page-password" in dashboard
    assert "data-profile-page-save" in dashboard
    assert "data-profile-rename-input" in dashboard
    assert "data-profile-rename-status" in dashboard
    assert "data-profile-rename-save" in dashboard
    assert "data-profile-delete-password" in dashboard
    assert "data-profile-delete-confirm" in dashboard
    assert "data-profile-delete-account" in dashboard
    assert "Delete account" in dashboard
    assert "Type DELETE" in dashboard


def test_dashboard_profile_page_js_checks_availability_renames_and_deletes_account():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert "renderProfilePage(session)" in dashboard_js
    assert "checkNodeNameAvailability" in dashboard_js
    assert 'fetch(`/api/accounts/${encodeURIComponent(candidate)}`' in dashboard_js
    assert "renameNodeName" in dashboard_js
    assert "newNodeName" in dashboard_js
    assert "refreshRepositories()" in dashboard_js
    assert "deleteAccount" in dashboard_js
    assert "deleteAccount: true" in dashboard_js
    assert "localStorage.removeItem(\"forkmesh.session\")" in dashboard_js
    assert 'setSection("profile")' in dashboard_js


def test_login_explains_disabled_legacy_accounts():
    login_js = _read(PUBLIC / "login.js")

    assert 'body.error === "account_disabled"' in login_js
    assert "This account has been disabled." in login_js


def test_login_persists_returned_session_details():
    login_js = _read(PUBLIC / "login.js")

    assert 'localStorage.setItem("forkmesh.session", JSON.stringify({' in login_js
    assert "nodeName: body.nodeName" in login_js
    assert "email: body.email" in login_js
    assert "status: body.status" in login_js
    assert "pubkey: body.pubkey" in login_js
    assert "emailVerified: Boolean(body.emailVerified)" in login_js
    assert "isAdmin: Boolean(body.isAdmin)" in login_js
    assert 'solana: body.solana || ""' in login_js
    assert "hasPayoutAddress: Boolean(body.hasPayoutAddress)" in login_js


def test_login_has_forgot_password_link():
    login = _read(PUBLIC / "login.html")

    assert 'href="/forgot-password.html"' in login
    assert "Forgot" in login


def test_forgot_password_page_exists_and_links_back():
    forgot = _read(PUBLIC / "forgot-password.html")

    assert 'id="identifier"' in forgot
    assert 'id="forgot-btn"' in forgot
    assert 'src="/forgot-password.js' in forgot
    assert 'href="/login.html"' in forgot


def test_forgot_password_js_posts_to_api():
    forgot_js = _read(PUBLIC / "forgot-password.js")

    assert 'fetch("/api/accounts/forgot-password"' in forgot_js
    assert '"POST"' in forgot_js
    assert "identifier" in forgot_js


def test_reset_password_page_exists():
    reset = _read(PUBLIC / "reset-password.html")

    assert 'id="password"' in reset
    assert 'id="confirm"' in reset
    assert 'id="reset-btn"' in reset
    assert 'src="/reset-password.js' in reset
    assert 'href="/login.html"' in reset


def test_reset_password_js_posts_to_api_and_redirects():
    reset_js = _read(PUBLIC / "reset-password.js")

    assert 'fetch("/api/accounts/reset-password"' in reset_js
    assert '"POST"' in reset_js
    assert "node" in reset_js
    assert "token" in reset_js
    assert "exp" in reset_js
    assert 'location.href = "/login.html"' in reset_js


def test_worker_handles_forgot_and_reset_password_routes():
    entry = (PUBLIC.parent / "src" / "entry.py").read_text(encoding="utf-8")

    assert '"/api/accounts/forgot-password"' in entry
    assert "_account_forgot_password" in entry
    assert '"/api/accounts/reset-password"' in entry
    assert "_account_reset_password" in entry


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
    # Demo login lands on the dashboard unless a ?next= bounce (e.g. a pending
    # link-node grant, adhoc #120) asked to resume somewhere specific.
    assert 'location.href = nextPath() || "/dashboard"' in login_js
    assert '"/dashboard.html"' not in login_js
    assert 'nodeName: "demo-node"' in login_js
