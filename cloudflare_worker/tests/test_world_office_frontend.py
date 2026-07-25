#!/usr/bin/env python3
"""Contracts for the spatial ForkMesh Office chat controller."""

import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OFFICE_PATH = ROOT / "public" / "world" / "world-office.js"
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"
WORLD_PATH = ROOT / "public" / "world" / "world.js"


def office_source():
    return OFFICE_PATH.read_text(encoding="utf-8") if OFFICE_PATH.exists() else ""


def test_office_controller_module_exists():
    assert OFFICE_PATH.exists()


def test_office_proximity_uses_enter_and_exit_hysteresis():
    if not OFFICE_PATH.exists():
        return
    script = f"""
      import {{ nextOfficeZoneState }} from {json.dumps(OFFICE_PATH.as_uri())};
      process.stdout.write(JSON.stringify([
        nextOfficeZoneState("distant", 6.6),
        nextOfficeZoneState("distant", 6.5),
        nextOfficeZoneState("nearby", 7.4),
        nextOfficeZoneState("nearby", 7.6),
      ]));
    """
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        text=True,
        capture_output=True,
    )
    assert json.loads(result.stdout) == [
        "distant",
        "nearby",
        "nearby",
        "distant",
    ]


def test_office_controller_validates_bridge_and_unloads_hidden_chat():
    source = office_source()
    if not source:
        return
    assert 'const OFFICE_CHAT_PATH = "/chat?embed=office"' in source
    assert "event.origin !== window.location.origin" in source
    assert "event.source !== frame.contentWindow" in source
    assert 'type: "office-chat-suspend"' in source
    assert "window.setTimeout" in source
    assert "2000" in source

    bridge_objects = re.findall(
        r"postMessage\(\s*(\{.*?\})\s*,",
        source,
        flags=re.DOTALL,
    )
    assert bridge_objects
    bridge_text = "\n".join(bridge_objects)
    for forbidden in (
        "plaintext",
        "roomKey",
        "token",
        "attachment",
        "privateChannelId",
    ):
        assert forbidden not in bridge_text


def test_office_controller_exposes_deliberate_entry_lifecycle():
    source = office_source()
    if not source:
        return
    for contract in (
        "export function createWorldOfficeController",
        "function setProximity",
        "function focusOffice",
        "function enterOffice",
        "function collapse",
        "function destroy",
        'world.focusLandmark("office")',
        "world.enterOffice()",
    ):
        assert contract in source


def test_reentry_reloads_a_chat_frame_suspended_during_the_grace_period():
    source = office_source()
    assert "let frameSuspended = false" in source
    assert "if (frameSuspended)" in source
    assert "frameSuspended = true" in source


def test_world_scene_builds_and_reports_the_interactive_office():
    source = SCENE_PATH.read_text(encoding="utf-8")
    assert "function createForkMeshOffice(" in source
    assert "office: createForkMeshOffice" in source
    assert 'userData.landmark = "office"' in source
    assert "onOfficeProximity" in source
    assert "enterOffice()" in source
    assert 'fillText("FORKMESH OFFICE"' in source


def test_office_entry_uses_shared_status_and_post_only_four_digit_access():
    source = office_source()
    assert '"/api/world/office/general/status"' in source
    assert '"/api/world/office/general/entry"' in source
    assert 'method: "GET"' in source
    assert 'method: "POST"' in source
    assert "body: JSON.stringify(code ? { code } : {})" in source
    assert r"/^\d{4}$/" in source
    assert "window.prompt" not in source
    assert "localStorage" not in source
    assert "sessionStorage" not in source
    assert "OFFICE_CODE_KEY" not in source

    submit = source[
        source.index("function onKeypadSubmit"):
        source.index("function onMessage")
    ]
    assert submit.index('keypadInput.value = "";') < submit.index(
        "requestOfficeEntry(code)")


def test_office_keypad_is_semantic_numeric_and_never_displays_the_secret():
    world = WORLD_PATH.read_text(encoding="utf-8")
    for contract in (
        "data-world-office-keypad",
        "data-world-office-keypad-form",
        "data-world-office-keypad-input",
        'type="password"',
        'inputmode="numeric"',
        'pattern="[0-9]{4}"',
        'minlength="4"',
        'maxlength="4"',
        'autocomplete="off"',
        'role="status"',
        'aria-live="polite"',
    ):
        assert contract in world
    assert "Share this code" not in world


def test_office_scene_uses_one_full_size_door_and_privacy_safe_state_metadata():
    source = SCENE_PATH.read_text(encoding="utf-8")
    office = source[
        source.index("function createForkMeshOffice("):
        source.index("function createWeather(")
    ]
    for contract in (
        "const OFFICE_WIDTH = 17;",
        "const OFFICE_DEPTH = 12;",
        "const OFFICE_HEIGHT = 7;",
        "const OFFICE_FRONT_Z = 6;",
        'door.name = "forkmesh-office-door"',
        "door.userData.officeEnter = true",
        "child.userData.officeAccessPanel = true",
        "function setOfficeOccupancy(",
        'source: "door"',
        'source: "keypad"',
        'occupied: Boolean(office?.userData?.officeOccupied)',
        "available: office?.userData?.officeAvailable !== false",
    ):
        assert contract in source
    assert office.count('door.name = "forkmesh-office-door"') == 1
    assert "group.scale.setScalar(1.35)" not in office
    assert "new THREE.BoxGeometry(OFFICE_WIDTH, 0.4, OFFICE_DEPTH)" in source
    assert "new THREE.BoxGeometry(OFFICE_WIDTH, OFFICE_HEIGHT, 0.35)" in source


