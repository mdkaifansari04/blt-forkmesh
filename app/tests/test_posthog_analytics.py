#!/usr/bin/env python3
"""Every page loads the shared, default-off coarse analytics controller."""

from pathlib import Path
import re


PROJECT = Path(__file__).resolve().parents[2]
APP_PUBLIC = PROJECT / "app" / "public"
WWW_PUBLIC = PROJECT / "www" / "public"
PUBLIC_DIRS = (("app", APP_PUBLIC), ("www", WWW_PUBLIC))
ENTRY = PROJECT / "app" / "src" / "entry.py"
ADMIN_CONSOLE = ENTRY.with_name("admin_console.py")


def _loads_posthog(html):
    return bool(re.search(r'<script\b[^>]*\bsrc="/posthog\.js"[^>]*>', html))


def test_posthog_snippet_is_consent_gated_and_privacy_minimized():
    js = (WWW_PUBLIC / "posthog.js").read_text(encoding="utf-8")
    assert "posthog.init(" in js
    assert 'storedConsent() === "granted"' in js
    assert 'if (storedConsent() === "granted" && !privacySignalEnabled()) start();' in js
    assert "navigator.globalPrivacyControl === true" in js
    assert "navigator.doNotTrack" in js
    assert "autocapture: false" in js
    assert "capture_pageview: false" in js
    assert "capture_pageleave: false" in js
    assert "capture_exceptions: false" in js
    assert "capture_performance: false" in js
    assert "disable_session_recording: true" in js
    assert "advanced_disable_flags: true" in js
    assert "disable_persistence: true" in js
    assert 'persistence: "memory"' in js
    assert "mask_all_text: true" in js
    assert "mask_all_element_attributes: true" in js
    assert 'event.event !== "forkmesh_surface_view"' in js
    assert "location.search" not in js
    assert "document.referrer" not in js


def test_privacy_page_discloses_and_controls_optional_analytics():
    html = (WWW_PUBLIC / "privacy.html").read_text(encoding="utf-8")
    assert "PostHog product analytics is disabled by default" in html
    assert 'id="analytics-consent-status"' in html
    assert 'id="analytics-consent-deny"' in html
    assert 'id="analytics-consent-grant"' in html
    assert "ForkMeshAnalytics?.setConsent?.(true)" in html
    assert "ForkMeshAnalytics?.setConsent?.(false)" in html


def test_all_public_html_pages_load_posthog():
    # dashboard/partials/*.html are shell fragments (no <head>); they are
    # composed into generated dashboard assets before deploy, and the shell
    # they land in carries the snippet — so they are exempt.
    html_pages = [
        (owner, public, page)
        for owner, public in PUBLIC_DIRS
        for page in sorted(public.rglob("*.html"))
        if "partials" not in page.relative_to(public).parts
    ]
    assert html_pages

    missing = [
        owner + "/" + page.relative_to(public).as_posix()
        for owner, public, page in html_pages
        if not _loads_posthog(page.read_text(encoding="utf-8"))
    ]
    assert missing == [], "pages missing the PostHog snippet: %s" % missing


def test_worker_generated_pages_load_posthog():
    # entry.py builds two HTML documents itself (everything else is served
    # from public/): the email-verification confirmation page and the admin
    # table browser. Both must carry the snippet like the static pages do.
    for func, path in (
        ("_verify_email_page", ENTRY),
        ("render_admin_html", ADMIN_CONSOLE),
    ):
        source = path.read_text(encoding="utf-8")
        match = re.search(
            r"^(?:async )?def %s\(.*?(?=^(?:async )?def |\Z)" % func,
            source, re.M | re.S)
        assert match, "missing function %s in %s" % (func, path.name)
        assert '/posthog.js' in match.group(0), (
            "%s does not load /posthog.js" % func)
