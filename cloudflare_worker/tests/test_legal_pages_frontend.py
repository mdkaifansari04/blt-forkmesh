#!/usr/bin/env python3
"""Static contracts for terms/privacy pages."""

from html.parser import HTMLParser
from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
LEGAL_PAGES = (
    PUBLIC / "terms.html",
    PUBLIC / "privacy.html",
)


class PrimaryNavParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self._in_nav = False
        self._depth = 0
        self.links = []

    def handle_starttag(self, tag, attrs):
        attr_map = dict(attrs)
        if tag == "nav" and attr_map.get("aria-label") == "Primary":
            self._in_nav = True
            self._depth = 1
            return
        if self._in_nav:
            if tag not in {"br", "hr", "img", "input", "link", "meta"}:
                self._depth += 1
            if tag == "a":
                self.links.append((attr_map.get("href"), ""))

    def handle_data(self, data):
        if self._in_nav and self.links and self.links[-1][1] == "":
            href, text = self.links[-1]
            self.links[-1] = (href, data.strip())

    def handle_endtag(self, tag):
        if not self._in_nav:
            return
        if tag not in {"br", "hr", "img", "input", "link", "meta"}:
            self._depth -= 1
        if self._depth <= 0:
            self._in_nav = False


def _read(page: Path) -> str:
    return page.read_text(encoding="utf-8")


def test_legal_pages_use_404_style_simple_nav_only_docs_blog_login():
    for page in LEGAL_PAGES:
        html = _read(page)
        parser = PrimaryNavParser()
        parser.feed(html)

        assert parser.links == [("/docs", "Docs"), ("/blogs", "Blog"), ("/login", "Login")]
        assert 'class="site-header"' not in html
        assert 'class="global-nav"' not in html
        assert 'id="theme-toggle"' not in html
        assert 'class="mesh-bg"' in html
        assert 'class="mesh-grid"' in html
        assert 'class="legal-card' in html


def test_legal_pages_include_landing_footer_markup():
    for page in LEGAL_PAGES:
        html = _read(page)

        for marker in (
            'class="site-footer-word-wrap',
            'class="site-footer-panel',
            'class="site-footer-links-grid',
            'class="site-footer-brand',
            'class="site-footer-copyright',
            'src="/assets/logo.png"',
            '© 2026 ForkMesh. Built for local-first Git collaboration.',
        ):
            assert marker in html


def test_legal_pages_preserve_core_content_and_cross_links():
    terms = _read(PUBLIC / "terms.html")
    privacy = _read(PUBLIC / "privacy.html")

    assert "ForkMesh Terms" in terms
    assert "No warranty" in terms
    assert 'href="/privacy"' in terms
    assert "ForkMesh Privacy" in privacy
    assert "Install diagnostics" in privacy
    assert 'href="/terms"' in privacy


if __name__ == "__main__":
    test_legal_pages_use_404_style_simple_nav_only_docs_blog_login()
    test_legal_pages_include_landing_footer_markup()
    test_legal_pages_preserve_core_content_and_cross_links()
