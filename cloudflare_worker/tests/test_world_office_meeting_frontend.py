#!/usr/bin/env python3
"""Contracts for native multiplayer meetings inside ForkMesh Office."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public" / "world"
MEETING = PUBLIC / "world-office-meeting.js"
OFFICE = PUBLIC / "world-office.js"
SCENE = PUBLIC / "world-scene.js"
WORLD = PUBLIC / "world.js"
CSS = PUBLIC / "world.css"


def source(path):
    return path.read_text(encoding="utf-8") if path.exists() else ""


def test_native_office_meeting_module_and_world_wiring_exist():
    assert MEETING.exists()
    assert 'from "./world-office-meeting.js"' in source(WORLD)
    assert "createWorldOfficeMeeting" in source(WORLD)
    assert "meeting:" in source(WORLD)


def test_meeting_controller_exposes_room_and_seat_lifecycle():
    meeting = source(MEETING)
    for contract in (
        "export function createWorldOfficeMeeting",
        "function openLobby",
        "async function joinRoom",
        "function requestSeat",
        "function stand",
        "function move",
        "function leaveRoom",
        "function leaveOffice",
        "function destroy",
    ):
        assert contract in meeting


def test_scene_exposes_dedicated_office_interior_and_participant_apis():
    scene = source(SCENE)
    for contract in (
        "function enterOfficeLobby(",
        "function enterOfficeMeeting(",
        "function setOfficeParticipants(",
        "function setOfficeSeatState(",
        "function showOfficeBubble(",
        "function leaveOfficeInterior(",
        "officeParticipants",
        'child.userData.interactive = "office-chair"',
        "officeChairId",
        "onOfficeMovement",
    ):
        assert contract in scene


def test_lobby_is_native_and_iframe_is_only_an_explicit_fallback():
    office = source(OFFICE)
    world = source(WORLD)
    assert "meeting.openLobby()" in office
    assert "data-world-office-lobby" in world
    assert "data-world-office-room" in world
    assert "data-world-office-fallback" in world
    assert "openFallback" in office
    assert "enterOffice(trigger" not in office


def test_office_meeting_styles_cover_lobby_room_seats_and_participants():
    css = source(CSS)
    for selector in (
        ".world-office-lobby",
        ".world-office-room-board",
        ".world-office-room",
        ".world-office-seats",
        ".world-office-participants",
    ):
        assert selector in css


def test_native_meeting_uses_shared_encryption_proofs_and_attachments():
    meeting = source(MEETING)
    for contract in (
        'from "../chat-room-transport.js"',
        'from "../chat-crypto.js"',
        'from "../chat-attachments.js"',
        "createMeetingBinding",
        "signMeetingProof",
        "verifyMeetingProof",
        "fileToAttachment",
        "attachmentFromEntry",
        "persist: true",
        "scene.showOfficeBubble",
    ):
        assert contract in meeting


def test_native_meeting_has_semantic_composer_transcript_and_live_region():
    world = source(WORLD)
    for contract in (
        "data-world-office-transcript",
        "data-world-office-input",
        "data-world-office-send",
        "data-world-office-attachment",
        "data-world-office-attachment-input",
        "data-world-office-live",
        'aria-label="Attach image or document"',
    ):
        assert contract in world


def test_native_meeting_handles_clipboard_images_and_document_input():
    meeting = source(MEETING)
    assert 'input.addEventListener("paste"' in meeting
    assert "clipboardData" in meeting
    assert "getAsFile()" in meeting
    assert 'attachmentInput.addEventListener("change"' in meeting
    assert "URL.createObjectURL" in meeting
    assert "URL.revokeObjectURL" in meeting
