#!/usr/bin/env python3
"""Static contract tests for the public blog pages."""

from html.parser import HTMLParser
from pathlib import Path
import tomllib


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
BLOG_PAGE = PUBLIC / "blog.html"
ENTRY_TEXT = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WRANGLER = tomllib.loads((ROOT / "wrangler.toml").read_text(encoding="utf-8"))
REDIRECTS = PUBLIC / "_redirects"


class BlogNavParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self._in_nav = False
        self._nav_depth = 0
        self.links = []

    def handle_starttag(self, tag, attrs):
        attr_map = dict(attrs)
        if tag == "nav" and attr_map.get("aria-label") == "Blog":
            self._in_nav = True
            self._nav_depth = 1
            return

        if self._in_nav:
            if tag not in {"br", "hr", "img", "input", "link", "meta"}:
                self._nav_depth += 1
            if tag == "a":
                self.links.append(attr_map)

    def handle_endtag(self, tag):
        if not self._in_nav:
            return

        if tag not in {"br", "hr", "img", "input", "link", "meta"}:
            self._nav_depth -= 1
        if self._nav_depth <= 0:
            self._in_nav = False


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _css_rule_after(html: str, selector: str) -> str:
    selector_start = html.index(selector)
    rule_start = html.index("{", selector_start)
    rule_end = html.index("}", rule_start)
    return html[rule_start:rule_end]


def test_blog_page_uses_screenshot_editorial_shell():
    html = _read(BLOG_PAGE)

    for marker in (
        'class="blog1-shell"',
        'class="blog1-hero"',
        'Our latest <span class="blog1-mono">articles</span>',
        'class="blog1-featured-image blog1-has-art"',
        'class="blog1-next"',
    ):
        assert marker in html


def test_blog_page_nav_only_links_auth():
    parser = BlogNavParser()
    parser.feed(_read(BLOG_PAGE))

    # The nav's single auth entry matches the site-wide "Sign Up / Log In"
    # label introduced by adhoc #123.
    assert [link.get("href") for link in parser.links] == ["/signup"]
    assert [link.get("class") for link in parser.links] == ["blog1-login"]


def test_blog_page_uses_homepage_logo_markup():
    html = _read(BLOG_PAGE)

    assert '<a class="brand" href="/" aria-label="ForkMesh home">' in html
    assert '<img class="brand-mark" src="/assets/logo.png" alt="" aria-hidden="true">' in html


def test_blog_page_footer_keeps_only_twitter_social_link():
    html = _read(BLOG_PAGE)

    assert 'aria-label="Twitter"' in html
    assert 'href="https://x.com/forkmesh/"' in html
    assert ("https://twitter.com/" + "forkmesh") not in html
    for removed in ("GitHub", "Discord", "LinkedIn", "YouTube"):
        assert 'aria-label="%s"' % removed not in html


def test_blog_page_articles_open_in_page_detail_view():
    html = _read(BLOG_PAGE)

    assert html.count('href="#introducing-forkmesh"') >= 2
    assert 'id="introducing-forkmesh"' in html
    assert 'class="blog1-detail"' in html
    assert "Introducing ForkMesh: code that can't be taken down" in html
    assert "open-source desktop node and relay" in html


def test_first_blog_article_uses_launch_article_content():
    html = _read(BLOG_PAGE)

    for marker in (
        "Your machine is the server.",
        "Keys, not passwords.",
        "Chat the relay cannot read.",
        "Get paid to mirror.",
        "Federated by design.",
        "Signed history in Git.",
        "AES-256-GCM",
        "forkmesh-pull-event-v1",
        "This is an MVP, and that is exciting",
    ):
        assert marker in html


def test_blog_page_detail_matches_editorial_detail_shell():
    html = _read(BLOG_PAGE)

    for marker in (
        'class="blog1-article-hero"',
        'class="blog1-article-body"',
        'class="blog1-featured-image blog1-article-image blog1-has-art"',
        'href="/blogs"',
    ):
        assert marker in html


def test_blog_page_search_opens_premium_dialog():
    html = _read(BLOG_PAGE)

    for marker in (
        'class="blog1-search-trigger"',
        'aria-haspopup="dialog"',
        'aria-controls="blog-search-modal"',
        'id="blog-search-modal"',
        'role="dialog"',
        'aria-modal="true"',
        'id="blog-search-input"',
        'data-blog-search-result',
        'data-blog-search-empty',
        "openBlogSearch",
        "filterBlogSearchResults",
    ):
        assert marker in html


