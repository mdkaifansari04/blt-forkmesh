#!/usr/bin/env python3
"""Contracts for the Twitter, Reddit, and blog banners beside the Mastodon
kiosk."""

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"
WORLD_PATH = ROOT / "public" / "world" / "world.js"
ENTRY_PATH = SRC / "entry.py"

if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import blog_feed  # noqa: E402
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
    assert "function socialBannerTexture(" in scene
    assert "function updateSocialBanners(payload)" in scene
    # Exposed on the scene API so world.js can push snapshots.
    assert "updateSocialBanners,\n" in scene
    # Repaints dispose the previous texture like the Mastodon kiosk does.
    assert scene.count("face.material.map?.dispose?.();") >= 2
    # A feed that is not ready keeps the static sign, never a blank board.
    assert 'feed.state === "ready"' in scene


def test_repository_status_board_reuses_the_social_sign_format_near_the_office():
    scene = _source(SCENE_PATH)
    world = _source(WORLD_PATH)
    assert 'id: "status"' in scene
    assert 'title: "SYSTEM STATUS"' in scene
    assert 'host: "forkmesh.com/status"' in scene
    assert 'url: "/status"' in scene
    assert "function systemStatusBannerTexture(" in scene
    assert "payload?.systems" in scene
    assert "system.days" in scene
    assert "system.minutes" in scene
    assert "systemStatusColor(hour?.status)" in scene
    assert "systemStatusColor(minute?.status)" in scene
    assert "1640 / systems.length" in scene
    assert "systems.forEach((system, index)" in scene
    assert "systems.slice(0, 8)" not in scene
    assert "last 24 hours" in scene
    assert "last 60 one-minute checks" in scene
    assert 'registerMovableObject("status-banner", statusBanner);' in scene
    assert "updateSystemStatusBoard," in scene
    assert 'this.fetchJSON("/api/status?view=world"' in world
    assert "this.world?.updateSystemStatusBoard?.(payload)" in world


def test_world_notification_tick_uses_a_digest_before_the_full_list():
    world = _source(WORLD_PATH)
    assert "this.notificationToken" in world
    assert "`/api/poll?node=${encodeURIComponent(account)}`" in world
    assert "nextToken === this.notificationToken" in world
    assert "refreshPersonalNotifications(false, { digestOnly: true })" in world


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
        123, [{"id": "t"}], [{"id": "r"}], [{"id": "b"}], False, True, True)
    assert payload["ok"] is True and payload["now"] == 123
    assert payload["twitter"]["state"] == "unavailable"
    assert payload["twitter"]["posts"] == []
    assert payload["reddit"]["state"] == "ready"
    assert payload["reddit"]["posts"] == [{"id": "r"}]
    assert payload["blog"]["state"] == "ready"
    assert payload["blog"]["posts"] == [{"id": "b"}]
    assert payload["twitter"]["url"] == "https://x.com/forkmesh"
    assert payload["reddit"]["url"] == "https://www.reddit.com/r/forkmesh/"
    assert payload["blog"]["url"] == "https://forkmesh.com/blog"
    gated = feeds.social_posts_payload(1, [], [], [{"id": "b"}], True, True,
                                       False)
    assert gated["blog"]["state"] == "unavailable"
    assert gated["blog"]["posts"] == []


_BLOG_CARD_HTML = (
    '<a class="blog1-card" href="/blog/desktop-node-mirrors/"'
    ' data-published="2026-07-04" data-section="mesh">'
    '<img class="blog1-post-image" src="/x.webp" alt="">'
    '<div class="blog1-card-copy">'
    '<p class="blog1-card-meta">Distributed hosting &amp; more · Feature 01'
    '</p>\n<h3>Desktop-node mirrors</h3>\n'
    '<p>Actively   mirrored\nrepositories live on independent nodes.</p>'
    '</div></a>'
)


def test_blog_feed_normalization_carries_preview_text_and_artwork():
    posts = feeds.normalize_blog_feed(
        blog_feed.build_feed(_BLOG_CARD_HTML, 1_700_000_000_000))
    assert len(posts) == 1
    post = posts[0]
    assert post["id"] == "desktop-node-mirrors"
    assert post["text"] == "Desktop-node mirrors"
    assert post["meta"] == "Distributed hosting & more · Feature 01"
    # The board prints the feed item's description under the headline, and
    # paints its enclosure image as the card's artwork.
    assert post["detail"] == (
        "Actively mirrored repositories live on independent nodes.")
    assert post["image"] == "https://forkmesh.com/x.webp"
    # The item's pubDate rides through as createdAt: the board's plate
    # reads how long ago the newest post went up.
    assert post["createdAt"] == 1783123200000
    undated = feeds.normalize_blog_feed(blog_feed.build_feed(
        _BLOG_CARD_HTML.replace(' data-published="2026-07-04"', "")))
    assert undated[0]["createdAt"] == 0
    assert post["url"] == "https://forkmesh.com/blog/desktop-node-mirrors/"
    # Malformed documents degrade to an empty list, never raise.
    assert feeds.normalize_blog_feed(None) == []
    assert feeds.normalize_blog_feed("<rss><channel/></rss>") == []


