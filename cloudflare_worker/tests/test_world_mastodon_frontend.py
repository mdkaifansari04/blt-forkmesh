#!/usr/bin/env python3
"""Contracts for the Mastodon kiosk and mini-app beside the ForkMesh Office."""

import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
MASTODON_PATH = ROOT / "public" / "world" / "world-mastodon.js"
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"
WORLD_PATH = ROOT / "public" / "world" / "world.js"
CSS_PATH = ROOT / "public" / "world" / "world.css"


def _source(path):
    return path.read_text(encoding="utf-8") if path.exists() else ""


def _node(script):
    return json.loads(
        subprocess.run(
            ["node", "--input-type=module", "-e", script],
            check=True,
            text=True,
            capture_output=True,
        ).stdout
    )


def test_mastodon_module_exists():
    assert MASTODON_PATH.exists()


def test_scene_places_a_clickable_kiosk_beside_the_office():
    scene = _source(SCENE_PATH)
    assert "createMastodonKiosk" in scene
    assert '"mastodon-board"' in scene
    assert "onMastodonBoardSelect = () => {}" in scene
    assert 'userData?.interactive === "mastodon-board"' in scene
    # The kiosk stands beside the Office approach at [45, 0, -27], not in the
    # arrival grid or in front of the Office door / exterior keypad.
    assert "group.position.set(33.5, 0, -18.5);" in scene


def test_world_opens_the_mini_app_and_fetches_without_credentials():
    world = _source(WORLD_PATH)
    assert "onMastodonBoardSelect: () => this.openMastodonBoard()" in world
    assert 'from "./world-mastodon.js"' in world
    assert 'credentials: "omit"' in world
    assert "MASTODON_LOOKUP_URL" in world
    assert "exclude_replies=true" in world
    assert 'class="world-mastodon-app"' in world
    assert "data-world-mastodon-toots" in world
    assert "data-world-mastodon-retry" in world
    # Untrusted remote text is always escaped before injection.
    assert re.search(r"escapeHTML\(\s*status\.text,?\s*\)", world)


def test_kiosk_refreshes_every_ten_minutes_behind_an_mmss_timer():
    world = _source(WORLD_PATH)
    scene = _source(SCENE_PATH)
    assert "const MASTODON_REFRESH_MS = 10 * 60 * 1000;" in world
    assert "Date.now() - this.mastodonFetchedAt < MASTODON_REFRESH_MS" in world
    assert "startMastodonRefresh()" in world
    assert "this.startMastodonRefresh();" in world
    assert "window.clearInterval(this.mastodonRefreshTimer);" in world
    assert "updateMastodonCountdown" in world
    # The countdown runs from the attempt so a failed fetch cannot hot-loop.
    assert "this.mastodonRequestedAt = Date.now();" in world
    assert "MASTODON_KIOSK_REFRESH_MS = 10 * 60 * 1000" in scene
    assert "function mastodonCountdownTexture(" in scene
    assert '"forkmesh-mastodon-kiosk-countdown"' in scene
    assert "function updateMastodonCountdown(" in scene
    assert "updateMastodonCountdown," in scene
    # A plain MM:SS readout: no ring, no arc, no progress sweep.
    assert "function mastodonCountdownClock(" in scene
    countdown = scene.split("function mastodonCountdownClock(", 1)[1].split(
        "function createMastodonKiosk(", 1
    )[0]
    assert "context.arc(" not in countdown
    assert "progress" not in countdown
    assert 'String(seconds % 60).padStart(' in countdown


def test_countdown_clock_formats_mm_ss():
    scene = _source(SCENE_PATH)
    body = scene.split("function mastodonCountdownClock(", 1)[1].split("\n}\n", 1)[0]
    clock = _node(
        "function mastodonCountdownClock(" + body + "\n}\n"
        "process.stdout.write(JSON.stringify(["
        "mastodonCountdownClock(0),"
        "mastodonCountdownClock(59_000),"
        "mastodonCountdownClock(10 * 60 * 1000),"
        "]));"
    )
    assert clock == ["00:00", "00:59", "10:00"]


