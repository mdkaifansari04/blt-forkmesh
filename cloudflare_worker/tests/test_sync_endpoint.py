#!/usr/bin/env python3
"""GET /api/sync — the consolidated event-driven node sync endpoint.

One signed round-trip returns, for every repo the owner account has in the
catalog: pending issue/pull/discussion/commit inbox items, queued agent
prompts (drained on read, same semantics as GET /agents), and the relay's
pinned repo state. This replaces the desktop node's fast per-topic polling;
see notify_repo_host / sync_handler in entry.py.
"""

import ast
import asyncio
import base64
import hashlib
import importlib.util
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
CATALOG = ENTRY.parent / "catalog.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8") + "\n" + CATALOG.read_text(encoding="utf-8")
SECURITY_SPEC = importlib.util.spec_from_file_location(
    "sync_security_controls", ROOT / "src" / "security_controls.py")
SECURITY_CONTROL = importlib.util.module_from_spec(SECURITY_SPEC)
SECURITY_SPEC.loader.exec_module(SECURITY_CONTROL)

FUNCS = {
    "sync_handler",
    "_authorize_owner",
    "_authorized_owner_signing_key",
    "_verify_owner_signature",
    "_owner_signing_pubkeys",
    "_owner_pubkey",
    "method_name",
    "clean_string",
}


def _load_functions(extra_globals):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    selected = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in FUNCS
    ]
    found = {node.name for node in selected}
    assert found == FUNCS, "missing functions: %s" % sorted(FUNCS - found)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals)
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


class _Request:
    method = "GET"

    def __init__(self, url):
        self.url = url


