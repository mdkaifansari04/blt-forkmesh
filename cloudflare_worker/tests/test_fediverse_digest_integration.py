"""Worker integration contracts for privacy-safe daily Fediverse digests."""

import ast
import asyncio
import hashlib
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
SCHEMA_TEXT = (ROOT / "src" / "schema.py").read_text(encoding="utf-8")
URLS_TEXT = (ROOT / "src" / "urls.py").read_text(encoding="utf-8")
PUBLIC = ROOT / "public"

spec = importlib.util.spec_from_file_location(
    "fediverse_digest", ROOT / "src" / "fediverse_digest.py")
fedi_digest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fedi_digest)

ap_spec = importlib.util.spec_from_file_location(
    "activitypub", ROOT / "src" / "activitypub.py")
ap = importlib.util.module_from_spec(ap_spec)
ap_spec.loader.exec_module(ap)


def _load(*names, extra_globals=None):
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    nodes = [
        node for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    assert {node.name for node in nodes} == set(names)
    namespace = dict(extra_globals or {})
    exec(compile(ast.fix_missing_locations(
        ast.Module(body=nodes, type_ignores=[])), str(ENTRY), "exec"),
         namespace)
    return namespace


def _run(coro):
    return asyncio.new_event_loop().run_until_complete(coro)


def test_schema_migration_and_routes_are_deployed():
    migration = (
        ROOT / "migrations" / "0046_fediverse_digests.sql"
    ).read_text(encoding="utf-8")
    for table in ("ap_digest_queues", "ap_org_digest_settings"):
        assert "CREATE TABLE IF NOT EXISTS " + table in SCHEMA_TEXT
        assert "CREATE TABLE IF NOT EXISTS " + table in migration
    assert "dedupe_bi TEXT" in SCHEMA_TEXT
    assert "idx_ap_outbox_dedupe" in migration
    assert "REPO_AP_DIGEST_RE" in URLS_TEXT
    assert "ORG_FEDIVERSE_RE" in URLS_TEXT
    assert "return await ap_digest_handler(" in ENTRY_TEXT
    assert "return await org_fediverse_handler(" in ENTRY_TEXT


def test_automatic_hook_queues_and_only_explicit_manual_post_is_immediate():
    body = ENTRY_TEXT[
        ENTRY_TEXT.index("async def _ap_publish_repo_event"):
        ENTRY_TEXT.index("async def _ap_forget_remote")
    ]
    enqueue = body.index("await _ap_enqueue_repo_digest(")
    immediate_object = body.index("INSERT INTO ap_objects")
    assert "manual=False" in body
    assert "if not manual:" in body
    assert enqueue < immediate_object
    handler = ENTRY_TEXT[
        ENTRY_TEXT.index("async def ap_publish_handler"):
        ENTRY_TEXT.index("async def _authorize_repo_owner_web")
    ]
    assert 'manual = data.get("manual") is True' in handler
    assert 'manual=manual' in handler
    assert '"mode": "manual_immediate" if manual else "daily_digest"' in handler


def test_scope_queue_is_encrypted_deduplicated_and_bounded():
    now = 10_000
    rows = {}
    sealed = {}
    sequence = [0]

    async def blind_index(env, value):
        return "bi:" + value

    async def d1_first(env, sql, *args):
        return rows.get(args[0])

    async def encrypt_row(env, value):
        sequence[0] += 1
        token = "sealed-%d" % sequence[0]
        sealed[token] = json.loads(json.dumps(value))
        return token

    async def decrypt_row(env, token):
        return json.loads(json.dumps(sealed.get(token))) if token else None

    async def d1_run(env, sql, *args):
        assert "ap_digest_queues" in sql
        rows[args[0]] = {
            "scope_bi": args[0],
            "next_ts": args[1],
            "last_published_at": args[2],
            "data": args[3],
        }

    class Date:
        @staticmethod
        def now():
            return now

    ns = _load(
        "_ap_digest_scope_bi", "_ap_digest_queue_record",
        "_ap_enqueue_digest_scope",
        extra_globals={
            "blind_index": blind_index,
            "d1_first": d1_first,
            "d1_run": d1_run,
            "encrypt_row": encrypt_row,
            "decrypt_row": decrypt_row,
            "clean_string": lambda value, limit: str(value or "")[:limit],
            "valid_node_name": lambda value: bool(value),
            "MAX_NODE_NAME": 63,
            "MAX_REPO_SEGMENT": 160,
            "Date": Date,
            "fedi_digest": fedi_digest,
        },
    )

    for index in range(fedi_digest.MAX_PENDING_EVENTS + 15):
        event = fedi_digest.normalize_event({
            "type": "issue",
            "title": "Issue %d" % index,
            "url": "https://forkmesh.test/alice/repo/issues/%d" % index,
            "timestamp": index + 1,
            "scope": "alice/repo",
        })
        assert _run(ns["_ap_enqueue_digest_scope"](
            None, "alice", "repo", "alice", event))

    row = rows["bi:ap-digest:alice/repo"]
    payload = sealed[row["data"]]
    assert len(payload["events"]) == fedi_digest.MAX_PENDING_EVENTS
    assert payload["events"][0]["title"] == "Issue 15"
    assert row["next_ts"] == now + fedi_digest.DAY_MS
    # The D1 row contains only an opaque encrypted token, not public titles.
    assert row["data"].startswith("sealed-")
    assert "Issue" not in row["data"]


def test_publish_consumes_events_in_same_batch_as_object_and_outbox():
    now = fedi_digest.DAY_MS * 3
    event = fedi_digest.normalize_event({
        "type": "release",
        "title": "Release 2.0",
        "url": "https://forkmesh.test/alice/repo/releases/v2",
        "timestamp": now - 1000,
        "scope": "alice/repo",
    })
    queue = {
        "scope": "alice/repo",
        "scopeOwner": "alice",
        "sourceOwner": "alice",
        "repo": "repo",
        "events": [event],
        "lastPublishedAt": 0,
    }
    sealed = {"queue": queue}
    batches = []
    deletes = []

    async def decrypt_row(env, token):
        return json.loads(json.dumps(sealed[token]))

    async def encrypt_row(env, value):
        token = "enc-%d" % len(sealed)
        sealed[token] = json.loads(json.dumps(value))
        return token

    async def d1_all(env, sql, *args):
        return [
            {"inbox": "https://social.test/inbox", "shared_inbox": ""},
            {"inbox": "https://social.test/inbox", "shared_inbox": ""},
            {"inbox": "https://other.test/inbox", "shared_inbox": ""},
        ]

    async def d1_run(env, sql, *args):
        deletes.append((sql, args))

    async def batch(env, statements):
        batches.append(statements)

    async def blind_index(env, value):
        return hashlib.sha256(value.encode()).hexdigest()

    class Date:
        @staticmethod
        def now():
            return now

    ns = _load("_ap_publish_digest_row", extra_globals={
        "json": json,
        "hashlib": hashlib,
        "Date": Date,
        "fedi_digest": fedi_digest,
        "decrypt_row": decrypt_row,
        "encrypt_row": encrypt_row,
        "_ap_digest_scope_allowed": lambda env, rec: _true(),
        "_ap_origin": lambda env: "https://forkmesh.test",
        "_ap_local_actor": lambda env, kind, handle: _value({
            "actorBi": "actor-bi", "privkey": "private"}),
        "d1_all": d1_all,
        "d1_run": d1_run,
        "_ap_actor_url": lambda origin, kind, handle: (
            origin + "/ap/repos/" + handle.replace(".", "/")),
        "repo_web_href": lambda owner, repo: "/" + owner + "/" + repo,
        "blind_index": blind_index,
        "_contribution_run_batch": batch,
        "ap": ap,
        "AP_ACTOR_REPO": "repo",
    })
    assert _run(ns["_ap_publish_digest_row"](None, {
        "scope_bi": "scope-bi",
        "last_published_at": 0,
        "data": "queue",
    }))
    assert deletes == []
    assert len(batches) == 1
    statements = batches[0]
    sql = "\n".join(statement for statement, _ in statements)
    assert "INSERT OR IGNORE INTO ap_objects" in sql
    assert sql.count("INSERT OR IGNORE INTO ap_outbox") == 2
    assert "UPDATE ap_digest_queues" in sql
    updated = sealed[statements[-1][1][2]]
    assert updated["events"] == []
    assert updated["lastPublishedAt"] == now
    object_uuid = statements[0][1][0]
    assert len(object_uuid) == 32
    assert all(args[-1] for statement, args in statements[1:-1])


async def _true():
    return True


async def _value(value):
    return value


def test_private_or_revoked_scope_is_deleted_without_publication():
    deletes = []

    async def d1_run(env, sql, *args):
        deletes.append((sql, args))

    ns = _load("_ap_publish_digest_row", extra_globals={
        "decrypt_row": lambda env, value: _value({
            "scope": "alice/repo",
            "scopeOwner": "alice",
            "sourceOwner": "alice",
            "repo": "repo",
            "events": [],
        }),
        "_ap_digest_scope_allowed": lambda env, rec: _value(False),
        "d1_run": d1_run,
    })
    assert not _run(ns["_ap_publish_digest_row"](None, {
        "scope_bi": "secret-scope", "data": "encrypted"}))
    assert deletes == [(
        "DELETE FROM ap_digest_queues WHERE scope_bi=?",
        ("secret-scope",),
    )]


def test_owner_and_org_admin_previews_are_no_store_and_private_safe():
    repo_handler = ENTRY_TEXT[
        ENTRY_TEXT.index("async def ap_digest_handler"):
        ENTRY_TEXT.index("async def ap_posts_handler")
    ]
    assert "await _repo_is_private(env, owner, repo)" in repo_handler
    assert "await _authorize_repo_owner_web" in repo_handler
    assert 'cache_control="no-store"' in repo_handler

    org_handler = ENTRY_TEXT[
        ENTRY_TEXT.index("async def org_fediverse_handler"):
        ENTRY_TEXT.index("# --- Admins + manual email verification")
    ]
    assert 'not in ("owner", "admin")' in org_handler
    assert "await _repo_is_private(env, source_owner, repo)" in org_handler
    assert "ownerEnabled" in org_handler
    assert "Organization controls cannot override repository-owner settings." \
        in org_handler


def test_dashboard_exposes_repo_and_org_controls_with_exact_preview():
    assembled = (PUBLIC / "dashboard.js").read_text(encoding="utf-8")
    for marker in (
        "data-repo-digest-preview",
        "loadRepoDigestPreview",
        "/fediverse-digest",
        "data-org-fediverse-enabled",
        '"/fediverse"',
        "at most one clearly automated post per 24 hours",
        "No meaningful public updates are queued.",
    ):
        assert marker in assembled
    assert (Path(__file__).resolve().parents[2]
            / "docs" / "fediverse-digests.md").is_file()
