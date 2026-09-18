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
    # dashboard.js is built from ordered public/dashboard/js/*.js fragments.
    if page.name == "dashboard.js":
        return assembled_dashboard_js()
    return page.read_text(encoding="utf-8")


def test_auth_pages_use_landing_style_auth_shell():
    for page in AUTH_PAGES:
        html = _read(page)

        # Landing-matched sticky header, but not the old marketing global nav.
        assert 'class="site-header"' in html
        assert 'class="topbar-nav"' in html
        assert 'class="global-nav"' not in html
        assert 'id="theme-toggle"' not in html
        assert 'class="auth-shell"' in html
        assert 'class="auth-card"' in html


def test_auth_pages_keep_single_blt_home_link():
    for page in AUTH_PAGES:
        html = _read(page)

        assert html.count('class="topbar-brand" href="/" aria-label="BLT home"') == 1
        assert 'class="brand"' not in html
        assert 'src="/assets/blt-full-logo.svg"' in html
        assert 'src="/assets/logo.png"' not in html
        assert "ForkMesh World" not in html
        assert "data-forkmesh-world" not in html
        assert 'class="brand-name"' not in html
        assert "OWASP Bug Logging Tool" not in html
        # Navbar wordmark should not look like a bordered/shadowed button chip.
        topbar_img = html.split(".topbar-brand img {", 1)[1].split("}", 1)[0]
        assert "border:" not in topbar_img
        assert "box-shadow:" not in topbar_img
        assert 'class="site-header"' in html
        assert 'class="topbar-nav"' in html
        assert 'href="/signup">Get started</a>' in html or 'href="/signup" aria-current="page">Get started</a>' in html


def test_auth_pages_use_landing_inspired_small_controls():
    for page in AUTH_PAGES:
        html = _read(page)

        assert ".auth-button-primary" in html
        assert 'class="auth-input-sm"' in html
        assert "height: 2.5rem;" in html or "height: 2.6rem;" in html
        assert "border-radius: 0.55rem;" in html or "border-radius: 1rem;" in html


def test_auth_pages_use_blt_light_professional_surfaces():
    for page in AUTH_PAGES:
        html = _read(page)

        assert 'class="light"' in html
        assert 'content="light"' in html
        assert "radial-gradient(circle at top, rgba(74, 222, 128" not in html
        assert "rgba(220, 38, 38, 0.08)" in html
        assert "linear-gradient(to bottom, #ef4444, #dc2626)" in html
        assert "background: var(--blt-panel);" in html or "background: #ffffff;" in html


def test_auth_links_use_blt_red_not_forkmesh_green():
    for page in AUTH_PAGES:
        html = _read(page)

        assert ".row-links a" in html
        assert "color: var(--blt-red);" in html or "color: #dc2626;" in html
        assert "color: var(--accent-bright);" not in html
        assert "background: var(--accent-bright);" not in html
        assert ".auth-button-primary { color: var(--accent-bright)" not in html


def test_login_and_signup_cross_links_remain():
    login = _read(PUBLIC / "login.html")
    signup = _read(PUBLIC / "signup.html")

    assert 'href="/signup"' in login
    assert 'href="/login"' in signup
    assert 'href="/signup.html"' not in login
    assert 'href="/login.html"' not in signup


def test_signup_card_is_centered_vertically_like_login():
    signup = _read(PUBLIC / "signup.html")

    shell_css = signup[
        signup.index(".auth-shell {")
        : signup.index(".auth-card {")
    ]

    assert "place-items: center;" in shell_css
    assert "place-items: start center;" not in shell_css


def test_signup_is_single_step_email_password_name_form():
    signup = _read(PUBLIC / "signup.html")

    assert 'id="signup-form"' in signup
    assert 'id="node-name"' in signup
    assert 'id="acct-email"' in signup
    assert 'id="acct-pass"' in signup
    assert 'id="signup-create"' in signup
    assert "Join BLT" in signup
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
    dashboard_js = _read(PUBLIC / "dashboard.js")
    sidebar_start = dashboard.index("data-settings-sidebar")
    sidebar_end = dashboard.index("<main data-settings-main")
    settings_sidebar = dashboard[sidebar_start:sidebar_end]

    assert 'data-view="profile"' in dashboard
    assert "data-settings-layout" in dashboard
    assert "data-settings-account-header" in dashboard
    assert "data-settings-sidebar" in dashboard
    assert "data-settings-main" in dashboard
    assert "data-settings-profile-picture" in dashboard
    assert "Go to your personal profile" in dashboard
    assert "Public profile" in dashboard
    assert "Verification and payout" in settings_sidebar
    assert "Danger zone" in settings_sidebar
    for label in (
        "Accessibility",
        "Billing and licensing",
        "Emails",
        "Password and authentication",
        "SSH and GPG keys",
        "Credentials",
        "Enterprises",
        "Teams",
        "Moderation",
        "Code, planning, and automation",
        "Codespaces",
        "Packages",
        "Copilot",
    ):
        assert label not in settings_sidebar
    for section in (
        "public-profile",
        "account",
        "appearance",
        "notifications",
        "payout",
        "nodes",
        "organizations",
        "danger",
    ):
        assert f'data-settings-section-link="{section}"' in dashboard
        assert f'data-settings-section="{section}"' in dashboard
    assert "setSettingsSection(" in dashboard_js
    assert "[data-settings-section-link]" in dashboard_js
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


