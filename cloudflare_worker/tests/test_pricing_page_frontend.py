#!/usr/bin/env python3
"""Static contracts for the standalone pricing page."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public"
INDEX_HTML = (PUBLIC / "index.html").read_text(encoding="utf-8")
REDIRECTS = (PUBLIC / "_redirects").read_text(encoding="utf-8")


def _pricing_html() -> str:
    page = PUBLIC / "pricing.html"
    assert page.is_file()
    return page.read_text(encoding="utf-8")


def _font_faces(html: str, family: str) -> list[str]:
    blocks = re.findall(
        r"@font-face\s*{(?P<body>.*?)\n\s*}",
        html,
        flags=re.DOTALL,
    )
    return [block for block in blocks if f'font-family: "{family}"' in block]


def _has_font_face(html: str, family: str, path: str, weight: str, format_: str) -> bool:
    return any(
        f'url("{path}") format("{format_}")' in block
        and f"font-weight: {weight};" in block
        for block in _font_faces(html, family)
    )


def test_landing_header_links_to_pricing_page():
    assert 'href="/pricing"\n                      >Pricing</a' in INDEX_HTML
    assert 'href="#pricing"\n                      >Pricing</a' not in INDEX_HTML
    assert "/pricing /pricing.html 200" in REDIRECTS


def test_pricing_page_uses_tailwind_cdn_and_shared_branding():
    PRICING_HTML = _pricing_html()

    assert "https://cdn.tailwindcss.com" in PRICING_HTML
    assert '<div data-forkmesh-header="simple"></div>' in PRICING_HTML
    assert "<title>ForkMesh Pricing - Coding Reimagined for Teams</title>" in PRICING_HTML


def test_pricing_page_reuses_landing_font_stack():
    PRICING_HTML = _pricing_html()

    for marker in (
        'sans: ["Inter", "ui-sans-serif", "system-ui", "sans-serif"]',
        'domine: ["Domine", "Georgia", "serif"]',
        'favorit: [',
        "--pricing-body-font: Inter, ui-sans-serif, system-ui, sans-serif;",
        "--pricing-hero-font: Domine, Georgia, serif;",
        "--pricing-heading-font: ABCFavorit, Inter, ui-sans-serif, system-ui, sans-serif;",
        "font-family: var(--pricing-body-font);",
        "font-family: var(--pricing-hero-font);",
        "font-family: var(--pricing-heading-font);",
    ):
        assert marker in PRICING_HTML

    assert _has_font_face(
        PRICING_HTML,
        "Domine",
        "/assets/fonts/Domine-Regular.ttf",
        "400",
        "truetype",
    )
    assert _has_font_face(
        PRICING_HTML,
        "Domine",
        "/assets/fonts/Domine-Bold.ttf",
        "700",
        "truetype",
    )
    assert _has_font_face(
        PRICING_HTML,
        "ABCFavorit",
        "/assets/fonts/ABCFavorit-Bold.otf",
        "700",
        "opentype",
    )

    custom_font_css = "\n".join(
        block
        for family in ("Domine", "ABCFavorit")
        for block in _font_faces(PRICING_HTML, family)
    )
    assert "/assets/fonts/Lato-" not in custom_font_css
    assert "/assets/fonts/DejaVuSansMono" not in custom_font_css
    assert "ABCFavoritMono" not in PRICING_HTML
    assert "fonts.googleapis.com" not in PRICING_HTML
    assert "fonts.gstatic.com" not in PRICING_HTML

    assert "PricingNeue" not in PRICING_HTML
    assert "HelveticaNeueRoman.otf" not in PRICING_HTML
    assert "HelveticaNeueBold.otf" not in PRICING_HTML


def test_pricing_page_uses_the_universal_site_header():
    # The copied landing nav was replaced by the shared session-aware header
    # (site-header.js): one chrome for every page, account chip when logged in.
    PRICING_HTML = _pricing_html()
    body = PRICING_HTML[PRICING_HTML.index("<body"):]

    assert '<div data-forkmesh-header="simple"></div>' in body
    assert "<header" not in body  # rendered by site-header.js at runtime
    assert "Sign Up / Log In" not in body


def test_pricing_page_repeats_the_free_start_cta_after_plan_comparison():
    PRICING_HTML = _pricing_html()

    assert "Ready to start with Core?" in PRICING_HTML
    assert "Create a free account and publish your first mirrored repository." in PRICING_HTML
    assert 'href="/signup"' in PRICING_HTML


def test_pricing_page_reuses_landing_header_and_footer_chrome():
    PRICING_HTML = _pricing_html()
    body = PRICING_HTML[PRICING_HTML.index("<body"):]

    for marker in (
        'href="/site-header.css"',
        'src="/site-header.js?v=',
        '<div data-forkmesh-header="simple"></div>',
        'href="/site-footer.css"',
        'src="/site-footer.js?v=',
        '<div data-forkmesh-footer="landing"></div>',
    ):
        assert marker in PRICING_HTML

    assert "site-footer-links-grid" not in body
    assert '<footer id="signup"' not in body
    assert '<a href="/pricing" aria-current="page" class="sr-only">Pricing</a>' not in PRICING_HTML


def test_pricing_page_matches_plan_card_composition():
    PRICING_HTML = _pricing_html()

    for text in (
        "Coding Reimagined",
        "For Modern Teams",
        "Distributed Git hosting, signed collaboration, encrypted team",
        "Monthly",
        "Annually",
        "Core",
        "Pro",
        "Enterprise",
        "Free",
        "Custom",
        "Start Free",
        "Unlock Pro Free",
        "Contact Team",
        "What you will get",
        "Distributed Git hosting and discovery",
        "Issue-to-agent coding workflows",
        "Organization controls, onboarding, and rollout",
    ):
        assert text in PRICING_HTML

    assert PRICING_HTML.count('data-plan-card="true"') == 3
    assert 'data-featured-plan="true"' in PRICING_HTML
    assert 'class="pricing-grid-overlay"' in PRICING_HTML
    assert 'class="pricing-spotlight"' in PRICING_HTML


def test_pricing_main_section_heading_uses_favorit_display_font():
    PRICING_HTML = _pricing_html()
    heading = re.search(
        r'<h1 class="(?P<classes>[^"]+)"[^>]*>\s*'
        r'<span class="block">Coding Reimagined</span>\s*'
        r'<span class="block text-\[#bdbdbd\]">For Modern Teams</span>',
        PRICING_HTML,
        flags=re.DOTALL,
    )

    assert heading is not None
    classes = heading.group("classes").split()
    assert "font-favorit" in classes
    assert "font-domine" not in classes


def test_pricing_cards_use_reference_dark_surface_colors():
    PRICING_HTML = _pricing_html()

    for marker in (
        "linear-gradient(180deg, rgba(255, 255, 255, 0.014) 0%, rgba(255, 255, 255, 0.005) 18%, transparent 34%)",
        "linear-gradient(180deg, #131313 0%, #050505 43%, #0d0d0d 100%)",
        "radial-gradient(circle at 50% 0%, rgba(255, 255, 255, 0.024), transparent 28%)",
        "linear-gradient(180deg, rgba(255, 255, 255, 0.026) 0%, rgba(255, 255, 255, 0.008) 19%, transparent 37%)",
        "linear-gradient(180deg, #1b1b1b 0%, #080808 44%, #101010 100%)",
        "opacity: 0.1;",
        "border: 1px solid rgba(255, 255, 255, 0.36);",
    ):
        assert marker in PRICING_HTML

    assert "rgba(255, 255, 255, 0.08), transparent 18rem" not in PRICING_HTML
    assert "rgba(255, 255, 255, 0.052) 0%, rgba(255, 255, 255, 0.018) 17%" not in PRICING_HTML
    assert "rgba(255, 255, 255, 0.105) 0%, rgba(255, 255, 255, 0.025) 19%" not in PRICING_HTML
    assert "#151515 0%, #060606 47%, #0b0b0b 100%" not in PRICING_HTML


if __name__ == "__main__":
    test_landing_header_links_to_pricing_page()
    test_pricing_page_uses_tailwind_cdn_and_shared_branding()
    test_pricing_page_reuses_landing_font_stack()
    test_pricing_page_header_matches_updated_landing_header_copy()
    test_pricing_page_reuses_landing_header_and_footer_chrome()
    test_pricing_page_matches_plan_card_composition()
    test_pricing_main_section_heading_uses_favorit_display_font()
    test_pricing_cards_use_reference_dark_surface_colors()
