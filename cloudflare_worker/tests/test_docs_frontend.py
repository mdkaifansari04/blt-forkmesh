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

        assert '<a class="hover:text-[var(--docs-fg)]" href="/">Home</a>' in html
        assert ">GitHub<" not in html
        assert "ForkMesh repository" not in html
        assert 'href="/mainnode/forkmesh"' not in html


def test_docs_pages_keep_homepage_logo_markup():
    for page in DOCS_PAGES:
        html = _read(page)

        assert 'class="brand inline-flex items-center gap-3 text-[1.45rem]' in html
        assert 'href="/"' in html
        assert 'aria-label="ForkMesh home"' in html
        assert 'class="brand-mark"' in html
        assert 'src="/assets/logo.png"' in html
        assert 'alt=""' in html
        assert 'aria-hidden="true"' in html


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
