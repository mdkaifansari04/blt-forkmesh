#!/usr/bin/env python3
"""Contracts for native multiplayer meetings inside ForkMesh Office."""

import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "public" / "world"
MEETING = PUBLIC / "world-office-meeting.js"
OFFICE = PUBLIC / "world-office.js"
SCENE = PUBLIC / "world-scene.js"
WORLD = PUBLIC / "world.js"
CSS = PUBLIC / "world.css"
WORLD_PROTOCOL = ROOT / "src" / "world.py"


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


def test_office_scene_has_a_bounded_interactive_marketing_task_board():
    scene = source(SCENE)
    for contract in (
        'officeMarketingTaskBoard.name = "forkmesh-office-marketing-task-board"',
        '"forkmesh-office-marketing-task-board-frame"',
        '"forkmesh-office-marketing-task-board-face"',
        "officeMarketingTaskBoard.position.set(",
        "-OFFICE_WIDTH / 2 + 3.2,",
        'officeFloorY("marketing") + 3.55,',
        "-0.55,",
        'officeMarketingTaskBoard.rotation.y = Math.PI / 2',
        "officeMarketingTaskBoardFrame.userData.interactive =",
        "officeMarketingTaskBoardFace.userData.interactive =",
        'onOfficeTaskBoardSelect = () => {}',
        "function updateOfficeMarketingTasks(payload = {})",
        "normalizeOfficeMarketingTasks(payload)",
        "face.material.map?.dispose?.()",
        "face.material.needsUpdate = true",
        "updateOfficeMarketingTasks,",
    ):
        assert contract in scene
    assert "const OFFICE_MARKETING_TASK_LIMIT = 6" in scene
    assert "task.title,\n            42" in scene
    assert "task.assignee,\n            24" in scene
    assert "task.status,\n            16" in scene
    assert "task.elapsed,\n            18" in scene


def test_office_task_board_is_authorization_gated_and_selected_before_chairs():
    scene = source(SCENE)
    normalizer = scene[
        scene.index("function normalizeOfficeMarketingTasks("):
        scene.index("function officeMarketingTasksTexture(")
    ]
    assert "const authorized = source.authorized === true" in normalizer
    assert "const tasks = authorized" in normalizer
    assert '!authorized || requestedState === "locked"' in normalizer
    assert "authorized, state, tasks" in normalizer
    pointer = scene[
        scene.index("const hit = raycaster", scene.index("function finishPointer")):
        scene.index("const moderationAction", scene.index("function finishPointer"))
    ]
    assert pointer.index("onOfficeTaskBoardSelect") < pointer.index(
        "onOfficeChairSelect"
    )
    for forbidden in ("WebSocket", "sendMeeting", "sendPresence"):
        assert forbidden not in normalizer


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


def test_general_meeting_access_uses_only_the_short_lived_memory_ticket_header():
    meeting = source(MEETING)
    for contract in (
        'const OFFICE_ENTRY_HEADER = "X-ForkMesh-Office-Entry"',
        "let entryTicket = \"\"",
        "let entryTicketExpiresAt = 0",
        "entryTicketExpiresAt > Date.now()",
        "headers.set(OFFICE_ENTRY_HEADER, entryTicket)",
        "setEntryTicket(ticket, expiresAt)",
    ):
        assert contract in meeting
    assert "localStorage" not in meeting
    assert "sessionStorage" not in meeting


def test_native_office_entry_ticket_is_minted_lazily_for_meetings():
    office = source(OFFICE)
    meeting = source(MEETING)
    world = source(WORLD)
    ticket_wiring = (
        "meeting.setEntryTicket?.(officeEntryTicket, officeEntryExpiresAt)"
    )
    assert ticket_wiring in office
    assert "async function authorizeMeeting()" in office
    assert "const entered = completeOfficeEntry()" in office
    assert "setEntryTicketProvider" in world
    assert "let entryTicketProvider = null" in meeting
    assert "await entryTicketProvider()" in meeting
    assert meeting.index("await entryTicketProvider()") < meeting.index(
        "meetingBinding = await createMeetingBinding()"
    )
    assert 'const OFFICE_ENTRY_PATH = "/api/world/office/general/entry"' in office
    assert "if (!activeSession) return greetGuest()" not in office
    assert "world-office-keypad" not in world.lower()
    assert "four-digit office code" not in world.lower()


