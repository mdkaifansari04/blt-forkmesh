#!/usr/bin/env python3
"""Static HTML checks for the public brand logo."""

from html.parser import HTMLParser
from pathlib import Path
import re

from _dashboard_shell import assembled_dashboard


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

        if (
            tag == "a"
            and "brand" in classes
            and attr_map.get("href") in {"/", "/dashboard"}
        ):
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


class LogoImageParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.logos = []

    def handle_starttag(self, tag, attrs):
        attr_map = dict(attrs)
        if tag == "img" and attr_map.get("src") == LOGO_SRC:
            self.logos.append(attr_map)


def test_all_public_html_pages_use_logo_in_brand_link():
    assert (PUBLIC_DIR / "assets" / "logo.png").is_file()




    html_pages = sorted(
        p for p in PUBLIC_DIR.rglob("*.html")
        if "partials" not in p.relative_to(PUBLIC_DIR).parts
    )
    assert html_pages

    missing = []
    for page in html_pages:
        rel = page.relative_to(PUBLIC_DIR).as_posix()



        if rel == "dashboard/shell.html":
            continue
        html = page.read_text(encoding="utf-8")
        if '<div data-forkmesh-header="simple"></div>' in html:
            header_js = (PUBLIC_DIR / "site-header.js").read_text(encoding="utf-8")
            if (
                'href="/"' in header_js
                and 'src="/assets/logo.png"' in header_js
                and 'aria-hidden="true"' in header_js
            ):
                continue

        parser = BrandLogoParser()
        parser.feed(html)

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


def test_dashboard_assets_are_root_relative_for_deep_links():
    html = (PUBLIC_DIR / "dashboard" / "index.html").read_text(encoding="utf-8")




    m = re.search(r'src="/dashboard\.js\?v=([0-9a-f]{6,})"', html)
    assert m is not None, "dashboard.js must be root-relative with a content-hash ?v="
    assert 'href="styles.css"' not in html
    assert 'src="dashboard.js"' not in html
    assert 'src="/dashboard.js?v=public-profiles"' not in html


def test_dashboard_logo_does_not_paint_light_background():
    html = assembled_dashboard()
    parser = LogoImageParser()
    parser.feed(html)

    assert parser.logos
    for logo in parser.logos:
        classes = set(logo.get("class", "").split())
        assert "bg-foreground" not in classes
        assert "p-1" not in classes
        assert "rounded-full" not in classes


def test_dark_pages_do_not_wrap_logo_in_light_circle():
    bad_classes = {"bg-foreground", "bg-white", "bg-white/90", "p-1", "rounded-full"}
    offenders = []

    html_pages = sorted(
        p for p in PUBLIC_DIR.rglob("*.html")
        if "partials" not in p.relative_to(PUBLIC_DIR).parts
    )
    for page in html_pages:
        rel = page.relative_to(PUBLIC_DIR).as_posix()
        if rel == "dashboard/shell.html":
            continue
        html = page.read_text(encoding="utf-8")

        is_dark = (
            'class="dark' in html
            or 'data-dashboard-theme="dark"' in html
            or 'content="dark"' in html
            or "content=\"light dark\"" in html
        )
        if not is_dark:
            continue

        parser = LogoImageParser()
        parser.feed(html)
        for logo in parser.logos:
            classes = set(logo.get("class", "").split())
            overlap = sorted(classes & bad_classes)
            if overlap:
                offenders.append("%s: %s" % (rel, " ".join(overlap)))

    assert offenders == []


if __name__ == "__main__":
    test_all_public_html_pages_use_logo_in_brand_link()
    test_brand_logo_size_comes_from_shared_stylesheet()
    test_dashboard_assets_are_root_relative_for_deep_links()
    test_dashboard_logo_does_not_paint_light_background()
    test_dark_pages_do_not_wrap_logo_in_light_circle()
