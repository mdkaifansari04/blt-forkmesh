#!/usr/bin/env python3
"""Contracts for the ambient marine aquarium in the Office lobby."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)


def _aquarium_block():
    start = SCENE.index("function createOfficeMarineAquarium")
    end = SCENE.index("function createForkMeshOffice", start)
    return SCENE[start:end]


def test_office_lobby_has_a_bioluminescent_left_wall_marine_aquarium():
    aquarium = _aquarium_block()
    assert 'group.name = "forkmesh-office-marine-aquarium"' in aquarium
    assert 'group.position.set(-82.9, 0, -10.5)' in aquarium
    for name in (
        "forkmesh-office-aquarium-frame",
        "forkmesh-office-aquarium-water",
        "forkmesh-office-aquarium-sand",
        "forkmesh-office-aquarium-coral",
        "forkmesh-office-aquarium-anemone",
        "forkmesh-office-aquarium-fish",
        "forkmesh-office-aquarium-bubbles",
        "forkmesh-office-aquarium-light",
    ):
        assert name in aquarium
    assert "createOfficeMarineAquarium(THREE, animated)" in SCENE


def test_marine_aquarium_stays_ambient_and_reuses_the_scene_animation_queue():
    aquarium = _aquarium_block()
    assert "interactive.push" not in aquarium
    assert "userData.interactive" not in aquarium
    assert "userData.officeFloorId" not in aquarium
    assert "animated.push((time) =>" in aquarium
    assert "Math.sin(time" in aquarium
    assert "fish.position" in aquarium
    assert "bubble.position.y" in aquarium
    assert "coral.rotation.z" in aquarium
    assert "anemone.rotation.z" in aquarium


def test_marine_fish_have_species_detail_and_individual_wandering_paths():
    aquarium = _aquarium_block()
    assert "new THREE.SphereGeometry(0.5, 12, 8)" in aquarium
    assert "forkmesh-office-aquarium-fish-eye" in aquarium
    assert "forkmesh-office-aquarium-fish-dorsal-fin" in aquarium
    assert "forkmesh-office-aquarium-fish-pectoral-fin" in aquarium
    assert "forkmesh-office-aquarium-fish-stripe" in aquarium
    assert "fishTailGeometry.setAttribute" in aquarium
    assert "state.wander" in aquarium
    assert "Math.sin(swim * 2.17 + state.wander)" in aquarium
    assert "Math.atan2(velocityX, velocityZ)" in aquarium
