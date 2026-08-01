#!/usr/bin/env python3
"""Profile fediverse presence contract (adhoc #305).

A profile with a linked Mastodon handle shows that account's header image as
a banner across the overview page and its newest public posts in a sidebar
card. The browser fetches the user's home instance directly (public CORS API)
so the Worker's request quota is untouched, remote HTML is reduced to plain
text before rendering, and every remote URL must survive an https-only check.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import dashboard_shell

from _dashboard_shell import assembled_dashboard_page
from _dashboard_bundle import assembled_dashboard_js

BUNDLE = assembled_dashboard_js()


def fediverse_section():
    match = re.search(
        r"// ---- Fediverse presence -+\n(.*?)\n  function renderProfileLinksEditor",
        BUNDLE, re.S)
    assert match is not None, "fediverse section missing from dashboard.js"
    return match.group(1)


def test_profile_overview_page_carries_the_banner_and_posts_card():
    html = assembled_dashboard_page("profile")
    banner = re.search(r"<a data-profile-fediverse-banner\b[^>]*>", html)
    assert banner is not None


    assert "hidden" in banner.group(0)
    assert 'rel="noopener noreferrer"' in banner.group(0)
    assert 'target="_blank"' in banner.group(0)


def test_sidebar_template_ships_the_posts_card_on_every_page():
    for page_id, meta in dashboard_shell.PAGES.items():
        html = assembled_dashboard_page(page_id)
        if "data-profile-sidebar-template" not in html:
            continue
        assert "data-profile-fediverse" in html, page_id
        assert "data-profile-fediverse-posts" in html, page_id
        assert "data-profile-fediverse-link" in html, page_id


def test_fediverse_fetches_are_anonymous_and_bounded():
    section = fediverse_section()


    assert 'credentials: "omit"' in section
    assert "AbortController" in section
    assert "/api/v1/accounts/lookup?acct=" in section
    assert "exclude_replies=true" in section

    assert 'PROFILE_FEDIVERSE_CACHE_PREFIX = "forkmesh.profileFediverse:v1:"' in section
    assert "10 * 60 * 1000" in section


def test_remote_content_renders_as_text_and_https_only():
    section = fediverse_section()


    sinks = [line for line in section.splitlines() if "innerHTML" in line]
    assert sinks == ["    decoder.innerHTML = stripped;"], sinks
    assert "textContent" in section

    assert 'url.protocol === "https:" && !url.username && !url.password' in section

    assert "missing.png" in section
