#!/usr/bin/env python3
"""Contracts for the ambient marine aquarium in the Office lobby."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCENE = (ROOT / "public" / "world" / "world-scene.js").read_text(
    encoding="utf-8"
)
CSS = (ROOT / "public" / "world" / "world.css").read_text(encoding="utf-8")


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
    assert "feed," in aquarium
    assert "updateFeeding," in aquarium
    assert "getFeedingState," in aquarium
    assert "setTimeout(" not in aquarium
    assert "setInterval(" not in aquarium


def test_aquarium_feeding_action_is_proximity_scoped_and_accessible():
    assert 'aquariumFeedAction.type = "button"' in SCENE
    assert 'aquariumFeedAction.textContent = "FEED"' in SCENE
    assert 'aquariumFeedAction.dataset.worldAquariumFeed = ""' in SCENE
    assert "updateOfficeAquariumProximity" in SCENE
    assert 'removeEventListener("click", handleAquariumFeed)' in SCENE


def test_aquarium_bottom_right_control_panel_combines_all_three_actions():
    assert 'aquariumControlPanel.dataset.worldAquariumControls = ""' in SCENE
    assert 'aquariumControlTitle.textContent = "REEF CONTROL"' in SCENE
    assert 'aquariumBackdropAction.dataset.worldAquariumBackdrop = ""' in SCENE
    assert 'aquariumLightAction.dataset.worldAquariumLight = ""' in SCENE
    assert "handleAquariumBackdrop" in SCENE
    assert "handleAquariumLight" in SCENE
    assert "setBackdropOpaque(!current)" in SCENE
    assert "setLightEnabled(!current)" in SCENE
    # It chooses the projected screen-right end of the tank rather than
    # following the player's position along the glass.
    assert "aquariumControlCandidate.x >= aquariumControlOpposite.x" in SCENE
    assert "aquariumControlLocalPosition.z -" not in SCENE
    assert ".world-aquarium-control-panel" in CSS
    assert "grid-template-columns: repeat(3" in CSS
    assert ".world-aquarium-feed-action" not in CSS


def test_aquarium_background_and_light_are_real_scene_toggles():
    aquarium = _aquarium_block()
    assert "function setBackdropOpaque(value)" in aquarium
    assert "backdropMaterial.opacity = backdropOpaque ? 1 : 0.12" in aquarium
    assert "function setLightEnabled(value" in aquarium
    assert "waterLight.intensity = lightAmount *" in aquarium
    assert "spillLight.intensity = lightAmount *" in aquarium
    assert "sandLight.intensity = lightAmount *" in aquarium
    assert "topLight.visible = aquariumLightEnabled" in aquarium


def test_each_public_user_gets_one_small_name_seeded_fish_in_an_activity_lane():
    aquarium = _aquarium_block()
    assert "function aquariumUserPalette(name)" in aquarium
    assert '"outfit:" + String(name || "guest")' in aquarium
    assert "OUTFIT_COLORWAYS[rng.int(OUTFIT_COLORWAYS.length)]" in aquarium
    assert "function setUsers(users = [])" in aquarium
    assert "normalized.forEach((user, index) =>" in aquarium
    assert "fishStates.push({" in aquarium
    assert '"active-recent"' in aquarium
    assert '"inactive"' in aquarium
    assert "const laneMin = recent ? 6.8 : 1.65" in aquarium
    assert "Math.log2(population + 1)" in aquarium
    assert "officeAquarium.setUsers(members)" in SCENE
    # Names seed appearance but are not drawn or attached as visible labels.
    assert "makeLabelSprite" not in aquarium
    assert "fillText(user.name" not in aquarium
