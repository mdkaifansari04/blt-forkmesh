import re
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[2]
APP_PUBLIC = PROJECT / "app" / "public"
AUTH_PAGES = (
    APP_PUBLIC / "login.html",
    APP_PUBLIC / "signup.html",
    APP_PUBLIC / "forgot-password.html",
    APP_PUBLIC / "reset-password.html",
)
MESH_PAGES = (
    APP_PUBLIC / "404.html",
)
PUBLIC_THEME_PAGES = tuple(
    page
    for page in sorted(APP_PUBLIC.rglob("*.html"))
    # The dashboard and immersive 3D world own their chrome. World lighting
    # is controlled by its Day/Sunset/Night/weather palette without mounting
    # a second site header over the game HUD.
    if (
        "dashboard" not in page.parts
        and "world" not in page.parts
    )
)

FAMILY_THEME_STYLESHEETS = (
    "/auth-page-theme.css",
    "/mesh-page-theme.css",
)

FAMILY_THEME_FILES = {
    "auth-page-theme.css": APP_PUBLIC / "auth-page-theme.css",
    "mesh-page-theme.css": APP_PUBLIC / "mesh-page-theme.css",
}

FAMILY_THEME_CONTRACTS = {
    "auth-page-theme.css": {
        "light": {
            "color-scheme": "light",
            "--auth-page-bg": "#ffffff",
            "--auth-glow": (
                "radial-gradient(circle at top, rgba(220, 38, 38, 0.12), "
                "transparent 32rem)"
            ),
            "--auth-card-bg": "#ffffff",
            "--auth-border": "#e5e7eb",
            "--auth-text": "#111827",
            "--auth-text-secondary": "#4b5563",
            "--auth-text-quiet": "#6b7280",
            "--auth-field-bg": "#ffffff",
            "--auth-focus": "#dc2626",
            "--auth-action-bg": "#dc2626",
            "--auth-action-text": "#ffffff",
        },
        "rules": {
            "body": {
                "background": "var(--auth-page-bg)",
                "color": "var(--auth-text)",
            },
            ".auth-shell": {"background-image": "var(--auth-glow)"},
            ".auth-card": {
                "background": "var(--auth-card-bg)",
                "border-color": "var(--auth-border)",
            },
            ".auth-sub": {"color": "var(--auth-text-secondary)"},
            ".hint": {"color": "var(--auth-text-quiet)"},
            "input, textarea, select": {
                "background": "var(--auth-field-bg)",
                "border-color": "var(--auth-border)",
                "color": "var(--auth-text)",
            },
            "input:focus, textarea:focus, select:focus": {
                "border-color": "var(--auth-focus)"
            },
            ".auth-button-primary": {
                "background": "var(--auth-action-bg)",
                "color": "var(--auth-action-text)",
            },
        },
    },
    "mesh-page-theme.css": {
        "light": {
            "color-scheme": "light",
            "--mesh-page-bg": "#f6f8fb",
            "--mesh-text": "#1f2328",
            "--mesh-muted": "#656d76",
            "--mesh-border": "#d0d7de",
            "--mesh-background": (
                "radial-gradient(circle at top, #ffffff, #f6f8fb 70%)"
            ),
            "--mesh-grid": (
                "linear-gradient(rgba(31, 35, 40, 0.08) 1px, transparent 1px), "
                "linear-gradient(90deg, rgba(31, 35, 40, 0.08) 1px, "
                "transparent 1px)"
            ),
            "--mesh-vignette": (
                "radial-gradient(circle, transparent 35%, "
                "rgba(246, 248, 251, 0.92))"
            ),
            "--mesh-card-bg": "#ffffff",
            "--mesh-note-bg": "#eef2f7",
            "--mesh-button-bg": "#1f2328",
            "--mesh-button-text": "#ffffff",
            "--mesh-shadow": "0 18px 50px rgba(31, 35, 40, 0.12)",
        },
        "rules": {
            ".about-shell, .legal-shell, .page-shell": {
                "background": "var(--mesh-page-bg)",
                "color": "var(--mesh-text)",
            },
            ".mesh-bg": {"background": "var(--mesh-background)"},
            ".mesh-grid": {"background-image": "var(--mesh-grid)"},
            ".mesh-vignette": {"background": "var(--mesh-vignette)"},
            ".about-card, .legal-card, .status-card": {
                "background": "var(--mesh-card-bg)",
                "border-color": "var(--mesh-border)",
                "box-shadow": "var(--mesh-shadow)",
            },
            ".legal-note": {
                "background": "var(--mesh-note-bg)",
                "color": "var(--mesh-muted)",
            },
            ".button-primary": {
                "background": "var(--mesh-button-bg)",
                "color": "var(--mesh-button-text)",
            },
        },
    },
}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def css_rule(css: str, selector: str, source: str) -> str:
    selector_pattern = r"\s+".join(
        re.escape(part) for part in selector.split()
    )
    match = re.search(
        rf"(?ms)^[ \t]*{selector_pattern}[ \t]*\{{(?P<body>[^}}]*)\}}",
        css,
    )
    assert match, f"{source} missing {selector} rule"
    return match.group("body")


