#!/usr/bin/env python3
"""Contracts for the spatial ForkMesh Office chat controller."""

import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OFFICE_PATH = ROOT / "public" / "world" / "world-office.js"
SCENE_PATH = ROOT / "public" / "world" / "world-scene.js"


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