def test_office_interior_is_same_scale_and_has_door_only_collision_exit():
    source = SCENE_PATH.read_text(encoding="utf-8")
    for contract in (
        "const OFFICE_DOOR_WIDTH = 2.4;",
        "const OFFICE_INTERIOR_EXIT_Z = 6.18;",
        'officeInterior.name = "forkmesh-office-interior"',
        "const officeFrontPanelWidth = (OFFICE_WIDTH - OFFICE_DOOR_WIDTH) / 2;",
        'officeInteriorDoor.name = "forkmesh-office-interior-door"',
        "function constrainTownOfficeWalls(previousPosition)",
        "function constrainOfficeInteriorWalls(avatar)",
        "Math.abs(avatar.position.x) > doorClearance",
        "Math.abs(avatar.position.x) <= doorClearance",
        "officeExitHandler();",
        "function walkOfficeLobbyPlayer(delta, time)",
    ):
        assert contract in source

    interior = source[
        source.index('officeInterior.name = "forkmesh-office-interior"'):
        source.index("const neighborhoodHomes = new Map()", source.index(
            'officeInterior.name = "forkmesh-office-interior"'
        ))
    ]
    assert "new THREE.BoxGeometry(OFFICE_WIDTH, 0.4, OFFICE_DEPTH)" in interior
    assert "new THREE.BoxGeometry(OFFICE_WIDTH, OFFICE_HEIGHT, 0.35)" in interior


def test_office_exit_is_completed_at_the_physical_door_not_by_the_ui_button():
    source = office_source()
    collapse = source[
        source.index("function collapse()"):
        source.index("async function onSceneKeypadKey")
    ]
    completion = source[
        source.index("function completeOfficeExit()"):
        source.index("function collapse()")
    ]
    assert "world.beginOfficeExit?.()" in collapse
    assert "meeting.leaveOffice()" not in collapse
    assert "meeting.leaveOffice()" in completion
    assert "world.setOfficeExitHandler?.(completeOfficeExit)" in source
    assert "world.setOfficeExitHandler?.(null)" in source


def test_walking_through_the_doorway_requests_admission_once():
    office = office_source()
    scene = SCENE_PATH.read_text(encoding="utf-8")
    for contract in (
        "let officeDoorwayEntryPending = false;",
        "let officeDoorwayEntryArmed = true;",
        "const crossedDoorway =",
        "officeDoorwayEntryArmed &&",
        "!officeDoorwayEntryPending",
        "officeDoorwayEntryArmed = false;",
        "officeDoorwayEntryPending = true;",
        'source: "doorway"',
        "function setOfficeDoorwayEntryPending(pending = false)",
        "setOfficeDoorwayEntryPending,",
    ):
        assert contract in scene

    entry = office[
        office.index("async function enterOffice(entry = {})"):
        office.index("function openFallback")
    ]
    assert 'entry?.source === "doorway"' in entry
    assert 'return await requestOfficeEntry("")' in entry
    assert "world.setOfficeDoorwayEntryPending?.(false)" in entry


def test_exterior_keypad_never_admits_an_empty_office_or_sets_occupant_code():
    office = office_source()
    entry = office[
        office.index("async function enterOffice(entry = {})"):
        office.index("function openFallback")
    ]
    assert 'if (entry?.source === "keypad")' in entry
    keypad_guard = entry[entry.index('if (entry?.source === "keypad")'):]
    assert 'world.focusOfficeKeypad?.("entry", "exterior")' in keypad_guard
    assert keypad_guard.index("return false;") < keypad_guard.index(
        'requestOfficeEntry("")'
    )

    routing = office[
        office.index("async function onSceneKeypadKey"):
        office.index("function onClick")
    ]
    assert 'location === "interior"' in routing
    assert 'openKeypad("set", "interior")' in routing
    assert 'enterOffice({ source: "keypad" })' in routing


def test_close_up_keypad_keeps_digits_local_and_supports_capability_gated_updates():
    office = office_source()
    scene = SCENE_PATH.read_text(encoding="utf-8")
    for contract in (
        "function officeKeypadDisplayTexture(",
        'mode === "set" ? "SET CODE" : "ACCESS CODE"',
        "function focusOfficeKeypad(mode = \"entry\", location = \"exterior\")",
        'setCameraMode("first-person")',
        "function setOfficeKeypadDigits(",
        "button.userData.officeKeypadDigit = keypadKeys[index]",
        'officeInteriorKeypad.name = "forkmesh-office-interior-keypad"',
        'child.userData.officeKeypadLocation = "interior"',
        "officeKeypadHandler?.(",
    ):
        assert contract in scene

    for contract in (
        'const OFFICE_CODE_PATH = "/api/world/office/general/code";',
        "payload.canSetCode === true",
        "payload.codeManagement?.canSetCode === true",
        "!occupancy.canSetCode",
        "!active",
        "!meeting.inRoom",
        "officeEntryExpiresAt <= Date.now()",
        "root.postJSON(",
        "[OFFICE_ENTRY_HEADER]: officeEntryTicket",
        "world.setOfficeKeypadHandler?.((key, context)",
    ):
        assert contract in office

    assert 'const OFFICE_ENTRY_HEADER = "X-ForkMesh-Office-Entry";' in office
    assert "method: \"PUT\"" not in office
    assert "localStorage" not in office
    assert "sessionStorage" not in office
    assert "window.prompt" not in office
