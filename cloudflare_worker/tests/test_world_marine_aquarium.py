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


def test_marine_fish_have_smooth_species_detail_and_individual_curved_paths():
    aquarium = _aquarium_block()
    assert "new THREE.LatheGeometry(profile, 24)" in aquarium
    assert "forkmesh-office-aquarium-fish-eye" in aquarium
    assert "forkmesh-office-aquarium-fish-dorsal-fin" in aquarium
    assert "forkmesh-office-aquarium-fish-pectoral-fin" in aquarium
    assert "createAquariumFishTexture" in aquarium
    assert "new THREE.CatmullRomCurve3" in aquarium
    assert "Math.atan2(state.tangent.x, state.tangent.z)" in aquarium


def test_cinematic_reef_builds_recognizable_fish_species():
    aquarium = _aquarium_block()
    for species in (
        "clownfish",
        "blue-tang",
        "yellow-tang",
        "royal-gramma",
        "butterflyfish",
        "chromis",
    ):
        assert f'id: "{species}"' in aquarium
        assert f"forkmesh-office-aquarium-fish-{species}" in aquarium
    for detail in (
        "createAquariumFishTexture",
        "createReefFishBodyGeometry",
        "forkmesh-office-aquarium-fish-gill",
        "forkmesh-office-aquarium-fish-mouth",
        "forkmesh-office-aquarium-fish-iris",
        "forkmesh-office-aquarium-fish-highlight",
    ):
        assert detail in aquarium


def test_cinematic_reef_contains_layered_habitat_families():
    aquarium = _aquarium_block()
    for helper in (
        "createAquariumLiveRock",
        "createBranchingCoral",
        "createPlateCoral",
        "createBrainCoral",
        "createSeaFan",
        "createSoftCoral",
        "createAquariumAnemone",
        "createSeaGrass",
    ):
        assert helper in aquarium
    for name in (
        "forkmesh-office-aquarium-live-rock",
        "forkmesh-office-aquarium-staghorn",
        "forkmesh-office-aquarium-plate-coral",
        "forkmesh-office-aquarium-brain-coral",
        "forkmesh-office-aquarium-sea-fan",
        "forkmesh-office-aquarium-soft-coral",
        "forkmesh-office-aquarium-seagrass",
    ):
        assert name in aquarium


def test_cinematic_reef_uses_curved_motion_and_initialized_atmosphere():
    aquarium = _aquarium_block()
    assert "new THREE.CatmullRomCurve3" in aquarium
    assert "curve.getPointAt" in aquarium
    assert "curve.getTangentAt" in aquarium
    assert "initializeAquariumState(0)" in aquarium
    assert "createAquariumCausticTexture" in aquarium
    assert "forkmesh-office-aquarium-particles" in aquarium
    assert "forkmesh-office-aquarium-water-surface" in aquarium
    assert "forkmesh-office-aquarium-caustics" in aquarium


def test_aquarium_registers_a_lobby_only_collision_footprint():
    assert "OFFICE_AQUARIUM_BOUNDS" in SCENE
    assert "function officeScenePointIsWalkable" in SCENE
    assert 'floorId !== "lobby"' in SCENE
    assert "officeInteriorPointIsWalkable(" in SCENE
    assert "pointHitsOfficeAquarium(" in SCENE


def test_aquarium_feeding_is_timed_and_reuses_scene_animation():
    aquarium = _aquarium_block()
    assert "AQUARIUM_FEEDING_DURATION_MS = 60_000" in aquarium
    assert 'food.name = "forkmesh-office-aquarium-food"' in aquarium
    assert "function feed(time = performance.now())" in aquarium
    assert "function updateFeeding(time, animate = true)" in aquarium
    assert "feedingBlend" in aquarium
    assert "return { group, feed, updateFeeding, getFeedingState }" in aquarium
    assert "setTimeout(" not in aquarium
    assert "setInterval(" not in aquarium