def test_office_movement_queue_is_latest_wins_bounded_and_backpressure_safe():
    script = f"""
      import {{ createOfficeMovementQueue }} from {json.dumps(MEETING.as_uri())};

      let clock = 0;
      let nextTimer = 1;
      let buffered = 0;
      const timers = new Map();
      const sent = [];
      const queue = createOfficeMovementQueue({{
        now: () => clock,
        isReady: () => true,
        bufferedAmount: () => buffered,
        send: (frame) => {{
          sent.push({{ ...frame, at: clock }});
          return true;
        }},
        setTimer: (callback, delay) => {{
          const id = nextTimer++;
          timers.set(id, {{ callback, due: clock + delay }});
          return id;
        }},
        clearTimer: (id) => timers.delete(id),
      }});

      function assert(condition, message) {{
        if (!condition) throw new Error(message);
      }}

      function advance(target) {{
        while (true) {{
          const due = [...timers.entries()]
            .filter(([, timer]) => timer.due <= target)
            .sort((left, right) => left[1].due - right[1].due)[0];
          if (!due) break;
          const [id, timer] = due;
          timers.delete(id);
          clock = timer.due;
          timer.callback();
        }}
        clock = target;
      }}

      queue.queue({{ x: 1, y: 0.38, z: 1, yaw: 0, moving: true }});
      clock = 100;
      queue.queue({{ x: 2, y: 0.38, z: 2, yaw: 0.1, moving: true }});
      clock = 200;
      queue.queue({{ x: 3, y: 0.38, z: 3, yaw: 0.2, moving: true }});
      assert(sent.length === 1, "moving frames must be capped");
      advance(999);
      assert(sent.length === 1, "queued movement flushed too early");
      advance(1000);
      assert(
        sent.length === 2 && sent[1].x === 3,
        "the one-second flush must send only the latest position",
      );

      clock = 1100;
      queue.queue({{ x: 4, y: 0.38, z: 4, yaw: 0.3, moving: true }});
      clock = 1150;
      queue.queue({{ x: 5, y: 0.38, z: 5, yaw: 0.4, moving: false }});
      assert(
        sent.length === 3 && sent[2].moving === false && sent[2].at === 1150,
        "the final stopped frame must bypass the movement cadence",
      );

      queue.reset();
      clock = 2000;
      buffered = 65537;
      queue.queue({{ x: 6, y: 0.38, z: 6, yaw: 0.5, moving: true }});
      clock = 2100;
      queue.queue({{ x: 7, y: 0.38, z: 7, yaw: 0.6, moving: true }});
      assert(sent.length === 3, "movement must pause above the socket high-water mark");
      buffered = 0;
      advance(2250);
      assert(
        sent.length === 4 && sent[3].x === 7,
        "backpressure retry must preserve only the latest position",
      );

      queue.reset();
      clock = 3000;
      buffered = 65537;
      queue.queue({{ x: 8, y: 0.38, z: 8, yaw: 0.7, moving: true }});
      clock = 3050;
      queue.queue({{ x: 9, y: 0.38, z: 9, yaw: 0.8, moving: false }});
      buffered = 0;
      advance(3300);
      assert(
        sent.length === 5 && sent[4].x === 9 && sent[4].moving === false,
        "a stopped frame must replace backed-up movement before retry",
      );
    """
    subprocess.run(
        ["node", "--input-type=module", "-e", script],
        check=True,
        capture_output=True,
        text=True,
    )


def test_office_movement_queue_does_not_throttle_presence_or_ping_frames():
    meeting = source(MEETING)
    assert 'sendMeeting({\n        type: "presence",' in meeting
    assert 'sendMeeting({ type: "ping" })' in meeting
    movement = meeting[
        meeting.index("function move(movement = {})"):
        meeting.index("function leaveRoom()")
    ]
    assert "movementQueue.queue({" in movement
    assert "sendMeeting({" not in movement


def test_idle_meeting_keepalive_halves_durable_object_wakeups_with_stale_margin():
    meeting = source(MEETING)
    protocol = source(WORLD_PROTOCOL)
    assert "const OFFICE_PING_MS = 40000" in meeting
    assert "OFFICE_CLIENT_STALE_MS = 90 * 1000" in protocol
    assert (
        "window.setInterval(() => sendMeeting({ type: \"ping\" }), "
        "OFFICE_PING_MS)"
    ) in meeting
