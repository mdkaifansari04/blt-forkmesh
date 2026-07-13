#!/usr/bin/env python3
"""Static contract tests for the public docs page shell."""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
DOCS_PAGES = (
    PUBLIC / "docs.html",
    PUBLIC / "docs" / "index.html",
)


def _read(page: Path) -> str:
    return page.read_text(encoding="utf-8")


def test_docs_pages_use_tailwind_cdn_and_page_local_styles():
    for page in DOCS_PAGES:
        html = _read(page)

        assert "https://cdn.tailwindcss.com" in html
        assert 'href="styles.css"' not in html
        assert 'href="../styles.css"' not in html
        assert "<style>" in html


def test_docs_pages_expose_platform_tabs_with_coming_soon_states():
    for page in DOCS_PAGES:
        html = _read(page)

        assert ">Desktop<" in html
        assert ">Extension<" in html
        assert ">Mobile<" in html
        assert html.count("Coming soon") >= 2
        assert 'aria-disabled="true"' in html


def test_docs_pages_keep_reference_like_docs_shell():
    for page in DOCS_PAGES:
        html = _read(page)

        for marker in (
            'class="docs-shell',
            "Search...",
            "Copy page",
            "On this page",
            "The ForkMesh Mental Model",
            "Was this page helpful?",
            "Install ForkMesh Desktop",
        ):
            assert marker in html


def test_docs_pages_use_home_link_instead_of_github():
    for page in DOCS_PAGES:
        html = _read(page)

        # Home navigation now comes from the universal header's brand link
        # (rendered by site-header.js); the point of this test is that no
        # GitHub links crept back in.
        assert ">GitHub<" not in html
        assert "ForkMesh repository" not in html
        assert 'href="/mainnode/forkmesh"' not in html


def test_docs_pages_mount_the_universal_site_header():
    # The shared header is the sole visible theme controller.
    # The docs toolbar keeps only its search and product tabs beneath it.
    for page in DOCS_PAGES:
        html = _read(page)

        assert 'href="/site-header.css"' in html
        assert 'src="/site-header.js"' in html
        assert '<div data-forkmesh-header="simple"></div>' in html
        # The duplicated chrome is gone; docs-specific tools stay.
        assert 'aria-label="Docs header"' not in html
        assert ">Contact Us<" not in html
        assert 'id="theme-toggle"' not in html
        assert "function applyTheme" not in html
        assert "sticky top-0 z-30" in html
        assert 'id="docs-search"' in html


def test_docs_pages_use_reduced_type_scale():
    for page in DOCS_PAGES:
        html = _read(page)

        for old_size in (
            "text-[2.65rem]",
            "text-[2.35rem]",
            "text-[2rem]",
            "text-[1.7rem]",
            "text-[1.55rem]",
            "text-[1.35rem]",
            "text-[1.22rem]",
            "text-[1.15rem]",
            "text-[1.08rem]",
        ):
            assert old_size not in html

        for new_size in (
            "text-[2.25rem]",
            "text-[1.38rem]",
            "text-[1.78rem]",
            "text-[1.5rem]",
            "text-[1.1rem]",
            "text-[1.02rem]",
        ):
            assert new_size in html


def test_docs_pages_include_working_inline_search():
    for page in DOCS_PAGES:
        html = _read(page)

        for marker in (
            'id="docs-search"',
            'id="docs-search-panel"',
            'data-search-title="Repository model"',
            "function renderDocsSearchResults",
            "docsSearchIndex",
            "event.metaKey || event.ctrlKey",
        ):
            assert marker in html


def test_docs_logo_keeps_same_pixels_across_theme_modes():
    for page in DOCS_PAGES:
        html = _read(page)

        assert "html.dark .brand-mark" not in html
        assert "filter: invert" not in html


def test_docs_pages_wire_helpfulness_feedback():
    for page in DOCS_PAGES:
        html = _read(page)

        for marker in (
            "data-feedback-like",
            "data-feedback-dislike",
            "data-feedback-status",
            "data-feedback-modal",
            "data-feedback-form",
            "data-feedback-message",
            "What was confusing, missing, or could be improved?",
            'fetch("/api/feedback"',
            'source: "docs"',
            'vote: "like"',
            'vote: "dislike"',
        ):
            assert marker in html


def test_docs_pages_publish_api_and_operational_reference():
    for page in DOCS_PAGES:
        html = _read(page)

        for marker in (
            'id="api-websockets"',
            "APIs and WebSockets",
            "/api/repo/&lt;owner&gt;/&lt;repo&gt;/host",
            "/api/poll",
            'id="bounty-custody"',
            "Bounty custody and payouts",
            'id="installers-updates"',
            "Installers and updates",
            'id="developer-integrations"',
            "Developer integrations",
            'id="self-hosting"',
            "Self-hosting and development",
        ):
            assert marker in html
