#!/usr/bin/env python3
"""Agent-session sync endpoints (website "Agents" tab, adhoc #182).

The desktop app runs Claude Code coding agent sessions per repo/issue. These
tests exercise the three new routes end to end against an in-memory D1
double, following the same source-extraction harness pattern used by
test_users_nodes_claim_link.py: select just the functions under test out of
entry.py via ast, and stub the handful of I/O primitives they call
(D1/crypto/JS) so the real control-flow logic runs unmodified.

Covered:
 - POST /agents (desktop push, ts+sig signed) stores sessions; a follow-up
   POST /agents/list (owner account, adhoc #225: no password) returns them.
 - POST /agents with a bad/missing signature -> 401.
 - POST /agents/list from a non-owner account -> 403, no data leaked.
 - POST /agents/<id>/prompt (owner account) enqueues a prompt; a subsequent
   GET /agents (desktop drain, ts+sig) returns and clears it.
 - A non-owner account still fails list/prompt with 403 not_authorized.
"""

import ast
import asyncio
from pathlib import Path
from urllib.parse import parse_qs, urlparse


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
# clean_string was extracted from entry.py into catalog.py; parse both sources
# so the AST loader below still finds it.
CATALOG = ENTRY.parent / "catalog.py"
SCHEMA = ENTRY.parent / "schema.py"
ENTRY_TEXT = (
    ENTRY.read_text(encoding="utf-8") + "\n" + CATALOG.read_text(encoding="utf-8")
    + "\n" + SCHEMA.read_text(encoding="utf-8"))

