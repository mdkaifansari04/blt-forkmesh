"""Username and encrypted-chat moderation contracts."""
import ast
import asyncio
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
CHAT_MODERATION = ROOT / "public" / "chat-moderation.js"
CHAT_MODERATION_TEXT = CHAT_MODERATION.read_text(encoding="utf-8")
SERVER_NODE = ROOT.parent / "desktop" / "src" / "ServerNode.cpp"
SERVER_NODE_TEXT = SERVER_NODE.read_text(encoding="utf-8")


def _entry_namespace(*names):
    wanted = set(names) | {
        "BLOCKED_TERM_HASHES",
        "BLOCKED_TERM_LENGTHS",
    }
    tree = ast.parse(ENTRY_TEXT)
    selected = []
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if node.name in wanted:
                selected.append(node)
        elif isinstance(node, ast.Assign):
            assigned = {
                target.id for target in node.targets
                if isinstance(target, ast.Name)
            }
            if assigned & wanted:
                selected.append(node)
    namespace = {"re": re, "hashlib": hashlib, "json": json}
    exec(compile(ast.Module(selected, type_ignores=[]), str(ENTRY), "exec"), namespace)
    return namespace


def _decoded(hex_value):
    return bytes.fromhex(hex_value).decode("ascii")


def test_username_digest_filter_catches_exact_and_obfuscated_forms():
    namespace = _entry_namespace("_moderation_forms", "username_has_blocked_term")
    blocked = namespace["username_has_blocked_term"]
    first = _decoded("6675636b")
    second = _decoded("73686974")

    assert blocked(first)
    assert blocked("-".join(first))
    assert blocked(first[0] + first[1] * 3 + first[2:])
    assert blocked(second[:2] + "1" + second[3:])
    assert not blocked(_decoded("7363756e74686f727065"))
    assert not blocked("helpful-builder")


def test_username_moderation_hard_gate_is_deterministic_only():
    """Signup must not hard-block on a flaky Workers AI classifier."""
    namespace = _entry_namespace(
        "_moderation_forms",
        "username_has_blocked_term",
        "username_moderation_error",
    )
    first = _decoded("6675636b")

    assert asyncio.run(
        namespace["username_moderation_error"](object(), first)
    ) == "inappropriate_node_name"
    assert asyncio.run(
        namespace["username_moderation_error"](object(), "helpful-builder")
    ) == ""
    assert "_username_ai_blocked" not in ENTRY_TEXT
    assert "USERNAME_MODERATION_AI_MODEL" not in ENTRY_TEXT


def test_every_account_name_write_path_runs_moderation():
    assert ENTRY_TEXT.count("await username_moderation_error(env, name)") >= 3
    rename = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _rename_account_namespace"):
        ENTRY_TEXT.index("async def _account_signup")
    ]
    assert "await username_moderation_error(env, new_name)" in rename
    assert 'return name_bi, rec, moderation_error' in rename


def test_username_rejection_has_actionable_first_party_ui_copy():
    surfaces = [
        ROOT / "public" / "signup.js",
        ROOT / "public" / "dashboard" / "js" / "04-account.js",
        ROOT.parent / "world" / "public" / "world" / "world.js",
    ]
    for surface in surfaces:
        assert "inappropriate_node_name" in surface.read_text(encoding="utf-8")


def test_moderation_module_is_never_held_as_an_immutable_stale_dependency():
    headers = (ROOT / "public" / "_headers").read_text(
        encoding="utf-8")
    rule = headers[headers.index("/chat-moderation.js"):]
    rule = rule[:rule.index("\n\n")]
    assert "Cache-Control: no-store" in rule


def test_hash_vocabulary_matches_all_first_party_surfaces():
    digest_pattern = re.compile(r'"([0-9a-f]{64})"')
    worker_hashes = set(digest_pattern.findall(ENTRY_TEXT[
        ENTRY_TEXT.index("BLOCKED_TERM_HASHES"):
        ENTRY_TEXT.index("def valid_node_name")
    ]))
    web_hashes = set(digest_pattern.findall(CHAT_MODERATION_TEXT))
    desktop_hashes = set(digest_pattern.findall(SERVER_NODE_TEXT[
        SERVER_NODE_TEXT.index("blockedChatTermHashes"):
        SERVER_NODE_TEXT.index("bool isBlockedChatRun")
    ]))
    assert worker_hashes
    assert worker_hashes == web_hashes == desktop_hashes


@pytest.mark.skipif(shutil.which("node") is None, reason="Node.js is unavailable")
def test_browser_chat_filter_masks_plain_and_obfuscated_terms():
    script = r'''
import { readFileSync } from "node:fs";
const source = readFileSync(process.argv[2], "utf8");
const url = "data:text/javascript;base64," + Buffer.from(source).toString("base64");
const moderation = await import(url);
const first = Buffer.from("6675636b", "hex").toString("utf8");
const second = Buffer.from("73686974", "hex").toString("utf8");
const input = `keep ${first} and ${second[0]}-${second[1]}-1-${second[3]} done`;
const result = await moderation.moderateChatText(input);
const plain = await moderation.moderateChatPlain({type: "chat", text: input});
process.stdout.write(JSON.stringify({result, plain}));
'''
    completed = subprocess.run(
        ["node", "--input-type=module", "-", str(CHAT_MODERATION)],
        input=script,
        text=True,
        capture_output=True,
        check=True,
    )
    result = json.loads(completed.stdout)
    assert result["result"] == {"text": "keep *** and *** done", "changed": True}
    assert result["plain"]["text"] == "keep *** and *** done"


def test_desktop_filters_before_encryption_and_after_decryption():
    send_encrypted = SERVER_NODE_TEXT[
        SERVER_NODE_TEXT.index("void ServerNode::sendEncrypted"):
        SERVER_NODE_TEXT.index("void ServerNode::sampleSystemStats")
    ]
    assert "moderatedChatObject(plain)" in send_encrypted
    assert "m_crypto.encryptObject(moderated)" in send_encrypted
    assert SERVER_NODE_TEXT.count("filteredChatText(") >= 8
