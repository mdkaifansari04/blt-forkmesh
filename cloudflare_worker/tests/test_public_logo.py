#!/usr/bin/env python3
"""Static HTML checks for the public brand logo."""

from html.parser import HTMLParser
from pathlib import Path
import re


PUBLIC_DIR = Path(__file__).resolve().parents[1] / "public"
LOGO_SRC = "/assets/logo.png"


class BrandLogoParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self._in_brand = False
        self._brand_depth = 0
        self.brand_logos = []
        self.brand_links = 0

    def handle_starttag(self, tag, attrs):
        attr_map = dict(attrs)
        classes = set(attr_map.get("class", "").split())

        if tag == "a" and "brand" in classes and attr_map.get("href") == "/":
            self._in_brand = True
            self._brand_depth = 1
            self.brand_links += 1
            return

        if self._in_brand:
            if tag not in {"br", "hr", "img", "input", "link", "meta"}:
                self._brand_depth += 1
            if tag == "img" and "brand-mark" in classes:
                self.brand_logos.append(attr_map)

    def handle_endtag(self, tag):
        if not self._in_brand:
            return

        if tag not in {"br", "hr", "img", "input", "link", "meta"}:
            self._brand_depth -= 1
        if self._brand_depth <= 0:
            self._in_brand = False


def test_all_public_html_pages_use_logo_in_brand_link():
    assert (PUBLIC_DIR / "assets" / "logo.png").is_file()

    html_pages = sorted(PUBLIC_DIR.rglob("*.html"))
    assert html_pages

    missing = []
    for page in html_pages:
        parser = BrandLogoParser()
        parser.feed(page.read_text(encoding="utf-8"))

        has_logo = any(
            logo.get("src") == LOGO_SRC
            and logo.get("alt") == ""
            and logo.get("aria-hidden") == "true"
            for logo in parser.brand_logos
        )
        if parser.brand_links != 1 or not has_logo:
            missing.append(page.relative_to(PUBLIC_DIR).as_posix())

    assert missing == [], "Missing brand logo in: %s" % ", ".join(missing)


def test_brand_logo_size_comes_from_shared_stylesheet():
    css = (PUBLIC_DIR / "styles.css").read_text(encoding="utf-8")
    match = re.search(r"\.brand-mark\s*\{(?P<body>[^}]*)\}", css)

    assert match is not None
    body = match.group("body")
    assert "width: 2rem;" in body
    assert "height: 2.25rem;" in body
    assert "object-fit: contain;" in body
    assert "flex-shrink: 0;" in body
    assert "display: block;" in body


if __name__ == "__main__":
    test_all_public_html_pages_use_logo_in_brand_link()
    test_brand_logo_size_comes_from_shared_stylesheet()
