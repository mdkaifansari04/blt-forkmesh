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
    assert "function createOfficeMarineAquarium(THREE, animated, maxFish = 24)" in SCENE
    assert "compactRenderer ? 12 : 24" in SCENE


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


def test_aquarium_feeding_action_is_persistently_available_and_accessible():
    assert 'aquariumFeedAction.type = "button"' in SCENE
    assert 'aquariumFeedAction.textContent = "FEED"' in SCENE
    assert 'aquariumFeedAction.dataset.worldAquariumFeed = ""' in SCENE
    assert "updateOfficeAquariumProximity" in SCENE
    assert "officeAquarium.controlAnchor" in SCENE
    assert "updateScreenLabel(" in SCENE
    assert 'removeEventListener("click", handleAquariumFeed)' in SCENE


def test_aquarium_bottom_right_control_panel_combines_all_four_actions():
    assert 'aquariumControlPanel.dataset.worldAquariumControls = ""' in SCENE
    assert 'aquariumControlToggle.textContent = "REEF CONTROL"' in SCENE
    assert 'aquariumControlToggle.type = "button"' in SCENE
    assert 'aquariumControlToggle.setAttribute("aria-expanded", "false")' in SCENE
    assert 'aquariumControlActions.hidden = true' in SCENE
    assert "setAquariumControlsExpanded" in SCENE
    assert "handleAquariumControlToggle" in SCENE
    assert 'aquariumTapAction.dataset.worldAquariumTap = ""' in SCENE
    assert 'aquariumTapAction.textContent = "TAP GLASS"' in SCENE
    assert 'aquariumBackdropAction.dataset.worldAquariumBackdrop = ""' in SCENE
    assert 'aquariumLightAction.dataset.worldAquariumLight = ""' in SCENE
    assert "handleAquariumTap" in SCENE
    assert "officeAquarium.tapGlass()" in SCENE
    assert "handleAquariumBackdrop" in SCENE
    assert "handleAquariumLight" in SCENE
    assert "setBackdropOpaque(!current)" in SCENE
    assert "setLightEnabled(!current)" in SCENE
    # The exhibit controls remain pinned within the lobby viewport instead of
    # disappearing when the player leaves the narrow glass-side trigger.
    assert '"forkmesh-office-aquarium-control-anchor"' in SCENE
    assert "officeAquarium.controlAnchor" in SCENE
    assert "aquariumDistance / 9" in SCENE
    assert ".world-aquarium-control-panel" in CSS
    assert ".world-aquarium-control-toggle" in CSS
    assert ".world-aquarium-control-actions" in CSS
    assert "bottom: calc(100% + 7px)" in CSS
    assert "grid-template-columns: repeat(2" in CSS
    assert ".world-aquarium-feed-action" not in CSS


def test_aquarium_fish_approach_a_visitor_gently_without_a_second_loop():
    aquarium = _aquarium_block()
    assert "let visitorReaction = 0" in aquarium
    assert "let visitorReactionTarget = 0" in aquarium
    assert "function selectVisitorFish()" in aquarium
    assert "visitorApproachCount" in aquarium
    assert "aquariumFishApproachesVisitor(index)" in aquarium
    assert "setVisitorProximity(value, target = {})" in aquarium
    assert "1 - Math.exp(-elapsed / 720)" in aquarium
    assert "fish.position.lerp(" in aquarium
    assert "state.visitorPoint" in aquarium
    assert "reaction * 0.68" not in aquarium
    assert "fish.position.x -= reaction" not in aquarium
    assert "setVisitorProximity(" in SCENE


def test_tap_reverses_routes_continuously_and_one_close_fish_puffs_its_mouth():
    aquarium = _aquarium_block()
    assert "function tapGlass(time = performance.now())" in aquarium
    assert "state.direction *= -1" in aquarium
    assert "progress - now * state.speed * state.direction" in aquarium
    assert "AQUARIUM_GLASS_TAP_REACTION_MS = 2_600" in aquarium
    assert "const mouthPuff =" in aquarium
    assert "index === visitorFocusIndex" in aquarium
    assert "state.mouth.scale.setScalar(1 + mouthPuff)" in aquarium
    assert "tapGlass," in aquarium


def test_aquarium_background_and_light_are_real_scene_toggles():
    aquarium = _aquarium_block()
    assert "function setBackdropOpaque(value)" in aquarium
    assert "backdropMaterial.opacity = backdropOpaque ? 1 : 0.12" in aquarium
    assert "function setLightEnabled(value" in aquarium
    assert "waterLight.intensity = lightAmount *" in aquarium
    assert "spillLight.intensity = lightAmount *" in aquarium
    assert "sandLight.intensity = lightAmount *" in aquarium
    assert "topLight.visible = aquariumLightEnabled" in aquarium


def test_public_users_seed_a_bounded_activity_first_school():
    aquarium = _aquarium_block()
    assert "function aquariumUserPalette(name)" in aquarium
    assert '"outfit:" + String(name || "guest")' in aquarium
    assert "OUTFIT_COLORWAYS[rng.int(OUTFIT_COLORWAYS.length)]" in aquarium
    assert "function setUsers(users = [])" in aquarium
    assert ".slice(0, fishLimit)" in aquarium
    assert "visibleUsers.forEach((user, index) =>" in aquarium
    assert "group.userData.accountPopulation = normalized.length" in aquarium
    assert "group.userData.visibleFish = visibleUsers.length" in aquarium
    assert "group.userData.fishLimit = fishLimit" in aquarium
    assert "fishStates.push({" in aquarium
    assert '"active-recent"' in aquarium
    assert '"inactive"' in aquarium
    assert "const laneMin = recent ? 6.8 : 1.65" in aquarium
    assert "Math.log2(population + 1)" in aquarium
    assert "officeAquarium.setUsers(members)" in SCENE
    # Names seed appearance but are not drawn or attached as visible labels.
    assert "makeLabelSprite" not in aquarium
    assert "fillText(user.name" not in aquarium


def test_aquarium_animation_is_lobby_only_and_capped_at_twenty_hertz():
    aquarium = _aquarium_block()
    assert "const AQUARIUM_ANIMATION_MS = 1000 / 20" in aquarium
    assert "if (!animationActive || time < nextAnimationAt) return" in aquarium
    assert "nextAnimationAt + AQUARIUM_ANIMATION_MS" in aquarium
    assert "setAnimationActive(value)" in aquarium
    assert 'officeSceneMode === "lobby"' in SCENE
    assert 'officeCurrentFloorId === "lobby"' in SCENE
    assert "officeAquarium.setAnimationActive(aquariumAnimationActive)" in SCENE