def test_dashboard_profile_page_exposes_public_profile_controls():
    dashboard = assembled_dashboard()
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert "data-profile-page-bio" in dashboard
    assert "data-profile-page-location" in dashboard
    assert "data-profile-page-timezone" in dashboard
    assert "data-profile-page-mastodon" in dashboard
    assert "data-profile-page-private" in dashboard
    assert "data-profile-links-editor" in dashboard
    assert "data-profile-link-label" in dashboard
    assert "data-profile-link-url" in dashboard
    assert "data-profile-txt-value" in dashboard
    assert "data-profile-public-password" not in dashboard
    assert "data-profile-public-save" in dashboard
    assert "Save public profile" in dashboard
    assert "profileBio" in dashboard_js
    assert "profileLocation" in dashboard_js
    assert "profileTimezone" in dashboard_js
    assert "mastodon" in dashboard_js
    assert "profilePrivate" in dashboard_js
    assert "profileLinks: collectProfileLinks()" in dashboard_js
    assert "savePublicProfile" in dashboard_js


def test_dashboard_profile_timezone_uses_supported_timezone_select():
    dashboard = assembled_dashboard()
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert "<select data-profile-page-timezone" in dashboard
    assert "input data-profile-page-timezone" not in dashboard
    assert "Use browser time zone" in dashboard
    assert 'Intl.supportedValuesOf("timeZone")' in dashboard_js
    assert "profileTimezoneGmtLabel" in dashboard_js
    assert "profileTimezoneGmtOffset(zone)" in dashboard_js
    assert "GMT+00:00" in dashboard_js
    assert "renderProfileTimezoneOptions(session)" in dashboard_js


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
    assert "function clearForkMeshBrowserState()" in dashboard_js
    assert "logout();" in dashboard_js
    # Reaching settings is a real navigation now; the account panels are
    # settings sub-tabs restored from the URL.
    assert 'setSettingsSection(settingsSectionFromPath(), { scroll: false })' in dashboard_js


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
    assert 'location.protocol === "https:"' in login_js
    assert "body.sessionToken &&" in login_js
    assert "window.ForkMeshAPI?.isSameSite !== false" in login_js
    assert '? "cookie"' in login_js
    assert ': body.sessionToken || ""' in login_js
    assert 'profileBio: body.profileBio || ""' in login_js
    assert 'profileAbout: body.profileAbout || body.profileReadme || ""' in login_js
    assert 'profileLocation: body.profileLocation || ""' in login_js
    assert 'profileTimezone: body.profileTimezone || ""' in login_js
    assert "profileFollowers: Number(body.followers) || 0" in login_js
    assert "profileFollowing: Number(body.following) || 0" in login_js
    assert "profileMirrorCount: Number(body.mirrorCount) || 0" in login_js
    assert 'return nextPath() || referrerPath() || "/dashboard"' in login_js
    assert "location.replace(postLoginPath())" in login_js
    assert "if (readSession())" in login_js
    assert 'credentials: "same-origin"' in login_js


def test_login_title_is_not_extra_bold():
    login = _read(PUBLIC / "login.html")
    assert "Welcome back" in login
    assert "font-weight: 600;" in login
    assert "font-weight: 800;" not in login.split(".auth-card h1 {", 1)[1].split("}", 1)[0]


def test_dashboard_refreshes_canonical_cloudflare_profile_after_login():
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert "async function hydrateCanonicalProfile(session)" in dashboard_js
    assert 'hydrateCanonicalProfile(session).then(() => loadNotifications())' in dashboard_js
    assert 'fetchJson(`/api/accounts/${encodeURIComponent(nodeName)}`' in dashboard_js
    assert "writeSession(nextSession)" in dashboard_js


def test_dashboard_public_profile_edits_do_not_prompt_for_password():
    dashboard = assembled_dashboard()
    dashboard_js = _read(PUBLIC / "dashboard.js")

    assert "data-profile-about-password" not in dashboard
    assert "data-profile-public-password" not in dashboard
    assert "Required to save about changes" not in dashboard
    assert "Required for profile changes" not in dashboard
    assert 'profilePassword("[data-profile-about-password]")' not in dashboard_js
    assert 'profilePassword("[data-profile-public-password]")' not in dashboard_js
    assert "sessionToken: state.session?.sessionToken || \"\"" in dashboard_js


def test_login_has_forgot_password_link():
    login = _read(PUBLIC / "login.html")

    assert 'href="/forgot-password"' in login
    assert "Forgot" in login


def test_forgot_password_page_exists_and_links_back():
    forgot = _read(PUBLIC / "forgot-password.html")

    assert 'id="identifier"' in forgot
    assert 'id="forgot-btn"' in forgot
    assert 'src="/forgot-password.js' in forgot
    assert 'href="/login"' in forgot


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
    assert 'href="/login"' in reset


def test_reset_password_js_posts_to_api_and_redirects():
    reset_js = _read(PUBLIC / "reset-password.js")

    assert 'fetch("/api/accounts/reset-password"' in reset_js
    assert '"POST"' in reset_js
    assert "node" in reset_js
    assert "token" in reset_js
    assert "exp" in reset_js
    assert 'location.href = "/login"' in reset_js


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
    assert 'fetch("/api/accounts/login"' in login_js
    assert "email.toLowerCase() === DEMO_EMAIL" not in login_js
    assert 'nodeName: "demo-node"' not in login_js
    assert '"/dashboard.html"' not in login_js
