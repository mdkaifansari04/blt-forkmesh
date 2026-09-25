#!/usr/bin/env python3
"""Static contract tests for the public 404 page."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
NOT_FOUND_PAGE = PUBLIC / "404.html"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_404_page_exists_and_uses_homepage_inspired_shell():
    html = _read(NOT_FOUND_PAGE)

    for marker in (
        "Page not found - ForkMesh",
        'class="mesh-bg"',
        'class="mesh-grid"',
        'class="status-card',
        "404 / route not found",
        "This node does not have that page.",
        'class="footer-word',
        ">forkmesh<",
    ):
        assert marker in html


def test_404_page_keeps_homepage_brand_and_navigation_escape_routes():
    # Brand + Docs/Blog/Login now come from the universal session-aware header
    # (site-header.js renders them at runtime); the in-body escape buttons stay.
    html = _read(NOT_FOUND_PAGE)

    for marker in (
        'href="/site-header.css"',
        'src="/site-header.js?v=',
        '<div data-forkmesh-header="simple"></div>',
        'href="/"',
        "Back home",
        "Read docs",
        "View blog",
    ):
        assert marker in html


if __name__ == "__main__":
    test_404_page_exists_and_uses_homepage_inspired_shell()
    test_404_page_keeps_homepage_brand_and_navigation_escape_routes()
