#!/usr/bin/env python3
"""Static contracts for the shared simple page header."""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
SIMPLE_HEADER_PAGES = (
    PUBLIC / "features.html",
    PUBLIC / "terms.html",
    PUBLIC / "careers.html",
    PUBLIC / "changelog.html",
    PUBLIC / "privacy.html",
    PUBLIC / "status.html",
    PUBLIC / "desktop.html",
)


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_shared_simple_header_mounts_on_requested_pages():
    for page in SIMPLE_HEADER_PAGES:
        html = _read(page)
        body = html[html.index("<body") :]

        assert 'href="/site-header.css"' in html, f"{page.name} missing header CSS"
        assert 'src="/site-header.js"' in html, f"{page.name} missing header JS"
        assert '<div data-forkmesh-header="simple"></div>' in body

        assert 'class="site-header"' not in body
        assert 'class="global-nav"' not in body
        assert 'aria-label="Primary"' not in body
        assert "<header" not in body


def test_shared_simple_header_renderer_contains_about_header_contract():
    js = _read(PUBLIC / "site-header.js")
    css = _read(PUBLIC / "site-header.css")

    for marker in (
        'querySelectorAll("[data-forkmesh-header]")',
        'aria-label="ForkMesh home"',
        'src="/assets/logo.png"',
        'aria-label="Primary"',
        'href="/docs"',
        ">Docs</a>",
        'href="/blog"',
        ">Blog</a>",
        'href="/login"',
        ">Login</a>",
    ):
        assert marker in js

    for marker in (
        ".forkmesh-simple-header",
        ".forkmesh-simple-header-nav",
        ".forkmesh-simple-brand",
        ".forkmesh-simple-brand-mark",
    ):
        assert marker in css


UNIVERSAL_HEADER_PAGES = SIMPLE_HEADER_PAGES + (
    PUBLIC / "about.html",
    PUBLIC / "press.html",
    PUBLIC / "network.html",
    PUBLIC / "chat.html",
    PUBLIC / "blog.html",
    PUBLIC / "pricing.html",
    PUBLIC / "login.html",
    PUBLIC / "signup.html",
    PUBLIC / "forgot-password.html",
    PUBLIC / "reset-password.html",
    PUBLIC / "mirror-payouts.html",
    PUBLIC / "outreach.html",
    PUBLIC / "security-report.html",
    PUBLIC / "404.html",
    PUBLIC / "docs.html",
    PUBLIC / "docs" / "index.html",
)


def test_universal_header_mounts_on_every_page_except_home():
    # One shared, session-aware header across the site. The home page keeps
    # its own hero header; the dashboard SPA keeps its in-app chrome. Docs
    # mounts it too, with its own search toolbar below.
    for page in UNIVERSAL_HEADER_PAGES:
        html = _read(page)
        assert 'href="/site-header.css"' in html, f"{page.name} missing header CSS"
        assert 'src="/site-header.js"' in html, f"{page.name} missing header JS"
        assert '<div data-forkmesh-header="simple"></div>' in html, page.name

    home = _read(PUBLIC / "index.html")
    assert "data-forkmesh-header" not in home


def test_universal_header_is_session_aware():
    # A logged-in visitor sees their account chip (Dashboard / Profile / Log
    # out) instead of the old hardcoded Sign Up / Log In links — the reported
    # bug was /chat showing "Sign Up / Log In" to a logged-in user.
    js = _read(PUBLIC / "site-header.js")

    assert 'localStorage.getItem("forkmesh.session"' in js
    assert "function buildAccountArea(" in js
    assert 'localStorage.removeItem("forkmesh.session")' in js
    assert ">Dashboard<" not in js  # user data is DOM-built, never innerHTML
    assert 'dash.textContent = "Dashboard"' in js
    assert 'profile.textContent = "Public profile"' in js
    assert 'edit.href = "/dashboard/settings"' in js
    assert 'edit.textContent = "Edit profile"' in js
    assert 'out.textContent = "Log out"' in js
    assert 'href="/signup">Sign Up</a>' in js


def test_universal_header_organizes_all_pages():
    js = _read(PUBLIC / "site-header.js")

    # Every site link lives in the hamburger menu, grouped and fully
    # expanded — no nested "More" submenu to open.
    for href in ("/docs", "/chat", "/network", "/pricing", "/blog", "/status",
                 "/features", "/desktop", "/about", "/changelog", "/careers",
                 "/press", "/mirror-payouts", "/security-report", "/privacy",
                 "/terms"):
        assert f'href="{href}"' in js
    assert "fm-nav-group-title" in js
    for group in ("Product", "Resources", "Community", "Company",
                  "Legal &amp; security", "Account"):
        assert f">{group}</span>" in js
    assert "More <" not in js
    # Current page highlight + hamburger menu.
    assert 'aria-current' in js
    assert "fm-header-burger" in js
    assert "fm-header-mobile" in js
    # Dashboard-chrome parity: version pill, page context, payout shortcut.
    assert "/api/version" in js
    assert "fm-header-context" in js
    assert 'src="/assets/sol.png"' in js


def test_universal_header_uses_private_fixed_palette():
    css = _read(PUBLIC / "site-header.css")

    expected = (
        "--fm-header-bg: #090909;",
        "--fm-header-surface: #141416;",
        "--fm-header-fg: #f5f5f5;",
        "--fm-header-muted: #a3a3a3;",
        "--fm-header-border: #313134;",
        "--fm-header-accent: #2ea043;",
    )
    missing = tuple(marker for marker in expected if marker not in css)

    assert not missing, f"missing fixed header palette tokens: {missing}"


def test_universal_header_uses_canonical_theme_glyphs():
    js = _read(PUBLIC / "site-header.js")

    assert 'button.textContent = light ? "☾" : "☀";' in js
    assert '>☀</button>' in js


def test_universal_header_signup_is_a_white_rounded_rectangle():
    css = _read(PUBLIC / "site-header.css")
    signup_rule = css.split(".fm-header-signup {", 1)[1].split("}", 1)[0]
    expected = ("background: #ffffff;", "border-radius: 0.375rem;")
    missing = tuple(marker for marker in expected if marker not in signup_rule)

    assert not missing, f"missing Sign Up style declarations: {missing}"


def test_blog_posts_mount_universal_header():
    # Blog posts used to carry their own mini header; they now mount the same
    # universal header as the rest of the site.
    posts = sorted((PUBLIC / "blog").glob("*/index.html"))
    assert posts
    for page in posts:
        html = _read(page)
        assert 'href="/site-header.css"' in html, f"{page} missing header CSS"
        assert 'src="/site-header.js"' in html, f"{page} missing header JS"
        assert '<div data-forkmesh-header="simple"></div>' in html, page
        assert '<header class="top">' not in html, page
        assert 'class="global-nav"' not in html, page