def _harness(owner_e2ee=False):
    """In-memory D1 stand-ins, in the style of test_repo_agents.py."""
    repositories = []   # {"key_bi","owner_bi","data"}
    inboxes = {
        "issue_inbox": [],       # {"repo_bi","data"}
        "pull_inbox": [],
        "discussion_inbox": [],
        "commit_inbox": [],
    }
    agent_prompts = []  # {"id","repo_bi","data"}

    class _DateStub:
        @staticmethod
        def now():
            return 1_000_000_000

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    async def ensure_schema(_env):
        return None

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def _account_row(_env, name):
        key = str(name or "").strip().lower()
        if key == "alice":
            return "bi:alice", {"pubkey": "PK-alice"}
        return "bi:" + key, None

    async def ed25519_verify(_pubkey, sig, _canonical):
        return sig == "good-sig"

    async def _account_devices_list(_env, _account_bi):
        # No extra desktop devices by default; the device-key drain path is
        # exercised by overriding this stub in its own test below.
        return []

    async def decrypt_row(_env, stored, key=None):
        return dict(stored)

    async def d1_all(_env, sql, *args):
        if "FROM repositories WHERE owner_bi=?" in sql:
            owner_bi = args[0]
            return [{"key_bi": r["key_bi"], "data": r["data"]}
                    for r in repositories if r["owner_bi"] == owner_bi]
        # sync_handler now selects each inbox table once across all of the
        # owner's repos with `repo_bi IN (?,...)`, returning repo_bi + data.
        want = set(args)
        for table, rows in inboxes.items():
            if "FROM " + table in sql:
                return [
                    {"repo_bi": r["repo_bi"], "data": r["data"]}
                    for r in rows
                    if r["repo_bi"] in want
                    and (
                        table != "issue_inbox"
                        or "claimed_by_bi=?" not in sql
                        or r.get("claimed_by_bi") in want
                    )
                ]
        if "FROM agent_prompts" in sql:
            # The drain path selects row ids too (aliased drain_id) so the
            # delete can target exactly the rows it read.
            return [{"repo_bi": r["repo_bi"], "data": r["data"],
                     "drain_id": r["id"]}
                    for r in agent_prompts if r["repo_bi"] in want]
        if "FROM about_inbox" in sql:
            return []
        raise AssertionError("unexpected d1_all: " + sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith(
                "UPDATE issue_inbox SET claimed_by_bi="):
            claimant, expires = args[:2]
            repo_values = set(args[2:-2])
            now, same_claimant = args[-2:]
            for row in inboxes["issue_inbox"]:
                if (
                    row["repo_bi"] in repo_values
                    and (
                        not row.get("claimed_by_bi")
                        or int(row.get("claim_expires_at") or 0) <= now
                        or row.get("claimed_by_bi") == same_claimant
                    )
                ):
                    row["claimed_by_bi"] = claimant
                    row["claim_expires_at"] = expires
            return
        if sql.startswith("DELETE FROM agent_prompts WHERE id IN"):
            drained = set(args)
            agent_prompts[:] = [r for r in agent_prompts
                                if r["id"] not in drained]
            return
        if "about_inbox" in sql:
            return
        raise AssertionError("unexpected d1_run: " + sql)

    async def d1_first(_env, sql, *args):
        raise AssertionError("unexpected d1_first: " + sql)

    def safe_segment(value, max_length=100):
        # Stand-in for catalog.py's sanitizer: lowercase pass-through is
        # enough for these tests' well-formed owner names.
        return str(value or "").strip().lower()[:max_length]

    globals_for_handler = {
        "Date": _DateStub,
        "safe_segment": safe_segment,
        "json_response": json_response,
        "ensure_schema": ensure_schema,
        "blind_index": blind_index,
        "_account_row": _account_row,
        "_account_devices_list": _account_devices_list,
        "ed25519_verify": ed25519_verify,
        "decrypt_row": decrypt_row,
        "d1_all": d1_all,
        "d1_run": d1_run,
        "d1_first": d1_first,
        "parse_qs": parse_qs,
        "unquote": unquote,
        "urlparse": urlparse,
        "LOGIN_MAX_SKEW_MS": 5 * 60 * 1000,
        "ISSUE_INBOX_CLAIM_TTL_MS": 5 * 60 * 1000,
        "MAX_REPO_SEGMENT": 100,
    }
    if owner_e2ee:
        globals_for_handler["security_control"] = SECURITY_CONTROL
    ns = _load_functions(globals_for_handler)
    return ns, repositories, inboxes, agent_prompts


def _sync_url(owner="alice", ts=1_000_000_000, sig="good-sig"):
    return ("https://forkmesh.test/api/sync?owner=%s&ts=%d&sig=%s"
            % (owner, ts, sig))


def _b64url(raw):
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def _owner_envelope():
    return {
        "kind": "forkmesh.owner-sealed",
        "v": 1,
        "alg": "x25519+mlkem768/aes256gcm",
        "nonce": _b64url(b"n" * 12),
        "tag": _b64url(b"t" * 16),
        "body": _b64url(b"opaque encrypted prompt"),
        "recipients": [{
            "kid": _b64url(hashlib.sha256(b"owner-key").digest()),
            "x25519": _b64url(b"x" * 32),
            "mlkem768": _b64url(b"m" * 1088),
            "nonce": _b64url(b"w" * 12),
            "tag": _b64url(b"g" * 16),
            "key": _b64url(b"k" * 32),
        }],
    }


def test_sync_returns_all_topics_and_drains_prompts():
    ns, repositories, inboxes, agent_prompts = _harness()
    repositories.append({
        "key_bi": "bi:alice/repo-one",
        "owner_bi": "bi:alice",
        "data": {"name": "repo-one", "owner": "alice",
                 "stateHash": "abc123", "updatedAt": "999",
                 "source": "local-node"},
    })
    inboxes["issue_inbox"].append(
        {"repo_bi": "bi:alice/repo-one", "data": {"number": 1}})
    inboxes["pull_inbox"].append(
        {"repo_bi": "bi:alice/repo-one", "data": {"number": 2}})
    agent_prompts.append(
        {"id": 1, "repo_bi": "bi:alice/repo-one",
         "data": {"agentId": "new", "text": "fix it"}})

    resp = asyncio.run(ns["sync_handler"](object(), _Request(_sync_url())))
    assert resp["status"] == 200
    repos = resp["data"]["repos"]
    assert len(repos) == 1
    entry = repos[0]
    assert entry["name"] == "repo-one"
    assert entry["stateHash"] == "abc123"
    assert entry["issues"] == [{"number": 1}]
    assert entry["pulls"] == [{"number": 2}]
    assert entry["discussions"] == []
    assert entry["commits"] == []
    assert entry["agentPrompts"] == [{"agentId": "new", "text": "fix it"}]
    # Prompts drain on read (same contract as GET /agents); inbox items do
    # NOT — the node still acks a merged inbox with its per-topic DELETE.
    assert agent_prompts == []
    assert len(inboxes["issue_inbox"]) == 1


def test_sync_returns_owner_sealed_prompt_and_waits_for_explicit_ack():
    ns, repositories, _inboxes, agent_prompts = _harness(owner_e2ee=True)
    repositories.append({
        "key_bi": "bi:alice/repo-one",
        "owner_bi": "bi:alice",
        "data": {"name": "repo-one", "owner": "alice"},
    })
    envelope = _owner_envelope()
    agent_prompts.append({
        "id": 17,
        "repo_bi": "bi:alice/repo-one",
        "data": SECURITY_CONTROL.encode_owner_envelope(
            envelope, {"agentId": 42, "queuedAt": 999}),
    })

    response = asyncio.run(
        ns["sync_handler"](object(), _Request(_sync_url())))
    entry = response["data"]["repos"][0]
    assert entry["agentPrompts"] == [{
        "queueId": 17,
        "agentId": 42,
        "queuedAt": 999,
        "encrypted": True,
        "envelope": envelope,
    }]
    assert entry["agentPromptAckPath"] == (
        "/api/repo/alice/repo-one/agents/ack")
    assert "opaque encrypted prompt" not in str(entry)
    # Sync is non-destructive for ciphertext. The owner desktop opens and
    # journals it before calling /agents/ack.
    assert len(agent_prompts) == 1


def test_sync_rejects_bad_signature_and_stale_ts():
    ns, repositories, _inboxes, _prompts = _harness()
    repositories.append({
        "key_bi": "bi:alice/repo-one",
        "owner_bi": "bi:alice",
        "data": {"name": "repo-one", "owner": "alice"},
    })
    resp = asyncio.run(ns["sync_handler"](
        object(), _Request(_sync_url(sig="bad-sig"))))
    assert resp["status"] == 401
    resp = asyncio.run(ns["sync_handler"](
        object(), _Request(_sync_url(ts=1))))  # far outside the skew window
    assert resp["status"] == 401


def test_sync_accepts_a_stored_enabled_device_key_not_just_the_primary():
    # A reinstalled/rotated node signs with a NEW key that login stored in
    # account_devices (enabled, owner_sign) but never promoted to the account's
    # primary pubkey. The drain gate must honor that stored device key or the
    # node silently 401s forever and nothing (issues/chats/etc.) reaches it.
    ns, repositories, inboxes, _prompts = _harness()
    repositories.append({
        "key_bi": "bi:alice/repo-one",
        "owner_bi": "bi:alice",
        "data": {"name": "repo-one", "owner": "alice"},
    })
    inboxes["issue_inbox"].append(
        {"repo_bi": "bi:alice/repo-one", "data": {"number": 7}})

    # Primary pubkey is the OLD key; only the new device key verifies the sig.
    async def _account_row(_env, name):
        if str(name).lower() == "alice":
            return "bi:alice", {"pubkey": "PK-old-primary"}
        return "bi:" + str(name).lower(), None

    async def ed25519_verify(pubkey, sig, _canonical):
        return sig == "good-sig" and pubkey == "PK-new-device"

    async def devices(_env, account_bi):
        if account_bi != "bi:alice":
            return []
        return [{"pubkey": "PK-new-device", "enabled": True,
                 "capabilities": ["browse", "owner_sign"]}]

    ns["_account_row"] = _account_row
    ns["ed25519_verify"] = ed25519_verify
    ns["_account_devices_list"] = devices

    resp = asyncio.run(ns["sync_handler"](object(), _Request(_sync_url())))
    assert resp["status"] == 200
    assert resp["data"]["repos"][0]["issues"] == [{"number": 7}]


def test_sync_rejects_a_disabled_or_non_signing_device_key():
    # A device that is revoked/disabled, or lacks the owner_sign capability, must
    # NOT be able to drain — broadening the gate to stored keys must not weaken it.
    ns, repositories, _inboxes, _prompts = _harness()
    repositories.append({
        "key_bi": "bi:alice/repo-one",
        "owner_bi": "bi:alice",
        "data": {"name": "repo-one", "owner": "alice"},
    })

    async def _account_row(_env, name):
        if str(name).lower() == "alice":
            return "bi:alice", {"pubkey": "PK-old-primary"}
        return "bi:" + str(name).lower(), None

    async def ed25519_verify(pubkey, sig, _canonical):
        return sig == "good-sig" and pubkey == "PK-new-device"

    async def devices(_env, account_bi):
        return [
            {"pubkey": "PK-new-device", "enabled": False,
             "capabilities": ["owner_sign"]},               # revoked/disabled
            {"pubkey": "PK-web", "enabled": True,
             "capabilities": ["browse", "comment"]},         # no owner_sign
        ]

    ns["_account_row"] = _account_row
    ns["ed25519_verify"] = ed25519_verify
    ns["_account_devices_list"] = devices

    resp = asyncio.run(ns["sync_handler"](object(), _Request(_sync_url())))
    assert resp["status"] == 401


def test_sync_only_lists_the_signers_repos():
    ns, repositories, _inboxes, _prompts = _harness()
    repositories.append({
        "key_bi": "bi:bob/other",
        "owner_bi": "bi:bob",
        "data": {"name": "other", "owner": "bob"},
    })
    resp = asyncio.run(ns["sync_handler"](object(), _Request(_sync_url())))
    assert resp["status"] == 200
    assert resp["data"]["repos"] == []
