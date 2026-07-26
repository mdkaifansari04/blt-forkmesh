#!/usr/bin/env python3
"""Contracts for the Twitter and Reddit banners beside the Mastodon kiosk."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"


def _source(path):
    return path.read_text(encoding="utf-8") if path.exists() else ""


def test_scene_places_twitter_and_reddit_banners_on_the_social_row():
    scene = _source(SCENE_PATH)
    assert "createSocialBanner" in scene
    # Both banners flank the Mastodon kiosk on the ring toward the Office.
    assert "position: [37.2, 0, -9.2]" in scene
    assert "position: [27.6, 0, -26.6]" in scene
    assert 'registerMovableObject("twitter-banner", twitterBanner);' in scene
    assert 'registerMovableObject("reddit-banner", redditBanner);' in scene


def test_banners_carry_the_official_handles():
    scene = _source(SCENE_PATH)
    assert '"@forkmesh"' in scene
    assert '"r/forkmesh"' in scene
    assert '"https://x.com/forkmesh"' in scene
    assert '"https://www.reddit.com/r/forkmesh/"' in scene


def test_banner_click_opens_the_profile_in_a_new_tab():
    scene = _source(SCENE_PATH)
    # The whole board is one click target that rides the same guarded
    # open-link callback the Mastodon kiosk buttons use (noopener new tab).
    assert 'child.userData.interactive = "social-banner-open";' in scene
    assert 'userData?.interactive === "social-banner-open"' in scene
    assert scene.count("onMastodonOpenLink(href)") >= 2