def test_kiosk_and_mini_app_carry_replies_with_author_icons():
    world = _source(WORLD_PATH)
    scene = _source(SCENE_PATH)
    # Replies are collected from the thread context of the newest toots.
    assert "async fetchMastodonReplies(" in world
    assert "/context`" in world
    assert "reply.authorAcct === account.acct" in world
    assert "const MASTODON_REPLY_LIMIT = 12;" in world
    assert "replies: this.mastodonReplies.map(" in world
    assert "avatar: reply.authorAvatar," in world
    # Mini-app section plus the kiosk strip, each with the replier's icon.
    assert "mastodonRepliesHTML()" in world
    assert "data-world-mastodon-replies" in world
    assert re.search(r"escapeHTML\(\s*reply\.text,?\s*\)", world)
    assert re.search(r"escapeHTML\(\s*reply\.authorAvatar,?\s*\)", world)
    assert "const MASTODON_KIOSK_VISIBLE_REPLIES = 3;" in scene
    assert 'context.fillText("REPLIES", 56, 1750);' in scene
    assert "const icon = image(reply.avatar);" in scene
    assert "Array.isArray(snapshot.replies) ? snapshot.replies : []" in scene


def test_kiosk_board_is_larger_and_carries_post_images():
    world = _source(WORLD_PATH)
    scene = _source(SCENE_PATH)
    assert "images: status.images.map((media) => media.url)" in world
    assert "const MASTODON_KIOSK_WIDTH = 1536;" in scene
    assert "const MASTODON_KIOSK_HEIGHT = 2048;" in scene
    # Board plane and canvas keep the same 0.75 aspect ratio.
    assert "new THREE.PlaneGeometry(7.2, 9.6)" in scene
    assert "new THREE.BoxGeometry(7.9, 10.4, 0.36)" in scene
    assert "Array.isArray(toot.images) ? toot.images : []" in scene


def test_css_keeps_toots_scrollable_under_the_profile():
    css = _source(CSS_PATH)
    assert ".world-mastodon-app" in css
    assert ".world-mastodon-toot-list" in css
    assert "overflow-y: auto;" in css


def test_lookup_endpoint_targets_the_public_forkmesh_account():
    module = _source(MASTODON_PATH)
    assert (
        "https://mastodon.social/api/v1/accounts/lookup?acct=forkmesh" in module
    )
    assert "https://mastodon.social/@forkmesh" in module


def test_account_normalization_bounds_and_flattens_remote_html():
    account = _node(
        f"""
      import {{ normalizeMastodonAccount }} from {json.dumps(MASTODON_PATH.as_uri())};
      process.stdout.write(JSON.stringify(normalizeMastodonAccount({{
        id: "114",
        acct: "forkmesh",
        display_name: "Forkmesh",
        url: "https://mastodon.social/@forkmesh",
        avatar_static: "http://insecure.example/a.png",
        header_static: "https://files.mastodon.social/header.png",
        note: "<p>Community-owned <b>Git forge</b> &amp; mesh</p>",
        followers_count: 157,
        following_count: 1400,
        statuses_count: 79,
        created_at: "2025-07-03T00:00:00.000Z",
        fields: [{{
          name: "Website",
          value: '<a href="https://forkmesh.com">forkmesh.com</a>',
          verified_at: "2025-07-04T00:00:00.000Z",
        }}],
      }})));
    """
    )
    assert account["note"] == "Community-owned Git forge & mesh"
    assert account["avatar"] == ""  # http:// is rejected
    assert account["header"] == "https://files.mastodon.social/header.png"
    assert account["followersCount"] == 157
    assert account["fields"] == [
        {
            "name": "Website",
            "value": "forkmesh.com",
            "url": "https://forkmesh.com/",
            "verified": True,
        }
    ]


def test_status_normalization_unwraps_boosts_and_strips_markup():
    status = _node(
        f"""
      import {{ normalizeMastodonStatus }} from {json.dumps(MASTODON_PATH.as_uri())};
      process.stdout.write(JSON.stringify(normalizeMastodonStatus({{
        id: "9",
        created_at: "2026-07-04T00:00:00.000Z",
        reblog: {{
          content: "<p>Hello <script>alert(1)</script>Fediverse</p>",
          url: "javascript:alert(1)",
          created_at: "2026-07-01T00:00:00.000Z",
          replies_count: 3,
          account: {{ acct: "friend@example.social", display_name: "Friend" }},
          media_attachments: [
            {{ type: "image", preview_url: "https://files.example/p.png", description: "alt" }},
            {{ type: "video", url: "https://files.example/v.mp4" }},
          ],
        }},
      }})));
    """
    )
    assert status["boostedFrom"] == "friend@example.social"
    assert "<" not in status["text"] and "Fediverse" in status["text"]
    assert status["url"] == ""  # non-https status URLs are rejected
    assert status["images"] == [
        {"url": "https://files.example/p.png", "alt": "alt"}
    ]
    assert status["repliesCount"] == 3


def test_deploy_freshness_check_covers_the_mastodon_module():
    deploy = _source(ROOT / "deploy.sh")
    assert "public/world/world-mastodon.js" in deploy