FUNCS = {
    "agents_handler", "agents_list_handler", "agents_prompt_handler",
    "agents_transcript_handler",
    "_clean_agent_session", "_authorize_owner",
    "_authorize_owner_account", "_owner_pubkey", "_login_locked_until",
    "_login_record_fail", "_login_clear", "method_name", "clean_string",
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


CORRECT_PASSWORD = "correct horse battery staple"


class _Request:
    def __init__(self, method="GET", url="https://forkmesh.test/x", body=None):
        self.method = method
        self.url = url
        self._body = body

    async def json(self):
        if self._body is None:
            raise ValueError("no body")
        return self._body


def _harness(accounts):
    """accounts: name (lowercase) -> {"pass_hash": bool-ish, "is_admin": bool}.

    D1 tables are plain in-memory containers; encrypt/decrypt are identity
    (dict copies) and blind_index is 'bi:' + value, matching the style already
    used by test_users_nodes_claim_link.py.
    """
    repo_agents = {}   # (repo_bi, agent_id) -> {"data":..., "updated_at":...}
    agent_prompts = []  # list of {"id","repo_bi","agent_id","data","queued_at"}
    login_attempts = {}  # id_bi -> {"fails","first_fail_ts","locked_until"}
    next_prompt_id = [1]
    now = [1_000_000_000]

    class _DateStub:
        @staticmethod
        def now():
            return now[0]

    def json_response(data, status=200, **_kwargs):
        return {"status": status, "data": data}

    async def ensure_schema(_env):
        return None

    async def blind_index(_env, value):
        return "bi:" + str(value)

    async def _account_row(_env, name):
        key = str(name or "").strip().lower()
        rec = accounts.get(key)
        return "bi:" + key, (dict(rec) if rec is not None else None)

    async def verify_password(password, _salt, _hash):
        return password == CORRECT_PASSWORD

    async def _is_admin(_env, name):
        return bool(accounts.get(str(name or "").strip().lower(), {}).get("is_admin"))

    async def ed25519_verify(_pubkey, sig, _canonical):
        # A stand-in signature scheme: only the literal "good-sig" verifies.
        return sig == "good-sig"

    async def encrypt_row(_env, obj):
        return dict(obj)

    async def decrypt_row(_env, stored, key=None):
        return dict(stored)

    async def d1_first(_env, sql, *args):
        if "FROM login_attempts" in sql:
            row = login_attempts.get(args[0])
            return dict(row) if row else None
        if "FROM repo_agents WHERE repo_bi=? AND agent_id=?" in sql:
            repo_bi, agent_id = args
            row = repo_agents.get((repo_bi, agent_id))
            return {"data": row["data"]} if row else None
        if "SELECT COUNT(*) AS c FROM agent_prompts" in sql:
            repo_bi = args[0]
            c = sum(1 for r in agent_prompts if r["repo_bi"] == repo_bi)
            return {"c": c}
        raise AssertionError("unexpected d1_first: " + sql)

    async def d1_run(_env, sql, *args):
        if sql.startswith("INSERT INTO login_attempts"):
            login_attempts[args[0]] = {
                "fails": args[1], "first_fail_ts": args[2], "locked_until": args[3],
            }
            return
        if sql.startswith("DELETE FROM login_attempts"):
            login_attempts.pop(args[0], None)
            return
        if sql.startswith("DELETE FROM repo_agents"):
            repo_bi = args[0]
            for key in [k for k in repo_agents if k[0] == repo_bi]:
                del repo_agents[key]
            return
        if sql.startswith("INSERT INTO repo_agents"):
            repo_bi, agent_id, data, updated_at = args
            # Mirrors the real repo_agents PRIMARY KEY (repo_bi, agent_id): a
            # duplicate insert must raise, the same as D1's UNIQUE constraint.
            if (repo_bi, agent_id) in repo_agents:
                raise AssertionError(
                    "UNIQUE constraint failed: repo_agents.repo_bi, repo_agents.agent_id")
            repo_agents[(repo_bi, agent_id)] = {"data": data, "updated_at": updated_at}
            return
        if sql.startswith("DELETE FROM agent_prompts"):
            repo_bi = args[0]
            agent_prompts[:] = [r for r in agent_prompts if r["repo_bi"] != repo_bi]
            return
        if sql.startswith("INSERT INTO agent_prompts"):
            repo_bi, agent_id, data, queued_at = args
            agent_prompts.append({
                "id": next_prompt_id[0], "repo_bi": repo_bi, "agent_id": agent_id,
                "data": data, "queued_at": queued_at,
            })
            next_prompt_id[0] += 1
            return
        raise AssertionError("unexpected d1_run: " + sql)

    async def d1_all(_env, sql, *args):
        if "FROM repo_agents" in sql:
            repo_bi = args[0]
            rows = [v for k, v in repo_agents.items() if k[0] == repo_bi]
            rows.sort(key=lambda r: r["updated_at"], reverse=True)
            return [{"data": r["data"]} for r in rows]
        if "FROM agent_prompts" in sql:
            repo_bi = args[0]
            rows = [r for r in agent_prompts if r["repo_bi"] == repo_bi]
            rows.sort(key=lambda r: r["id"])
            return [{"id": r["id"], "data": r["data"]} for r in rows]
        raise AssertionError("unexpected d1_all: " + sql)

    ns = _load_functions({
        "Date": _DateStub,
        "json_response": json_response,
        "ensure_schema": ensure_schema,
        "blind_index": blind_index,
        "_account_row": _account_row,
        "verify_password": verify_password,
        "_is_admin": _is_admin,
        "ed25519_verify": ed25519_verify,
        "encrypt_row": encrypt_row,
        "decrypt_row": decrypt_row,
        "d1_first": d1_first,
        "d1_run": d1_run,
        "d1_all": d1_all,
        "parse_qs": parse_qs,
        "urlparse": urlparse,
        "LOGIN_MAX_SKEW_MS": 5 * 60 * 1000,
        "LOGIN_MAX_FAILS": 10,
        "LOGIN_FAIL_WINDOW_MS": 15 * 60 * 1000,
        "LOGIN_LOCKOUT_MS": 15 * 60 * 1000,
        "MAX_AGENT_SESSIONS": 300,
        "MAX_AGENT_STRING": 300,
        "MAX_AGENT_TITLE": 240,
        "MAX_AGENT_PROMPT_TEXT": 8000,
        "MAX_PENDING_AGENT_PROMPTS": 50,
        "MAX_AGENT_TRANSCRIPT": 16000,
    })
    ns["_now"] = now
    return ns


def _owner_account(is_admin=False):
    return {"pubkey": "PK-alice", "status": "active", "pass_hash": "h",
            "pass_salt": "s", "is_admin": is_admin}


def _session(agent_id=42, **overrides):
    base = {
        "id": agent_id, "issueNumber": 7, "issueTitle": "Fix the thing",
        "status": "running", "provider": "anthropic", "model": "claude-sonnet-4-5",
        "branchName": "agent/issue-7", "createdAtMs": 1000, "startedAtMs": 1010,
        "finishedAtMs": 0, "numTurns": 5, "durationMs": 12345, "costUsd": 0.42,
        "lastError": "",
    }
    base.update(overrides)
    return base


def _push_url(ts="1000000000", sig="good-sig"):
    return "https://forkmesh.test/api/repo/alice/proj/agents?ts=%s&sig=%s" % (ts, sig)


def test_post_agents_valid_signature_stores_sessions_visible_via_list():
    accounts = {"alice": _owner_account()}
    ns = _harness(accounts)
    env = object()

    push = asyncio.run(ns["agents_handler"](
        env, _Request("POST", _push_url(), {"sessions": [_session()]}),
        "alice", "proj",
    ))
    assert push == {"status": 200, "data": {"ok": True}}

    listed = asyncio.run(ns["agents_list_handler"](
        env, _Request("POST", body={
            "ownerAccount": "alice",
        }),
        "alice", "proj",
    ))
    assert listed["status"] == 200
    assert listed["data"]["ok"] is True
    agents = listed["data"]["agents"]
    assert len(agents) == 1
    assert agents[0]["id"] == 42
    assert agents[0]["status"] == "running"
    assert agents[0]["issueTitle"] == "Fix the thing"
    assert agents[0]["costUsd"] == 0.42


def test_post_agents_duplicate_ids_in_one_push_deduped_not_500():
    # Regression: a push containing two sessions with the same id used to hit
    # the repo_agents PRIMARY KEY (repo_bi, agent_id) on the second INSERT
    # (DELETE only runs once before the loop), producing a 500 D1_ERROR.
    accounts = {"alice": _owner_account()}
    ns = _harness(accounts)
    env = object()

    push = asyncio.run(ns["agents_handler"](
        env, _Request("POST", _push_url(), {
            "sessions": [_session(status="running"), _session(status="completed")],
        }),
        "alice", "proj",
    ))
    assert push == {"status": 200, "data": {"ok": True}}

    listed = asyncio.run(ns["agents_list_handler"](
        env, _Request("POST", body={
            "ownerAccount": "alice",
        }),
        "alice", "proj",
    ))
    agents = listed["data"]["agents"]
    assert len(agents) == 1
    assert agents[0]["status"] == "completed"


def test_post_agents_bad_or_missing_signature_rejected():
    accounts = {"alice": _owner_account()}
    ns = _harness(accounts)
    env = object()

    missing_sig = asyncio.run(ns["agents_handler"](
        env,
        _Request("POST", "https://forkmesh.test/api/repo/alice/proj/agents?ts=1000000000",
                  {"sessions": [_session()]}),
        "alice", "proj",
    ))
    assert missing_sig == {"status": 401, "data": {"error": "unauthorized"}}

    bad_sig = asyncio.run(ns["agents_handler"](
        env,
        _Request("POST", _push_url(sig="bad-sig"), {"sessions": [_session()]}),
        "alice", "proj",
    ))
    assert bad_sig == {"status": 401, "data": {"error": "unauthorized"}}


def test_agents_list_non_owner_403_forbidden():
    accounts = {
        "alice": _owner_account(),
        "mallory": {"pubkey": "PK-mallory", "status": "active",
                    "pass_hash": "h", "pass_salt": "s", "is_admin": False},
    }
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["agents_handler"](
        env, _Request("POST", _push_url(), {"sessions": [_session()]}),
        "alice", "proj",
    ))

    resp = asyncio.run(ns["agents_list_handler"](
        env, _Request("POST", body={
            "ownerAccount": "mallory",
        }),
        "alice", "proj",
    ))
    assert resp["status"] == 403
    assert resp["data"] == {"error": "not_authorized"}
    assert "agents" not in resp["data"]
    assert "Fix the thing" not in str(resp["data"])