def css_declarations(css: str, selector: str, source: str) -> dict[str, str]:
    return {
        name: re.sub(r"\s+", " ", value.strip())
        for name, value in re.findall(
            r"(?m)^[ \t]*([\w-]+)\s*:\s*([^;]+);",
            css_rule(css, selector, source),
        )
    }


def assert_css_declarations(
    css: str,
    selector: str,
    expected: dict[str, str],
    source: str,
) -> None:
    declarations = css_declarations(css, selector, source)
    for name, value in expected.items():
        normalized_value = re.sub(r"\s+", " ", value.strip())
        assert declarations.get(name) == normalized_value, (
            f"{source} {selector} must set {name}: {value}; "
            f"found {declarations.get(name)!r}"
        )


def inline_styles(html: str) -> str:
    return "\n".join(
        re.findall(r"<style\b[^>]*>(.*?)</style>", html, re.IGNORECASE | re.DOTALL)
    )


def recognized_light_theme_strategies(page: Path, html: str) -> tuple[str, ...]:
    strategies = []
    inline_css = inline_styles(html)
    chat_css = (
        read(APP_PUBLIC / "chat.css")
        if page == APP_PUBLIC / "chat.html"
        else ""
    )

    for href in FAMILY_THEME_STYLESHEETS:
        if f'href="{href}"' in html:
            strategies.append(href)

    styles_link = re.search(
        r'<link\b[^>]*\bhref="/?styles\.css"[^>]*>',
        html,
        flags=re.IGNORECASE,
    )
    dark_root = re.search(
        r":root\s*\{[^{}]*color-scheme\s*:\s*dark\b",
        inline_css,
        flags=re.IGNORECASE | re.DOTALL,
    )
    dark_body = re.search(
        r"body\s*\{[^{}]*background(?:-color)?\s*:\s*"
        r"(?:#0{3,6}\b|#090909\b|black\b)",
        inline_css,
        flags=re.IGNORECASE | re.DOTALL,
    )
    inline_light_override = re.search(
        r"html(?:\.light|:not\(\.dark\))[^{}]*\{[^{}]*"
        r"(?:--[\w-]+\s*:|background(?:-color)?\s*:)",
        inline_css,
        flags=re.IGNORECASE | re.DOTALL,
    )
    unhandled_dark_style = (dark_root or dark_body) and not inline_light_override
    if styles_link and not unhandled_dark_style:
        strategies.append("styles.css")

    chat_light = re.search(
        r"html:not\(\.dark\)\s*\{(?P<body>[^{}]*)\}",
        chat_css,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if (
        page == APP_PUBLIC / "chat.html"
        and chat_light
        and re.search(r"--bg\s*:\s*#f6f8fa\s*;", chat_light.group("body"))
        and re.search(r"--fg\s*:\s*#1f2328\s*;", chat_light.group("body"))
    ):
        strategies.append("chat-stylesheet-palette")

    return tuple(strategies)


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
        assert 'src="/site-header.js?v=' in html, page
        assert '<div data-forkmesh-header="simple"></div>' in html, page
        assert 'id="theme-toggle"' not in html, page


def test_every_public_page_has_a_recognized_light_theme_strategy():
    for page in PUBLIC_THEME_PAGES:
        strategies = recognized_light_theme_strategies(page, read(page))
        assert strategies, (
            f"{page} must use tokenized styles.css without an unhandled dark "
            "inline style, a family theme stylesheet, or the Chat light palette"
        )


def test_hardcoded_dark_page_is_not_a_recognized_light_theme_strategy():
    html = """
      <link rel="stylesheet" href="/styles.css">
      <style>
        :root { color-scheme: dark; }
        body { background: #000; color: #fff; }
      </style>
    """

    assert not recognized_light_theme_strategies(APP_PUBLIC / "new-page.html", html)


def test_page_families_load_complete_light_theme_styles():
    for page in AUTH_PAGES:
        assert_stylesheet_after_inline_styles(page, "/auth-page-theme.css")
    for page in MESH_PAGES:
        assert_stylesheet_after_inline_styles(page, "/mesh-page-theme.css")


def test_shared_family_styles_define_light_palettes():
    for name, contract in FAMILY_THEME_CONTRACTS.items():
        stylesheet = FAMILY_THEME_FILES[name]
        assert stylesheet.is_file(), f"missing shared family stylesheet: {name}"
        css = read(stylesheet)
        assert_css_declarations(css, "html.light", contract["light"], name)
        for selector, expected in contract["rules"].items():
            assert_css_declarations(css, selector, expected, name)


def test_shared_footer_defines_dark_and_light_semantic_palettes():
    footer_css = read(APP_PUBLIC / "site-footer.css")

    for token in (
        "--fm-footer-panel",
        "--fm-footer-fg",
        "--fm-footer-muted",
        "--fm-footer-border",
        "--fm-footer-wordmark",
        "--fm-footer-glow",
    ):
        assert token in footer_css

    light_override = css_rule(footer_css, "html.light .forkmesh-footer", "site-footer.css")
    expected_light = {
        "--fm-footer-panel": "#ffffff",
        "--fm-footer-fg": "#1f2328",
        "--fm-footer-muted": "#656d76",
        "--fm-footer-border": "#d0d7de",
        "--fm-footer-wordmark": "#eef2f7",
        "--fm-footer-glow": "color-mix(in srgb, #1f2328 72%, #ffffff)",
    }
    assert_css_declarations(
        footer_css,
        "html.light .forkmesh-footer",
        expected_light,
        "site-footer.css",
    )
    for token in (
        "--fm-footer-panel",
        "--fm-footer-fg",
        "--fm-footer-muted",
        "--fm-footer-border",
        "--fm-footer-wordmark",
        "--fm-footer-glow",
    ):
        assert token in light_override

    for selector in (
        ".site-footer-panel",
        ".site-footer-brand-copy",
        ".site-footer-column-title",
        ".site-footer-column-link",
        ".site-footer-copyright",
    ):
        assert "var(--fm-footer-" in css_rule(footer_css, selector, "site-footer.css")

    assert_css_declarations(
        footer_css,
        ".forkmesh-footer",
        {"--fm-footer-social-border": "var(--border, #313131)"},
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        "html.light .forkmesh-footer",
        {"--fm-footer-social-border": "#d0d7de"},
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        ".site-footer-social-link",
        {"border": "1px solid var(--fm-footer-social-border)"},
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        ".forkmesh-footer",
        {
            "--fm-footer-social-hover-border": (
                "rgba(232, 232, 232, 0.3)"
            )
        },
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        "html.light .forkmesh-footer",
        {"--fm-footer-social-hover-border": "#8c959f"},
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        ".site-footer-social-link:hover",
        {
            "border-color": "var(--fm-footer-social-hover-border)",
            "color": "var(--fm-footer-fg)",
        },
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        ".forkmesh-footer",
        {"--fm-footer-glow-filter": "rgba(255, 255, 255, 0.18)"},
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        "html.light .forkmesh-footer",
        {"--fm-footer-glow-filter": "rgba(31, 35, 40, 0.18)"},
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        ".footer-glow-text",
        {
            "filter": (
                "drop-shadow(0 0 24px var(--fm-footer-glow-filter))"
            )
        },
        "site-footer.css",
    )

    assert_css_declarations(
        footer_css,
        ".site-footer-status-pill",
        {
            "border": "1px solid var(--fm-footer-border)",
            "color": "var(--fm-footer-muted)",
        },
        "site-footer.css",
    )
    assert_css_declarations(
        footer_css,
        ".site-footer-status-dot",
        {"background": "var(--green, var(--accent-bright, #28c878))"},
        "site-footer.css",
    )