def test_blog_feed_is_bounded_to_the_banner_post_limit():
    document = blog_feed.build_feed(_BLOG_CARD_HTML * 20)
    posts = feeds.normalize_blog_feed(document)
    assert len(posts) == feeds.SOCIAL_POSTS_LIMIT


def test_blog_feed_parses_the_shipped_blog_page():
    blog_html = _source(ROOT / "public" / "blog.html")
    posts = feeds.normalize_blog_feed(blog_feed.build_feed(blog_html))
    assert len(posts) == feeds.SOCIAL_POSTS_LIMIT
    assert all(post["text"] and post["meta"] for post in posts)
    assert all(post["url"].startswith("https://forkmesh.com/blog/")
               for post in posts)
    # Every shipped card has artwork, so every board card gets an image.
    assert all(post["image"].startswith("https://forkmesh.com/assets/")
               for post in posts)


def test_scene_places_the_blog_banner_on_the_social_row():
    scene = _source(SCENE_PATH)
    assert "BLOG_BANNER_OPTIONS" in scene
    assert "position: [19.8, 0, -32.8]" in scene
    assert 'registerMovableObject("blog-banner", blogBanner);' in scene
    assert '"https://forkmesh.com/blog"' in scene


def test_banners_carry_sync_and_staleness_plates():
    scene = _source(SCENE_PATH)
    # Each banner stand carries the kiosk's two plates: the MM:SS countdown
    # to the next feed sync and the color-coded staleness readout.
    assert "banner-countdown" in scene
    assert "banner-lastpost" in scene
    assert "function updateSocialBannerTimers(payload)" in scene
    assert "updateSocialBannerTimers,\n" in scene
    # Every board, blog included, reads its own newest post's age.
    assert 'label: "SYNCED"' not in scene
    assert scene.count('label: "LAST POST"') == 3
    # The blog runs the same color rule on a blog's slower cadence.
    assert "const BLOG_POST_FRESH_MS = 7 * 24 * 60 * 60 * 1000;" in scene
    assert "const BLOG_POST_STALE_MS = 21 * 24 * 60 * 60 * 1000;" in scene
    assert "freshMs: BLOG_POST_FRESH_MS," in scene


def test_world_drives_the_banner_clocks_on_a_one_second_tick():
    world = _source(WORLD_PATH)
    assert "syncSocialBannerTimers()" in world
    assert "this.world?.updateSocialBannerTimers?.(" in world
    assert "socialNewestPostAgo(feed)" in world
    assert "blog: timers(this.socialNewestPostAgo(snapshot?.blog))," in world
    # The tick, not a ten-minute interval, drives the reload so the countdown
    # and the fetch can never drift apart.
    idx = world.index("startSocialBannersRefresh() {")
    assert "1000" in world[idx:idx + 300]


def test_worker_folds_the_blog_feed_into_the_social_snapshot():
    entry = _source(ENTRY_PATH)
    assert "blog_feed.BLOG_INDEX_ASSET" in entry
    # The board reads the same RSS document the public feed serves.
    assert "world_social_feeds.normalize_blog_feed(blog_rss)" in entry
    # The blog read stays inside the Worker's own static assets.
    idx = entry.index("blog_feed.BLOG_INDEX_ASSET")
    assert "env.ASSETS.fetch" in entry[idx - 400:idx]


def test_blog_board_draws_feed_artwork_and_preview_text():
    scene = _source(SCENE_PATH)
    world = _source(WORLD_PATH)
    # The blog board is the one that carries per-post art.
    assert "postArt: true" in scene
    assert scene.count("postArt: true") == 1
    assert "function drawSocialBannerPostCard(" in scene
    # Artwork is cover-cropped into the card tile and only drawn once the
    # image decodes CORS-clean; otherwise the placeholder plate stays.
    assert 'image.crossOrigin = "anonymous";' in scene
    assert "media?.naturalWidth > 0 && media?.naturalHeight > 0" in scene
    # world.js forwards the item's preview text and image to the board, with
    # off-site image URLs dropped so the canvas is never tainted.
    assert "socialPostImage(value)" in world
    assert 'url.pathname.startsWith("/assets/")' in world
    assert "detail: String(post?.detail || \"\")" in world
