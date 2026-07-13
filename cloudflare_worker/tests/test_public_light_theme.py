import re
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

FAMILY_THEME_CONTRACTS = {
    "auth-page-theme.css": {
        "tokens": (
            "--auth-background",
            "--auth-glow",
            "--auth-card",
            "--auth-border",
            "--auth-text",
            "--auth-text-secondary",
            "--auth-text-quiet",
            "--auth-field",
            "--auth-focus",
            "--auth-action",
            "--auth-action-text",
        ),
        "uses": (
            "var(--auth-background)",
            "var(--auth-glow)",
            "var(--auth-card)",
            "var(--auth-border)",
            "var(--auth-text)",
            "var(--auth-text-secondary)",
            "var(--auth-text-quiet)",
            "var(--auth-field)",
            "var(--auth-focus)",
            "var(--auth-action)",
            "var(--auth-action-text)",
        ),
        "selectors": (
            "body",
            ".auth-card",
            ".auth-sub",
            ".join-title",
            ".join-sub",
            ".join-eyebrow",
            ".field",
            ".hint",
            ".row-links",
            ".calc",
            ".flow",
            ".panel",
            ".meta",
            ".private-badge",
            "input",
            "textarea",
            "select",
            ".auth-button-primary",
        ),
    },
    "mesh-page-theme.css": {
        "tokens": (
            "--background",
            "--foreground",
            "--muted-foreground",
            "--border",
            "--mesh-gradient",
            "--mesh-grid-line",
            "--mesh-vignette",
            "--mesh-card",
            "--mesh-note",
            "--mesh-button",
            "--mesh-button-text",
            "--mesh-shadow",
        ),
        "uses": (
            "var(--background)",
            "var(--foreground)",
            "var(--muted-foreground)",
            "var(--border)",
            "var(--mesh-gradient)",
            "var(--mesh-grid-line)",
            "var(--mesh-vignette)",
            "var(--mesh-card)",
            "var(--mesh-note)",
            "var(--mesh-button)",
            "var(--mesh-button-text)",
            "var(--mesh-shadow)",
        ),
        "selectors": (
            ".about-shell",
            ".legal-shell",
            ".page-shell",
            ".mesh-bg",
            ".mesh-grid",
            ".mesh-vignette",
            ".about-card",
            ".legal-card",
            ".legal-note",
            ".status-card",
            ".button-primary",
        ),
    },
    "pricing-theme.css": {
        "tokens": (
            "--pricing-page-bg",
            "--pricing-text",
            "--pricing-muted",
            "--pricing-border",
            "--pricing-spotlight",
            "--pricing-card",
            "--pricing-card-featured",
            "--pricing-divider",
            "--pricing-icon",
            "--pricing-button",
            "--pricing-button-text",
            "--pricing-check",
            "--pricing-cta",
            "--pricing-cta-text",
        ),
        "uses": (
            "var(--pricing-page-bg)",
            "var(--pricing-text)",
            "var(--pricing-muted)",
            "var(--pricing-border)",
            "var(--pricing-spotlight)",
            "var(--pricing-card)",
            "var(--pricing-card-featured)",
            "var(--pricing-divider)",
            "var(--pricing-icon)",
            "var(--pricing-button)",
            "var(--pricing-button-text)",
            "var(--pricing-check)",
            "var(--pricing-cta)",
            "var(--pricing-cta-text)",
        ),
        "selectors": (
            "body",
            ".pricing-shell",
            ".pricing-spotlight",
            ".plan-card",
            '.plan-card[data-featured-plan="true"]',
            ".plan-divider",
            ".plan-icon",
            ".plan-button",
            ".feature-check",
            ".button-primary",
        ),
    },
    "feature-post.css": {
        "tokens": (
            "--bg",
            "--paper",
            "--line",
            "--soft",
            "--text",
            "--muted",
            "--green",
            "--cyan",
            "--paragraph",
            "--note",
            "--image-placeholder",
        ),
        "uses": (
            "var(--bg)",
            "var(--paper)",
            "var(--line)",
            "var(--soft)",
            "var(--text)",
            "var(--muted)",
            "var(--cyan)",
            "var(--paragraph)",
            "var(--note)",
            "var(--image-placeholder)",
        ),
        "selectors": (
            "body",
            ".shell",
            ".pill",
            ".hero-img",
            ".body p",
            ".body .note",
        ),
    },
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def css_rule(css: str, selector: str, source: str) -> str:
    match = re.search(
        rf"(?ms)^[ \t]*{re.escape(selector)}[ \t]*\{{(?P<body>[^}}]*)\}}",
        css,
    )
    assert match, f"{source} missing {selector} rule"
    return match.group("body")


def assert_stylesheet_after_inline_styles(page: Path, href: str) -> str:
    html = read(page)
    marker = f'href="{href}"'
    assert marker in html, page
    assert html.rfind(marker) > html.rfind("</style>"), (
        f"{page} must load {href} after its final inline style"
    )
    return html


def test_every_public_page_uses_the_shared_theme_control():
    assert PUBLIC_THEME_PAGES
    for page in PUBLIC_THEME_PAGES:
        html = read(page)
        assert '<meta name="color-scheme" content="light dark"' in html, page
        assert 'href="/site-header.css"' in html, page
        assert 'src="/site-header.js"' in html, page
        assert '<div data-forkmesh-header="simple"></div>' in html, page
        assert 'id="theme-toggle"' not in html, page


def test_page_families_load_complete_light_theme_styles():
    for page in AUTH_PAGES:
        assert_stylesheet_after_inline_styles(page, "/auth-page-theme.css")
    for page in MESH_PAGES:
        assert_stylesheet_after_inline_styles(page, "/mesh-page-theme.css")
    assert_stylesheet_after_inline_styles(
        PUBLIC / "pricing.html",
        "/pricing-theme.css",
    )
    assert len(FEATURE_POSTS) == 72
    for page in FEATURE_POSTS:
        html = assert_stylesheet_after_inline_styles(page, "/feature-post.css")
        legacy_root = re.search(
            r":root\s*\{[^{}]*color-scheme\s*:\s*dark\b",
            html,
            flags=re.IGNORECASE | re.DOTALL,
        )
        legacy_background = re.search(
            r"--bg\s*:\s*#030303\b",
            html,
            flags=re.IGNORECASE,
        )
        assert legacy_root is None, page
        assert legacy_background is None, page


def test_shared_family_styles_define_light_palettes():
    for name, contract in FAMILY_THEME_CONTRACTS.items():
        stylesheet = PUBLIC / name
        assert stylesheet.is_file(), f"missing shared family stylesheet: {name}"
        css = read(stylesheet)
        light_rule = css_rule(css, "html.light", name)
        assert re.search(r"color-scheme\s*:\s*light\s*;", light_rule), name
        for token in contract["tokens"]:
            assert re.search(rf"{re.escape(token)}\s*:", light_rule), (name, token)
        for usage in contract["uses"]:
            assert usage in css, (name, usage)
        for selector in contract["selectors"]:
            assert selector in css, (name, selector)
