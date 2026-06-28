#!/usr/bin/env python3
"""Static homepage pricing and media contract tests."""

from html.parser import HTMLParser
from pathlib import Path
import re


PUBLIC = Path(__file__).resolve().parents[1] / "public"
INDEX_HTML = (PUBLIC / "index.html").read_text(encoding="utf-8")


class _VisibleMediaParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.paths = []

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if tag not in {"img", "video"}:
            if tag == "link":
                rel = values.get("rel", "")
                href = values.get("href", "")
                if href and ("icon" in rel or rel == "manifest"):
                    self._append_path(href)
            return
        for name in ("src", "poster"):
            self._append_path(values.get(name))

    def _append_path(self, value):
        if value and value.startswith("/"):
            self.paths.append(value.split("?", 1)[0].split("#", 1)[0])


def _pricing_section():
    start = INDEX_HTML.index('<section id="pricing"')
    end = INDEX_HTML.index('<section id="faq"', start)
    return INDEX_HTML[start:end]


def test_pricing_section_explains_mirror_rewards():
    pricing = _pricing_section()

    for text in (
        "FORKMESH - NETWORK PARTICIPATION",
        "Preserve code.",
        "Get paid <em>$0.0001</em> at a time.",
        "Live earnings calculator",
        "SOL / join",
        "Paid in Solana (SOL)",
        "You mirror code",
        "You get paid",
    ):
        assert text in pricing


def test_pricing_section_replaces_old_stack_illustration():
    pricing = _pricing_section()

    assert "pricing-stack-illustration" not in pricing
    assert "Preserve code for just $1" not in pricing


def test_visible_media_assets_exist():
    parser = _VisibleMediaParser()
    parser.feed(INDEX_HTML)
    parser.paths.extend(
        match.group(1) for match in re.finditer(r'url\(["\']?(/[^)"\']+)', INDEX_HTML)
    )

    missing = [
        path for path in parser.paths
        if not (PUBLIC / path.lstrip("/")).is_file()
    ]

    assert missing == []
