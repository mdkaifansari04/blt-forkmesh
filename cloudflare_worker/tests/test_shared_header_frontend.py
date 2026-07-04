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
