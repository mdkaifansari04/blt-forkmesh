#!/usr/bin/env python3
"""Every page loads the shared, default-off coarse analytics controller."""

from pathlib import Path
import re


PUBLIC_DIR = Path(__file__).resolve().parents[1] / "public"
ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
POSTHOG_TAG = '<script src="/posthog.js"></script>'


def test_posthog_snippet_is_consent_gated_and_privacy_minimized():
    js = (PUBLIC_DIR / "posthog.js").read_text(encoding="utf-8")
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
    html = (PUBLIC_DIR / "privacy.html").read_text(encoding="utf-8")
    assert "PostHog product analytics is disabled by default" in html
    assert 'id="analytics-consent-status"' in html
    assert 'id="analytics-consent-deny"' in html
    assert 'id="analytics-consent-grant"' in html
    assert "ForkMeshAnalytics?.setConsent?.(true)" in html
    assert "ForkMeshAnalytics?.setConsent?.(false)" in html


def test_all_public_html_pages_load_posthog():



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



    source = ENTRY.read_text(encoding="utf-8")
    for func in ("_verify_email_page", "render_admin_html"):
        match = re.search(
            r"^(?:async )?def %s\(.*?(?=^(?:async )?def |\Z)" % func,
            source, re.M | re.S)
        assert match, "missing function %s in entry.py" % func
        assert '/posthog.js' in match.group(0), (
            "%s does not load /posthog.js" % func)