def test_prompt_enqueued_then_drained_by_desktop_get():
    accounts = {"alice": _owner_account()}
    ns = _harness(accounts)
    env = object()

    sent = asyncio.run(ns["agents_prompt_handler"](
        env, _Request("POST", body={
            "ownerAccount": "alice",
            "text": "please continue",
        }),
        "alice", "proj", "42",
    ))
    assert sent == {"status": 200, "data": {"ok": True}}

    drained = asyncio.run(ns["agents_handler"](
        env, _Request("GET", _push_url()), "alice", "proj",
    ))
    assert drained["status"] == 200
    prompts = drained["data"]["prompts"]
    assert len(prompts) == 1
    assert prompts[0]["agentId"] == "42"
    assert prompts[0]["text"] == "please continue"

    drained_again = asyncio.run(ns["agents_handler"](
        env, _Request("GET", _push_url()), "alice", "proj",
    ))
    assert drained_again["data"]["prompts"] == []


def test_non_owner_forbidden_even_without_password():
    accounts = {
        "alice": _owner_account(),
        "mallory": {"pubkey": "PK-mallory", "status": "active",
                    "pass_hash": "h", "pass_salt": "s", "is_admin": False},
    }
    ns = _harness(accounts)
    env = object()

    list_resp = asyncio.run(ns["agents_list_handler"](
        env, _Request("POST", body={
            "ownerAccount": "mallory",
        }),
        "alice", "proj",
    ))
    assert list_resp == {"status": 403, "data": {"error": "not_authorized"}}

    prompt_resp = asyncio.run(ns["agents_prompt_handler"](
        env, _Request("POST", body={
            "ownerAccount": "mallory",
            "text": "hijack",
        }),
        "alice", "proj", "42",
    ))
    assert prompt_resp == {"status": 403, "data": {"error": "not_authorized"}}


