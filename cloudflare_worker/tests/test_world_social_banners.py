#!/usr/bin/env python3
"""Contracts for the Twitter and Reddit banners beside the Mastodon kiosk."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"
WORLD_PATH = ROOT / "public" / "world" / "world.js"
ENTRY_PATH = SRC / "entry.py"

if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import world_social_feeds as feeds  # noqa: E402


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


def test_banner_faces_repaint_from_the_proxy_snapshot():
    scene = _source(SCENE_PATH)
    assert "function socialBannerTexture(THREE, options, snapshot = null)" in scene
    assert "function updateSocialBanners(payload)" in scene
    # Exposed on the scene API so world.js can push snapshots.
    assert "updateSocialBanners,\n" in scene
    # Repaints dispose the previous texture like the Mastodon kiosk does.
    assert scene.count("face.material.map?.dispose?.();") >= 2
    # A feed that is not ready keeps the static sign, never a blank board.
    assert 'feed.state === "ready"' in scene


def test_world_fetches_the_proxy_without_credentials_on_a_timer():
    world = _source(WORLD_PATH)
    assert 'const SOCIAL_POSTS_URL = "/api/world/social-posts";' in world
    assert "const SOCIAL_REFRESH_MS = 10 * 60 * 1000;" in world
    assert "startSocialBannersRefresh()" in world
    assert "this.startSocialBannersRefresh();" in world
    assert "window.clearInterval(this.socialFeedsTimer);" in world
    # The proxy read is public and never carries ForkMesh session material.
    idx = world.index("loadSocialBanners()")
    assert 'credentials: "omit"' in world[idx:idx + 1200]
    assert "this.world?.updateSocialBanners?.(" in world


def test_worker_routes_the_social_posts_proxy_behind_the_edge_cache():
    entry = _source(ENTRY_PATH)
    assert '"/api/world/social-posts", "/api/world/social-posts/"' in entry
    assert "world_social_posts_handler" in entry
    assert (
        'WORLD_SOCIAL_POSTS_CACHE_KEY = (\n'
        '    "https://forkmesh.internal/api/world/social-posts")' in entry
    )
    # Ten-minute TTL matching the Mastodon kiosk cadence keeps the two
    # upstream fetches to one per colo per window (free-plan quota).
    assert "WORLD_SOCIAL_POSTS_TTL = 600" in entry
    assert "edge_cache_match(WORLD_SOCIAL_POSTS_CACHE_KEY)" in entry
    assert "edge_cache_put(WORLD_SOCIAL_POSTS_CACHE_KEY, resp)" in entry
    assert "world_social_feeds.REDDIT_USER_AGENT" in entry


def test_reddit_listing_normalization_bounds_and_links():
    listing = {"data": {"children": [
        {"data": {
            "id": "abc",
            "title": "  A   spaced\ntitle  ",
            "author": "someone",
            "score": "7",
            "num_comments": 3,
            "created_utc": 1752000000,
            "permalink": "/r/forkmesh/comments/abc/a_spaced_title/",
        }},
        {"data": {"title": ""}},
        {"broken": True},
    ]}}
    posts = feeds.normalize_reddit_listing(listing)
    assert len(posts) == 1
    post = posts[0]
    assert post["text"] == "A spaced title"
    assert post["score"] == 7
    assert post["comments"] == 3
    assert post["createdAt"] == 1752000000000
    assert post["url"].startswith("https://www.reddit.com/r/forkmesh/")
    # Malformed payloads normalize to an empty list, never raise.
    assert feeds.normalize_reddit_listing(None) == []
    assert feeds.normalize_reddit_listing({"data": {"children": "x"}}) == []


def test_reddit_listing_is_bounded_to_the_banner_post_limit():
    children = [
        {"data": {"id": str(i), "title": "post %d" % i,
                  "permalink": "/r/forkmesh/%d/" % i}}
        for i in range(20)
    ]
    posts = feeds.normalize_reddit_listing({"data": {"children": children}})
    assert len(posts) == feeds.SOCIAL_POSTS_LIMIT


def test_twitter_timeline_extraction_and_normalization():
    html = (
        '<html><script id="__NEXT_DATA__" type="application/json">'
        '{"props":{"pageProps":{"timeline":{"entries":['
        '{"content":{"tweet":{"id_str":"9","full_text":"hi  there",'
        '"favorite_count":3,"retweet_count":1,'
        '"created_at":1752000000000,"permalink":"/forkmesh/status/9",'
        '"user":{"screen_name":"forkmesh"}}}},'
        '{"content":{"tweet":{"id_str":"10","full_text":""}}}'
        ']}}}}</script></html>'
    )
    next_data = feeds.extract_next_data(html)
    assert isinstance(next_data, dict)
    posts = feeds.normalize_twitter_timeline(next_data)
    assert len(posts) == 1
    post = posts[0]
    assert post["text"] == "hi there"
    assert post["author"] == "@forkmesh"
    assert post["likes"] == 3 and post["retweets"] == 1
    assert post["url"] == "https://x.com/forkmesh/status/9"
    # Classic string dates still resolve to epoch milliseconds.
    assert feeds._twitter_created_ms("Mon, 01 Apr 2024 12:00:00 +0000") > 0
    # Missing or mangled markup degrades to None/[] instead of raising.
    assert feeds.extract_next_data("<html>no data</html>") is None
    assert feeds.extract_next_data(
        '<script id="__NEXT_DATA__">not json</script>') is None
    assert feeds.normalize_twitter_timeline(None) == []
    assert feeds.normalize_twitter_timeline({"props": {}}) == []


def test_payload_states_gate_each_feed_independently():
    payload = feeds.social_posts_payload(
        123, [{"id": "t"}], [{"id": "r"}], False, True)
    assert payload["ok"] is True and payload["now"] == 123
    assert payload["twitter"]["state"] == "unavailable"
    assert payload["twitter"]["posts"] == []
    assert payload["reddit"]["state"] == "ready"
    assert payload["reddit"]["posts"] == [{"id": "r"}]
    assert payload["twitter"]["url"] == "https://x.com/forkmesh"
    assert payload["reddit"]["url"] == "https://www.reddit.com/r/forkmesh/"
