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
FEATURE_IMAGES = PUBLIC / "assets" / "blog" / "features"


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


def _feature_post_paths():
    feature_slugs = {path.stem for path in FEATURE_IMAGES.glob("*.webp")}
    return sorted(
        path for path in (PUBLIC / "blog").glob("*/index.html")
        if path.parent.name in feature_slugs
    )


def test_blog_page_uses_editorial_feature_archive_shell():
    html = _read(BLOG_PAGE)

    for marker in (
        '<link rel="canonical" href="https://forkmesh.com/blog">',
        'class="blog1-shell"',
        'class="blog1-hero"',
        'Our latest <span class="blog1-mono">articles</span>',
        'class="blog1-featured-image blog1-has-art"',
        'class="blog1-next"',
    ):
        assert marker in html


def test_blog_page_mounts_the_universal_site_header():
    # The blog's page-specific topbar (brand + hardcoded "Sign Up / Log In")
    # was replaced by the shared session-aware header, which shows the
    # logged-in account chip instead of the auth links.
    html = _read(BLOG_PAGE)

    assert 'href="/site-header.css"' in html
    assert 'src="/site-header.js?v=' in html
    assert '<div data-forkmesh-header="simple"></div>' in html
    assert 'class="blog1-topbar"' not in html
    assert "Sign Up / Log In" not in html


def test_blog_page_footer_keeps_only_twitter_social_link():
    html = _read(BLOG_PAGE)

    assert 'aria-label="Twitter"' in html
    assert 'href="https://x.com/forkmesh/"' in html
    assert ("https://twitter.com/" + "forkmesh") not in html
    for removed in ("GitHub", "Discord", "LinkedIn", "YouTube"):
        assert 'aria-label="%s"' % removed not in html


def test_blog_page_indexes_every_feature_post():
    html = _read(BLOG_PAGE)
    posts = _feature_post_paths()

    assert len(posts) == 72
    assert html.count('class="blog1-card" href="/blog/') == 72
    assert html.count('class="blog1-search-result" href="/blog/') == 72
    assert html.count('data-blog-search-text=') == 72
    assert 'href="/blog/desktop-node-mirrors/"' in html
    assert 'href="/blog/status-blog-and-changelog/"' in html
    assert "Every ForkMesh feature now has a short technical post" in html


def test_feature_blog_posts_have_images_and_article_shells():
    sample = PUBLIC / "blog" / "desktop-node-mirrors" / "index.html"
    html = _read(sample)

    for marker in (
        '<link rel="canonical" href="https://forkmesh.com/blog/desktop-node-mirrors/">',
        "Desktop-node mirrors",
        'src="/assets/blog/features/desktop-node-mirrors.webp"',
        "Why it matters",
        "How ForkMesh handles it",
        "Where it fits",
        'href="/blog">← All blog posts</a>',
    ):
        assert marker in html


def test_every_blog_post_carries_fillable_social_permalinks():
    posts = sorted((PUBLIC / "blog").glob("*/index.html"))

    assert len(posts) == 73
    for post in posts:
        html = _read(post)
        for marker in (
            '<link rel="stylesheet" href="/blog-social.css">',
            '<script src="/blog-social.js" defer></script>',
            "<section data-blog-social",
            'data-reddit=""',
            'data-mastodon=""',
            'data-twitter=""',
        ):
            assert marker in html, post


def test_blog_social_renderer_shows_unfilled_networks_as_empty():
    script = _read(PUBLIC / "blog-social.js")
    styles = _read(PUBLIC / "blog-social.css")

    for marker in (
        '{ key: "reddit", label: "Reddit" }',
        '{ key: "mastodon", label: "Mastodon" }',
        '{ key: "twitter", label: "X (Twitter)" }',
        'const EMPTY_LABEL = "Not posted yet";',
        # A blank attribute — and anything that is not an http(s) permalink —
        # renders as the empty slot instead of becoming an anchor.
        'return /^https?:\\/\\//i.test(raw) ? raw : "";',
        'empty.className = "blog-social-empty";',
        'link.rel = "noopener noreferrer me";',
    ):
        assert marker in script

    assert ".blog-social-item.is-empty" in styles
    assert ".blog-social-empty" in styles