def test_admin_account_may_list_and_prompt_a_non_owned_repo():
    accounts = {
        "alice": _owner_account(),
        "root-admin": {"pubkey": "PK-admin", "status": "active",
                       "pass_hash": "h", "pass_salt": "s", "is_admin": True},
    }
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["agents_handler"](
        env, _Request("POST", _push_url(), {"sessions": [_session()]}),
        "alice", "proj",
    ))

    listed = asyncio.run(ns["agents_list_handler"](
        env, _Request("POST", body={
            "ownerAccount": "root-admin",
        }),
        "alice", "proj",
    ))
    assert listed["status"] == 200
    assert len(listed["data"]["agents"]) == 1


def test_prompt_validates_text_and_queue_cap():
    accounts = {"alice": _owner_account()}
    ns = _harness(accounts)
    env = object()

    empty = asyncio.run(ns["agents_prompt_handler"](
        env, _Request("POST", body={
            "ownerAccount": "alice", "text": "   ",
        }),
        "alice", "proj", "42",
    ))
    assert empty == {"status": 400, "data": {"error": "text_required"}}

    too_long = asyncio.run(ns["agents_prompt_handler"](
        env, _Request("POST", body={
            "ownerAccount": "alice",
            "text": "x" * 8001,
        }),
        "alice", "proj", "42",
    ))
    assert too_long == {"status": 400, "data": {"error": "text_too_long"}}

    for i in range(50):
        ok = asyncio.run(ns["agents_prompt_handler"](
            env, _Request("POST", body={
                "ownerAccount": "alice",
                "text": "msg %d" % i,
            }),
            "alice", "proj", "42",
        ))
        assert ok["status"] == 200
    full = asyncio.run(ns["agents_prompt_handler"](
        env, _Request("POST", body={
            "ownerAccount": "alice",
            "text": "one too many",
        }),
        "alice", "proj", "42",
    ))
    assert full == {"status": 429, "data": {"error": "prompt_queue_full"}}


