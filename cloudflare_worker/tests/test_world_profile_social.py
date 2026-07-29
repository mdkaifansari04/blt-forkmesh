#!/usr/bin/env python3
"""Contracts for the authenticated World profile social controls."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")


def _handler_block():
    start = ENTRY.index("async def world_profile_social_handler")
    end = ENTRY.index(
        "\n\n# --- Inbound: the shared/actor inbox", start
    )
    return ENTRY[start:end]


def test_profile_social_route_is_authenticated_and_self_scoped():
    handler = _handler_block()
    assert '"/api/world/profile-social"' in ENTRY
    assert "_account_session_record(env, request, data)" in handler
    assert '"invalid_session"' in handler
    assert "WHERE target_bi=?" in handler
    assert "account_bi)" in handler
    assert '"avatarPng": follower.get("avatar_png", "")' in handler
    assert '"avatarUrl": ap.public_media_url(' in handler
    assert "LIMIT 24" in handler


def test_profile_update_is_durable_before_delivery_and_carries_alt_text():
    handler = _handler_block()
    object_insert = handler.index("INSERT INTO ap_objects")
    outbox_insert = handler.index("INSERT INTO ap_outbox")
    drain = handler.index("_ap_drain_outbox")
    assert object_insert < outbox_insert < drain
    assert "ap.extract_body_images(" in handler
    assert 'images[0]["name"] = alt_text' in handler
    assert 'item["name"] = image.get("name", "")' in handler
    assert '"kind": "user-update"' in handler
    assert '"world.activitypub.publish"' in handler
    assert "published>=?" in handler
    assert "publish_rate_limited" in handler


def test_profile_update_uses_the_authenticated_user_actor_only():
    handler = _handler_block()
    assert "_ap_user_federates(env, name)" in handler
    assert "_ap_local_actor(env, AP_ACTOR_USER, name, create=True)" in handler
    assert '"actorKind": AP_ACTOR_USER' in handler
    assert '"actorHandle": name' in handler
    assert "data.get(\"name\"" not in handler