def test_feature_blog_images_exist_for_each_generated_post():
    posts = _feature_post_paths()
    images = sorted(FEATURE_IMAGES.glob("*.webp"))

    assert len(images) == 72
    for post in posts:
        slug = post.parent.name
        assert FEATURE_IMAGES.joinpath(slug + ".webp").is_file()


def test_blog_page_search_opens_dialog():
    html = _read(BLOG_PAGE)

    for marker in (
        'class="blog1-search-trigger"',
        'aria-haspopup="dialog"',
        'aria-controls="blog-search-modal"',
        'id="blog-search-modal"',
        'role="dialog"',
        'aria-modal="true"',
        'id="blog-search-input"',
        'data-blog-search-empty',
        "openBlogSearch",
        "filterBlogSearchResults",
    ):
        assert marker in html


def test_blog_page_uses_landing_green_accent_for_primary_art():
    html = _read(BLOG_PAGE)

    assert "--blog1-green:#3fb950;" in html
    assert "--blog1-cyan:#76d8f6;" in html
    assert "--blog1-blue:" not in html


def test_blog_interaction_states_use_neutral_dark_not_green():
    html = _read(BLOG_PAGE)

    for marker in (
        "--blog1-hover-line:rgba(246,246,242,.24);",
        "--blog1-hover-fill:rgba(255,255,255,.045);",
        "--blog1-hover-ring:rgba(255,255,255,.08);",
        "--blog1-popup-sheen:rgba(255,255,255,.11);",
    ):
        assert marker in html


def test_blog_search_result_copy_is_compact():
    html = _read(BLOG_PAGE)

    for marker in (
        ".blog1-search-result-meta",
        "font-size:11px;",
        ".blog1-search-result-title",
        "font-size:18px;",
        ".blog1-search-result-copy",
        "font-size:13px;",
    ):
        assert marker in html


def test_blog_paths_are_owned_by_redirects_not_python_worker():
    assert 'if url.path == "/blog":' not in ENTRY_TEXT
    assert 'if url.path == "/blog.html":' not in ENTRY_TEXT
    assert REDIRECTS.exists()
    redirects = _read(REDIRECTS)
    assert "/blog /blog.html 200" in redirects
    assert "/blog/ /blog 308" in redirects
    assert "/blogs /blog 308" in redirects
    assert "/blog/:slug /blog/:slug/ 308" in redirects
    assert "/blog/:slug/ /blog/:slug/index.html 200" in redirects
    assert "/blog.html /blog 308" not in redirects

    run_worker_first = WRANGLER["assets"]["run_worker_first"]
    assert WRANGLER["assets"]["html_handling"] == "none"
    assert "/blog" not in run_worker_first
    assert "/blogs" not in run_worker_first
    assert "/blog.html" in run_worker_first


if __name__ == "__main__":
    test_blog_page_uses_editorial_feature_archive_shell()
    test_blog_page_nav_only_links_auth()
    test_blog_page_uses_homepage_logo_markup()
    test_blog_page_footer_keeps_only_twitter_social_link()
    test_blog_page_indexes_every_feature_post()
    test_feature_blog_posts_have_images_and_article_shells()
    test_every_blog_post_carries_fillable_social_permalinks()
    test_blog_social_renderer_shows_unfilled_networks_as_empty()
    test_feature_blog_images_exist_for_each_generated_post()
    test_blog_page_search_opens_dialog()
    test_blog_page_uses_landing_green_accent_for_primary_art()
    test_blog_interaction_states_use_neutral_dark_not_green()
    test_blog_search_result_copy_is_compact()
    test_blog_paths_are_owned_by_redirects_not_python_worker()