def test_transcript_pushed_stripped_from_list_but_served_by_detail_endpoint():
    # adhoc #259: the desktop pushes a bounded run-log tail per session for the
    # website's agent detail page. It must NOT bloat the list payload, but the
    # per-agent transcript endpoint returns it in full for the owner.
    accounts = {"alice": _owner_account()}
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["agents_handler"](
        env, _Request("POST", _push_url(), {
            "sessions": [_session(transcript="line one\nline two\n")],
        }),
        "alice", "proj",
    ))

    listed = asyncio.run(ns["agents_list_handler"](
        env, _Request("POST", body={"ownerAccount": "alice"}),
        "alice", "proj",
    ))
    assert listed["status"] == 200
    assert "transcript" not in listed["data"]["agents"][0]

    transcript = asyncio.run(ns["agents_transcript_handler"](
        env, _Request("POST", body={"ownerAccount": "alice"}),
        "alice", "proj", "42",
    ))
    assert transcript["status"] == 200
    assert transcript["data"]["transcript"] == "line one\nline two"
    assert transcript["data"]["status"] == "running"


def test_transcript_non_owner_403_and_missing_agent_404():
    accounts = {
        "alice": _owner_account(),
        "mallory": {"pubkey": "PK-mallory", "status": "active",
                    "pass_hash": "h", "pass_salt": "s", "is_admin": False},
    }
    ns = _harness(accounts)
    env = object()

    asyncio.run(ns["agents_handler"](
        env, _Request("POST", _push_url(), {"sessions": [_session(transcript="secret")]}),
        "alice", "proj",
    ))

    forbidden = asyncio.run(ns["agents_transcript_handler"](
        env, _Request("POST", body={"ownerAccount": "mallory"}),
        "alice", "proj", "42",
    ))
    assert forbidden == {"status": 403, "data": {"error": "not_authorized"}}
    assert "secret" not in str(forbidden["data"])

    missing = asyncio.run(ns["agents_transcript_handler"](
        env, _Request("POST", body={"ownerAccount": "alice"}),
        "alice", "proj", "999",
    ))
    assert missing == {"status": 404, "data": {"error": "not_found"}}


def test_worker_wires_up_all_three_agent_routes():
    urls_text = (ENTRY.parent / "urls.py").read_text(encoding="utf-8")
    assert "REPO_AGENTS_RE = re.compile" in urls_text
    assert "REPO_AGENTS_LIST_RE = re.compile" in urls_text
    assert "REPO_AGENTS_PROMPT_RE = re.compile" in urls_text
    assert "async def agents_handler" in ENTRY_TEXT
    assert "async def agents_list_handler" in ENTRY_TEXT
    assert "async def agents_prompt_handler" in ENTRY_TEXT
    assert "REPO_AGENTS_LIST_RE.match(url.path)" in ENTRY_TEXT
    assert "REPO_AGENTS_PROMPT_RE.match(url.path)" in ENTRY_TEXT
    assert "REPO_AGENTS_RE.match(url.path)" in ENTRY_TEXT
    assert "CREATE TABLE IF NOT EXISTS repo_agents" in ENTRY_TEXT
    assert "CREATE TABLE IF NOT EXISTS agent_prompts" in ENTRY_TEXT


def test_issues_handler_wants_agent_uses_authorization_helper():
    # adhoc #225 changed wantsAgent from password verification to just checking
    # ownership via _authorize_owner_account (same as agents_list/prompt).
    start = ENTRY_TEXT.index('if meta_in.get("wantsAgent"):')
    wants_agent_body = ENTRY_TEXT[start:ENTRY_TEXT.index("meta = {", start)]
    assert "_authorize_owner_account(env, owner, data)" in wants_agent_body
