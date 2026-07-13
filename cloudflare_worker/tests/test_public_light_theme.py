from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
AUTH_PAGES = tuple(
    PUBLIC / name
    for name in (
        "login.html",
        "signup.html",
        "forgot-password.html",
        "reset-password.html",
        "mirror-payouts.html",
        "outreach.html",
        "security-report.html",
    )
)
MESH_PAGES = tuple(
    PUBLIC / name
    for name in (
        "about.html",
        "terms.html",
        "privacy.html",
        "404.html",
    )
)
FEATURE_POSTS = tuple(
    page
    for page in sorted((PUBLIC / "blog").glob("*/index.html"))
    if page.parent.name != "introducing-forkmesh"
)
PUBLIC_THEME_PAGES = tuple(
    page
    for page in sorted(PUBLIC.rglob("*.html"))
    if page != PUBLIC / "index.html" and "dashboard" not in page.parts
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_every_public_page_uses_the_shared_theme_control():
    assert PUBLIC_THEME_PAGES
    for page in PUBLIC_THEME_PAGES:
        html = read(page)
        assert '<meta name="color-scheme" content="light dark"' in html, page
        assert '<div data-forkmesh-header="simple"></div>' in html, page
        assert 'id="theme-toggle"' not in html, page


def test_page_families_load_complete_light_theme_styles():
    for page in AUTH_PAGES:
        assert 'href="/auth-page-theme.css"' in read(page), page
    for page in MESH_PAGES:
        assert 'href="/mesh-page-theme.css"' in read(page), page
    assert 'href="/pricing-theme.css"' in read(PUBLIC / "pricing.html")
    assert len(FEATURE_POSTS) == 72
    for page in FEATURE_POSTS:
        html = read(page)
        assert 'href="/feature-post.css"' in html, page
        assert "color-scheme: dark; --bg:#030303" not in html, page


def test_shared_family_styles_define_light_palettes():
    for name in (
        "auth-page-theme.css",
        "mesh-page-theme.css",
        "pricing-theme.css",
        "feature-post.css",
    ):
        stylesheet = PUBLIC / name
        assert stylesheet.is_file(), f"missing shared family stylesheet: {name}"
        css = read(stylesheet)
        assert "html.light" in css, name
        assert "color-scheme: light" in css, name
