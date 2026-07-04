#!/usr/bin/env python3
"""Static contracts for landing page typography."""

import re
from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
INDEX_HTML = (PUBLIC / "index.html").read_text(encoding="utf-8")


def _font_faces(family: str) -> list[str]:
    blocks = re.findall(
        r"@font-face\s*{(?P<body>.*?)\n\s*}",
        INDEX_HTML,
        flags=re.DOTALL,
    )
    return [block for block in blocks if f'font-family: "{family}"' in block]


def _has_font_face(family: str, path: str, weight: str, format_: str) -> bool:
    return any(
        f'url("{path}") format("{format_}")' in block
        and f"font-weight: {weight};" in block
        for block in _font_faces(family)
    )


def test_landing_page_uses_bundled_brand_font_assets():
    assert _has_font_face(
        "Domine",
        "/assets/fonts/Domine-Regular.ttf",
        "400",
        "truetype",
    )
    assert _has_font_face(
        "Domine",
        "/assets/fonts/Domine-Bold.ttf",
        "700",
        "truetype",
    )
    assert _has_font_face(
        "ABCFavorit",
        "/assets/fonts/ABCFavorit-Bold.otf",
        "700",
        "opentype",
    )

    custom_font_css = "\n".join(
        block
        for family in ("Domine", "ABCFavorit")
        for block in _font_faces(family)
    )
    assert "/assets/fonts/Lato-" not in custom_font_css
    assert "/assets/fonts/DejaVuSansMono" not in custom_font_css
    assert "ABCFavoritMono" not in INDEX_HTML


def test_landing_page_declares_typography_roles():
    assert "--landing-body-font: Inter, ui-sans-serif, system-ui, sans-serif;" in INDEX_HTML
    assert "--landing-hero-font: Domine, Georgia, serif;" in INDEX_HTML
    assert (
        "--landing-heading-font: ABCFavorit, Inter, ui-sans-serif, system-ui, sans-serif;"
        in INDEX_HTML
    )
    assert (
        "--landing-mono-font: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;"
        in INDEX_HTML
    )

    assert ".hero-section h1 {" in INDEX_HTML
    assert "font-family: var(--landing-hero-font);" in INDEX_HTML

    for selector in (
        "#features h2",
        "#network h2",
        "#solution h2",
        "#pricing.forkmesh h2",
        "#faq .faq-title",
    ):
        assert selector in INDEX_HTML

    for selector in (
        ".feature-card h3",
        "#solution article h3",
        "#pricing.forkmesh .step h3",
        "#faq-list .faq-item summary",
    ):
        assert selector in INDEX_HTML

    assert "font-family: var(--landing-heading-font);" in INDEX_HTML

    for selector in (
        ".hero-section p",
        "#features p",
        "#network p",
        "#solution p",
        "#pricing.forkmesh .lead",
        "#pricing.forkmesh .step p",
        "#faq .faq-intro",
        "#faq-list .faq-item p",
    ):
        assert selector in INDEX_HTML

    assert "font-family: var(--landing-body-font);" in INDEX_HTML


def test_landing_page_uses_inter_for_non_heading_text():
    assert "#pricing.forkmesh {" in INDEX_HTML
    assert "font-family: var(--landing-body-font);" in INDEX_HTML
    assert "--pricing-mono" not in INDEX_HTML
    assert "font-family: var(--pricing-mono);" not in INDEX_HTML
