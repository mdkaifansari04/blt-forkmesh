#!/usr/bin/env python3
"""Static contracts for the homepage pricing section."""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
INDEX_HTML = (PUBLIC / "index.html").read_text(encoding="utf-8")


def _pricing_region() -> str:
    start = INDEX_HTML.index('<div class="pattern-rails relative w-full">')
    end = INDEX_HTML.index('<section id="faq"', start)
    return INDEX_HTML[start:end]


def test_pricing_section_uses_network_participation_copy():
    pricing = _pricing_region()

    for text in (
        '<section id="pricing" class="forkmesh">',
        "FORKMESH - NETWORK PARTICIPATION",
        "Preserve code.",
        "Get paid <em>$0.0001</em> at a time.",
        "Nodes online",
        "Your est. earnings",
        "Data mirrored",
        "Wallet",
        "Live earnings calculator",
        "SOL / join",
        "You mirror code",
        "You get paid",
        "Suggested donation <strong>$1</strong>",
        "Join the network",
        "Paid in Solana (SOL)",
    ):
        assert text in pricing


def test_pricing_section_replaces_previous_stack_design_inside_pattern_rails():
    pricing = _pricing_region()

    assert pricing.index('<div class="pattern-rails relative w-full">') < pricing.index(
        '<section id="pricing" class="forkmesh">'
    )
    assert pricing.rstrip().endswith("</div>")

    for old_marker in (
        "pricing-stack-illustration",
        "pricing-stat",
        "pricing-flow-card",
        "Preserve code for just $1",
        "Get paid to mirror code",
    ):
        assert old_marker not in pricing


def test_pricing_section_constrains_content_inside_full_width_rails_band():
    pricing = _pricing_region()

    assert (
        '<section id="pricing" class="forkmesh">\n'
        '          <div class="pricing-content">'
    ) in pricing
    assert pricing.index('<div class="pricing-content">') < pricing.index(
        '<h2 id="pricing-title">'
    )
    assert pricing.index("</footer>") < pricing.index("</div>\n        </section>")

    assert "width: min(100%, 40rem);" not in INDEX_HTML
    assert "max-width: 40rem;\n        margin-inline: auto;" not in INDEX_HTML
    assert "#pricing.forkmesh .pricing-content" in INDEX_HTML
