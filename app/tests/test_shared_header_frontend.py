#!/usr/bin/env python3
"""Static contracts for the shared simple page header."""

import re
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[2]
APP_PUBLIC = PROJECT / "app" / "public"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _css_declarations(css: str, selector: str) -> dict[str, str]:
    match = re.search(
        rf"(?ms)^[ \t]*{re.escape(selector)}[ \t]*\{{(?P<body>[^}}]*)\}}",
        css,
    )
    assert match, f"missing CSS rule for {selector}"
    return {
        name: value.strip()
        for name, value in re.findall(
            r"(?m)^[ \t]*([\w-]+)\s*:\s*([^;]+);",
            match.group("body"),
        )
    }


def _assert_declarations(
    css: str,
    selector: str,
    expected: dict[str, str],
) -> None:
    declarations = _css_declarations(css, selector)
    for name, value in expected.items():
        assert declarations.get(name) == value, (
            f"{selector} must set {name}: {value}; "
            f"found {declarations.get(name)!r}"
        )


def test_shared_simple_header_renderer_contains_blt_header_contract():
    js = _read(APP_PUBLIC / "site-header.js")
    css = _read(APP_PUBLIC / "site-header.css")

    for marker in (
        'querySelectorAll("[data-forkmesh-header]")',
        'aria-label="BLT home"',
        'src="/assets/blt-logo.png"',
        "fm-header-brand-text",
        ">BLT</strong>",
        'aria-label="Primary"',
        'href="/dashboard"',
        ">Dashboard</a>",
        'href="/login"',
        ">Login</a>",
        'href="/signup"',
        ">Sign up</a>",
    ):
        assert marker in js
    for marker in (
        'aria-label="ForkMesh home"',
        'src="/assets/logo.png"',
        'href="https://app.forkmesh.com/"',
        'href="/docs"',
        'href="/chat"',
        'href="/network"',
    ):
        assert marker not in js

    for marker in (
        ".forkmesh-simple-header",
        ".forkmesh-simple-header-nav",
        ".forkmesh-simple-brand",
        ".forkmesh-simple-brand-mark",
    ):
        assert marker in css


UNIVERSAL_HEADER_PAGES = (
    APP_PUBLIC / "404.html",
)


def test_universal_header_mounts_on_every_site_page():
    # One shared, session-aware header across the site. The dashboard (which
    # is also the home page: `/` redirects to it) keeps its in-app chrome.
    for page in UNIVERSAL_HEADER_PAGES:
        html = _read(page)
        assert 'href="/site-header.css"' in html, f"{page.name} missing header CSS"
        assert 'src="/site-header.js?v=' in html, f"{page.name} missing header JS"
        assert '<div data-forkmesh-header="simple"></div>' in html, page.name


def test_universal_header_is_session_aware():
    # A logged-in visitor sees their account chip (Dashboard / Profile / Log
    # out) instead of the old hardcoded Sign Up / Log In links — the reported
    # bug was /chat showing "Sign Up / Log In" to a logged-in user.
    js = _read(APP_PUBLIC / "site-header.js")

    assert 'localStorage.getItem("forkmesh.session"' in js
    assert "function buildAccountArea(" in js
    assert 'localStorage.removeItem("forkmesh.session")' in js
    # Nav may include a Dashboard link; the signed-in chip still DOM-builds labels.
    assert 'dash.textContent = "Dashboard"' in js
    assert 'profile.textContent = "Public profile"' in js
    assert 'edit.href = "/dashboard/settings"' in js
    assert 'edit.textContent = "Settings"' in js
    assert 'out.textContent = "Log out"' in js
    # Login state stays universal: a login/logout in any other open section
    # re-renders this header's account area via the storage event.
    assert 'window.addEventListener("storage"' in js
    assert 'href="/signup">Sign Up</a>' in js


def test_universal_header_organizes_blt_pages():
    js = _read(APP_PUBLIC / "site-header.js")

    for href in (
        "/dashboard",
        "/dashboard/repos",
        "/dashboard/tasks",
        "/dashboard/notes",
        "/login",
        "/signup",
        "/dashboard/settings",
        "/privacy",
        "/terms",
    ):
        assert f'href="{href}"' in js
    for href in (
        "/chat",
        "/network",
        "/pricing",
        "/desktop",
        "/mirror-payouts",
        "/leaderboards",
        "/referrals",
    ):
        assert f'href="{href}"' not in js
    assert "fm-nav-group-title" in js
    for group in ("App", "Account", "Legal"):
        assert f">{group}</span>" in js
    assert "More <" not in js
    assert 'aria-current' in js
    assert "fm-header-burger" in js
    assert "fm-header-mobile" in js
    assert "fm-header-context" in js
    assert 'src="/assets/sol.png"' not in js


def test_universal_header_uses_private_fixed_palette():
    css = _read(APP_PUBLIC / "site-header.css")

    _assert_declarations(
        css,
        ".forkmesh-simple-header",
        {
            "--fm-header-bg": "#ffffff",
            "--fm-header-surface": "#f9fafb",
            "--fm-header-fg": "#111827",
            "--fm-header-muted": "#6b7280",
            "--fm-header-border": "rgba(0, 0, 0, 0.06)",
            "--fm-header-accent": "#dc2626",
            "color": "var(--fm-header-fg)",
            "border-bottom": "1px solid var(--fm-header-border)",
        },
    )
    for selector in (".fm-header-mobile", ".fm-header-dropdown"):
        _assert_declarations(
            css,
            selector,
            {
                "background": "var(--fm-header-surface)",
                "border": "1px solid var(--fm-header-border)",
            },
        )
    _assert_declarations(
        css,
        ".forkmesh-simple-brand",
        {"color": "var(--fm-header-fg)"},
    )
    _assert_declarations(
        css,
        ".fm-nav-group-title",
        {"color": "var(--fm-header-muted)"},
    )
    _assert_declarations(
        css,
        ".fm-header-account-chip:hover",
        {"border-color": "var(--fm-header-accent)"},
    )


def test_universal_header_does_not_reference_host_page_palette():
    css = _read(APP_PUBLIC / "site-header.css")
    host_tokens = (
        "--background",
        "--bg",
        "--foreground",
        "--fg",
        "--card",
        "--surface2",
        "--muted",
        "--muted-foreground",
        "--border",
        "--accent",
    )
    inherited_references = tuple(
        token
        for token in host_tokens
        if re.search(
            rf"var\(\s*{re.escape(token)}(?:\s*,|\s*\))",
            css,
        )
    )

    assert not inherited_references, (
        f"site-header.css still inherits host page tokens: {inherited_references}"
    )


def test_universal_header_defaults_to_light_theme():
    js = _read(APP_PUBLIC / "site-header.js")

    assert 'return "light";' in js
    assert "fm-header-desktop-nav" in js
    assert 'href="/" class="forkmesh-simple-brand"' in js


def test_universal_header_signup_uses_blt_red_cta():
    css = _read(APP_PUBLIC / "site-header.css")
    expected = {
        "background": "linear-gradient(to bottom, #ef4444, #dc2626)",
        "color": "#ffffff",
    }

    _assert_declarations(css, ".fm-header-signup", expected)
    _assert_declarations(
        css,
        ".fm-header-mobile-account .fm-header-signup",
        {
            "background": "linear-gradient(to bottom, #ef4444, #dc2626)",
            "color": "#ffffff",
        },
    )