def test_blog_page_uses_landing_green_accent_for_primary_art():
    html = _read(BLOG_PAGE)

    assert "--blog1-green: #3fb950;" in html
    assert "rgba(63, 185, 80" in html
    assert "--blog1-blue:" not in html


def test_blog_footer_cta_arc_uses_original_cyan():
    html = _read(BLOG_PAGE)
    arc_rule = _css_rule_after(html, ".blog1-arc")

    assert "--blog1-cyan: #76d8f6;" in html
    assert "rgba(118, 216, 246, 0.95)" in arc_rule
    assert "rgba(63, 185, 80, 0.95)" not in arc_rule


def test_blog_interaction_states_use_neutral_dark_not_green():
    html = _read(BLOG_PAGE)

    for marker in (
        "--blog1-hover-line: rgba(246, 246, 242, 0.24);",
        "--blog1-hover-fill: rgba(255, 255, 255, 0.045);",
        "--blog1-hover-ring: rgba(255, 255, 255, 0.08);",
        "--blog1-popup-sheen: rgba(255, 255, 255, 0.11);",
    ):
        assert marker in html

    for selector in (
        ".blog1-search-trigger:hover,",
        ".blog1-search-backdrop",
        ".blog1-search-panel",
        ".blog1-search-panel::before",
        ".blog1-search-close:hover,",
        ".blog1-search-field:focus-within",
        ".blog1-search-result:hover,",
    ):
        rule = _css_rule_after(html, selector)
        assert "rgba(63, 185, 80" not in rule


def test_blog_search_result_copy_is_compact():
    html = _read(BLOG_PAGE)

    for marker in (
        ".blog1-search-result-meta",
        "font-size: 11px;",
        ".blog1-search-result-title",
        "font-size: 18px;",
        ".blog1-search-result-copy",
        "font-size: 13px;",
    ):
        assert marker in html


def test_blog_page_uses_blog_banner_assets():
    html = _read(BLOG_PAGE)

    assert html.count('src="/assets/blog/forkmesh-noise.webp"') == 3
    assert html.count('src="/assets/blog/coming-soon.webp"') == 2
    for marker in (
        'class="blog1-featured-image blog1-has-art"',
        'class="blog1-featured-image blog1-article-image blog1-has-art"',
        'class="blog1-post-image blog1-has-art"',
        'class="blog1-post-image blog1-has-art blog1-coming-soon-art"',
        'class="blog1-cover-img"',
    ):
        assert marker in html


def test_blog_paths_are_owned_by_python_worker():
    assert 'if url.path == "/blog":' in ENTRY_TEXT
    assert 'if url.path == "/blog.html":' in ENTRY_TEXT
    assert 'headers={"location": "/blogs"}' in ENTRY_TEXT
    assert REDIRECTS.exists()
    redirects = _read(REDIRECTS)
    assert "/blogs /blog.html 200" in redirects
    assert "/blog /blogs 308" in redirects
    assert "/blog.html /blogs 308" in redirects

    run_worker_first = WRANGLER["assets"]["run_worker_first"]
    assert WRANGLER["assets"]["html_handling"] == "none"
    assert "/blog" in run_worker_first
    assert "/blog.html" in run_worker_first
    assert "/blogs" not in run_worker_first


if __name__ == "__main__":
    test_blog_page_uses_screenshot_editorial_shell()
    test_blog_page_nav_only_links_login()
    test_blog_page_uses_homepage_logo_markup()
    test_blog_page_footer_keeps_only_twitter_social_link()
    test_blog_page_articles_open_in_page_detail_view()
    test_blog_page_detail_matches_editorial_detail_shell()
    test_blog_page_search_opens_premium_dialog()
    test_blog_page_uses_landing_green_accent_for_primary_art()
    test_blog_footer_cta_arc_uses_original_cyan()
    test_blog_interaction_states_use_neutral_dark_not_green()
    test_blog_search_result_copy_is_compact()
    test_blog_page_uses_blog_banner_assets()
    test_blog_paths_are_owned_by_python_worker()
