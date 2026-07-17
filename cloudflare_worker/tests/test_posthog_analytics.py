#!/usr/bin/env python3
"""Every page must load the shared PostHog snippet (/posthog.js)."""

from pathlib import Path
import re


PUBLIC_DIR = Path(__file__).resolve().parents[1] / "public"
ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
POSTHOG_TAG = '<script src="/posthog.js"></script>'


def test_posthog_snippet_exists_and_inits():
    js = (PUBLIC_DIR / "posthog.js").read_text(encoding="utf-8")
    assert "posthog.init(" in js


def test_all_public_html_pages_load_posthog():
    # dashboard/partials/*.html are shell fragments (no <head>); they are
    # composed into generated dashboard assets before deploy, and the shell
    # they land in carries the snippet — so they are exempt.
    html_pages = sorted(
        p for p in PUBLIC_DIR.rglob("*.html")
        if "partials" not in p.relative_to(PUBLIC_DIR).parts
    )
    assert html_pages

    missing = [
        page.relative_to(PUBLIC_DIR).as_posix()
        for page in html_pages
        if POSTHOG_TAG not in page.read_text(encoding="utf-8")
    ]
    assert missing == [], "pages missing the PostHog snippet: %s" % missing


def test_worker_generated_pages_load_posthog():
    # entry.py builds two HTML documents itself (everything else is served
    # from public/): the email-verification confirmation page and the admin
    # table browser. Both must carry the snippet like the static pages do.
    source = ENTRY.read_text(encoding="utf-8")
    for func in ("_verify_email_page", "render_admin_html"):
        match = re.search(
            r"^(?:async )?def %s\(.*?(?=^(?:async )?def |\Z)" % func,
            source, re.M | re.S)
        assert match, "missing function %s in entry.py" % func
        assert '/posthog.js' in match.group(0), (
            "%s does not load /posthog.js" % func)
