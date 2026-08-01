#!/usr/bin/env python3
"""Name-tailored outfits and the Supporting-member outfit/face perks."""

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORLD_PATH = ROOT / "src" / "world.py"
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8")
APP = (ROOT / "public" / "world" / "world.js").read_text(encoding="utf-8")
DATA = (ROOT / "public" / "world" / "world-data.js").read_text(
    encoding="utf-8")

spec = importlib.util.spec_from_file_location(
    "forkmesh_world_protocol", WORLD_PATH)
world = importlib.util.module_from_spec(spec)
spec.loader.exec_module(world)


def _presence(payload, status="Supporting member"):
    state = world.default_presence("peer1", 1000)
    state["accountStatus"] = status
    result = world.sanitize_message(
        {"type": "presence", **payload}, state, 2000)
    assert result is not None
    return result[1]


def test_outfit_cut_ids_stay_in_lockstep_across_protocol_data_and_scene():
    cuts = sorted(world.WORLD_OUTFIT_STYLE_VALUES - {""})
    assert cuts
    for cut in cuts:

        assert 'id: "%s"' % cut in DATA

        assert "%s(context, rng, kit)" % cut in SCENE


def test_supporting_member_may_pin_an_outfit_cut():
    assert _presence({"outfitStyle": "circuit"})["outfitStyle"] == "circuit"


def test_guest_and_registered_outfit_cut_requests_reset_to_seeded():
    for status in ("Guest", "Registered"):
        assert _presence(
            {"outfitStyle": "circuit"}, status=status)["outfitStyle"] == ""


def test_unknown_outfit_cut_resets_to_seeded():
    assert _presence({"outfitStyle": "tuxedo"})["outfitStyle"] == ""


def test_face_image_is_a_boolean_for_authenticated_members_only():
    assert _presence({"faceImage": True})["faceImage"] is True
    assert _presence({"faceImage": "yes"})["faceImage"] is False
    assert _presence({"faceImage": 1})["faceImage"] is False
    assert _presence({"faceImage": True}, status="Guest")["faceImage"] is False
    assert _presence(
        {"faceImage": True}, status="Registered")["faceImage"] is True


def test_outfit_and_face_fields_are_public_presence_fields():
    for field in ("outfitStyle", "faceImage"):
        assert field in world.WORLD_PUBLIC_FIELDS
        assert field in world.default_presence("peer1", 1000)


def test_presence_frames_never_carry_image_bytes():


    assert "faceImage:" in APP
    assert "syncWorldFaceImages" in APP
    assert "/api/accounts/" in APP
    assert "avatarPng" not in SCENE
    assert "base64" not in SCENE


def test_every_identity_has_a_stable_generated_face_and_verified_pin():
    assert "function proceduralAvatarFaceTexture" in SCENE
    assert "faceIdentityKey: identity.id || identity.name" in SCENE
    assert 'verifiedPin.name = "forkmesh-verified-email-pin"' in SCENE
    assert "identity?.emailVerified === true" in SCENE


def test_signed_in_members_can_upload_a_compact_face_photo():
    assert "function compactWorldAvatar" in APP
    assert "WORLD_AVATAR_MAX_BYTES = 64 * 1024" in APP
    assert "for (const size of [128, 112, 96, 80, 64, 48])" in APP
    assert "data-world-avatar-upload" in APP
    assert "Change and save avatar" in APP
    assert '"/api/accounts/profile"' in APP
    assert "storeWorldSession({" in APP
    assert "this.updateIdentityUI();" in APP
    assert "this.identity.accountStatus === \"Guest\"" in APP


def test_outfits_are_tailored_from_the_public_name():
    assert "function outfitRandom" in SCENE
    assert "function countryShirtTexture" in SCENE
    assert "function tailorOutfitDetails" in SCENE
    assert "OUTFIT_CUTS" in SCENE

    assert "identity.outfitStyle" in SCENE
    assert "identity.outfitColor" in SCENE
