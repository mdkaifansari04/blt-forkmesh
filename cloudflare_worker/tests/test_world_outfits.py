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
        # The picker option in world-data.js...
        assert 'id: "%s"' % cut in DATA
        # ...and the painter that actually draws that cut in world-scene.js.
        assert "%s(context, rng, kit)" % cut in SCENE


def test_supporting_member_may_pin_an_outfit_cut():
    assert _presence({"outfitStyle": "circuit"})["outfitStyle"] == "circuit"


def test_guest_and_registered_outfit_cut_requests_reset_to_seeded():
    for status in ("Guest", "Registered"):
        assert _presence(
            {"outfitStyle": "circuit"}, status=status)["outfitStyle"] == ""


def test_unknown_outfit_cut_resets_to_seeded():
    assert _presence({"outfitStyle": "tuxedo"})["outfitStyle"] == ""


def test_face_image_is_a_boolean_and_supporting_member_only():
    assert _presence({"faceImage": True})["faceImage"] is True
    assert _presence({"faceImage": "yes"})["faceImage"] is False
    assert _presence({"faceImage": 1})["faceImage"] is False
    assert _presence({"faceImage": True}, status="Guest")["faceImage"] is False
    assert _presence(
        {"faceImage": True}, status="Registered")["faceImage"] is False


def test_outfit_and_face_fields_are_public_presence_fields():
    for field in ("outfitStyle", "faceImage"):
        assert field in world.WORLD_PUBLIC_FIELDS
        assert field in world.default_presence("peer1", 1000)


def test_presence_frames_never_carry_image_bytes():
    # Only the opt-in boolean travels over presence; peers resolve the actual
    # image from the already-public, edge-cached account lookup.
    assert "faceImage:" in APP
    assert "syncWorldFaceImages" in APP
    assert "/api/accounts/" in APP
    assert "avatarPng" not in SCENE
    assert "base64" not in SCENE


def test_outfits_are_tailored_from_the_public_name():
    assert "function outfitRandom" in SCENE
    assert "function countryShirtTexture" in SCENE
    assert "function tailorOutfitDetails" in SCENE
    assert "OUTFIT_CUTS" in SCENE
    # The tailor keys off the same public identity fields the badge shows.
    assert "identity.outfitStyle" in SCENE
    assert "identity.outfitColor" in SCENE
