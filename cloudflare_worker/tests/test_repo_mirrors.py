#!/usr/bin/env python3
"""Repo mirror-health payload contract tests."""

import ast
import asyncio
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
MIRRORS = ENTRY.parent / "mirrors.py"
# Mirror grouping / clone-selection helpers were extracted from entry.py into
# mirrors.py; parse both sources so the AST loaders below still find them.
_WORKER_SRC = ENTRY.read_text(encoding="utf-8") + "\n" + MIRRORS.read_text(encoding="utf-8")


def _load(*names, extra_globals=None):
    tree = ast.parse(_WORKER_SRC, filename=str(ENTRY))
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name in names
    ]
    found = {node.name for node in selected}
    missing = set(names) - found
    assert not missing, "missing functions: %s" % sorted(missing)
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = dict(extra_globals or {})
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return [namespace[name] for name in names]


(
    _mirror_ms,
    build_repo_mirrors_payload,
    clone_state_pins,
    repo_mirror_group_key,
    repo_mirror_same_group,
) = _load(
    "_mirror_ms",
    "build_repo_mirrors_payload",
    "clone_state_pins",
    "repo_mirror_group_key",
    "repo_mirror_same_group",
)


def _row(key, owner, name, *, root="", visibility="public", hosted="", synced="",
         size=0, commit="", branch="", issue_count=None, platform="", version="",
         node_id="", clones_served=None, website_served=None, artifact_count=None,
         state_hash="", source="local-node"):
    data = {
        "owner": owner,
        "name": name,
        "visibility": visibility,
        "hostedSince": hosted,
        "lastSync": synced,
        "rootCommit": root,
        "sizeBytes": size,
        "source": source,
    }
    if state_hash:
        data["stateHash"] = state_hash
    # Node facts the publishing node mirrors into its catalog record (adhoc #56);
    # only set when provided so legacy records without them are also exercised.
    for field, value in (("commit", commit), ("branch", branch),
                         ("platform", platform), ("version", version),
                         ("nodeId", node_id)):
        if value:
            data[field] = value
    if issue_count is not None:
        data["issueCount"] = issue_count
    if clones_served is not None:
        data["clonesServed"] = clones_served
    if website_served is not None:
        data["websiteServed"] = website_served
    if artifact_count is not None:
        data["artifactCount"] = artifact_count
    return {
        "key_bi": key,
        "is_private": 1 if visibility == "private" else 0,
        "data": data,
    }


def test_group_key_prefers_root_commit_and_falls_back_to_name():
    assert repo_mirror_group_key({"rootCommit": "ABC", "name": "forkmesh"}) == "root:abc"
    assert repo_mirror_group_key({"rootCommit": "", "name": "ForkMesh"}) == "name:forkmesh"


def test_same_group_matches_root_then_name():
    rooted = {"rootCommit": "ABC", "name": "forkmesh"}
    # Identical root -> same group; differing non-empty root -> separate (fork).
    assert repo_mirror_same_group(rooted, {"rootCommit": "abc", "name": "forkmesh"})
    assert not repo_mirror_same_group(rooted, {"rootCommit": "def", "name": "forkmesh"})
    # Missing root on either side falls back to a case-insensitive name match.
    assert repo_mirror_same_group(rooted, {"rootCommit": "", "name": "ForkMesh"})
    assert repo_mirror_same_group({"name": "forkmesh"}, {"rootCommit": "abc", "name": "forkmesh"})
    assert not repo_mirror_same_group(rooted, {"rootCommit": "", "name": "other"})


def test_payload_groups_public_root_commit_mirrors_and_sorts_online_first():
    now = 1_000_000
    rows = [
        _row("a", "mainnode", "forkmesh", root="abc", hosted="100000", synced="990000", size=10),
        _row("b", "kaif-node", "forkmesh", root="abc", hosted="", synced="940000", size=20),
        _row("c", "other", "forkmesh", root="def", hosted="90000", synced="999000", size=30),
    ]
    payload = build_repo_mirrors_payload(
        "mainnode",
        "forkmesh",
        rows,
        {"a": now - 1_000, "b": now - 30_000},
        {"b": 80_000},
        now,
        600_000,
        5_000,
    )

    assert payload["ok"] is True
    assert payload["groupKey"] == "root:abc"
    assert payload["summary"] == {
        "mirrors": 2,
        "online": 2,
        "cloneAvailable": 2,
        "dataHostedBytes": 30,
        "longestHostedSince": 80_000,
        "freshestSync": 990_000,
    }
    assert [m["node"] for m in payload["mirrors"]] == ["mainnode", "kaif-node"]
    assert payload["mirrors"][0]["status"] == "online"
    assert payload["mirrors"][0]["lastSeen"] == now - 1_000
    assert payload["mirrors"][0]["syncAgeMs"] == 10_000
    assert payload["mirrors"][0]["behind"] is False
    assert payload["mirrors"][1]["hostedSince"] == 80_000
    assert payload["mirrors"][1]["behind"] is True
    assert payload["mirrors"][1]["cloneAvailable"] is True


def test_payload_carries_node_facts_for_offline_mirrors():
    # A node mirrors its latest commit / issue count / platform / version / id into
    # its catalog record, so the Mirror nodes view can show those columns even while
    # the node is offline (adhoc #56). A legacy record without them degrades to the
    # unknown sentinels rather than erroring.
    now = 1_000_000
    rows = [
        _row("a", "mainnode", "forkmesh", root="abc", synced="990000", size=10,
             commit="686d7ebd1ef0", branch="main", issue_count=302,
             platform="linux", version="0.5.22", node_id="7ZMh_2s_IOTPxYz",
             clones_served=42, website_served=118, artifact_count=3),
        _row("b", "legacy", "forkmesh", root="abc", synced="980000", size=20),
    ]
    payload = build_repo_mirrors_payload(
        "mainnode", "forkmesh", rows, {}, {}, now, 600_000, 5_000
    )
    rich = payload["mirrors"][0]
    assert rich["commit"] == "686d7ebd1ef0"
    assert rich["branch"] == "main"
    assert rich["issueCount"] == 302
    assert rich["platform"] == "linux"
    assert rich["version"] == "0.5.22"
    assert rich["id"] == "7ZMh_2s_IOTPxYz"
    # Clone / website-serve tallies the node has provided (issue: mirror-node counts).
    assert rich["clonesServed"] == 42
    assert rich["websiteServed"] == 118
    # Release artifacts the node is hosting for download (adhoc #77).
    assert rich["artifactCount"] == 3
    # Legacy record (no node facts): empty strings and the -1 "unknown" sentinels.
    legacy = payload["mirrors"][1]
    assert legacy["commit"] == ""
    assert legacy["platform"] == ""
    assert legacy["issueCount"] == -1
    assert legacy["id"] == ""
    assert legacy["clonesServed"] == -1
    assert legacy["websiteServed"] == -1
    assert legacy["artifactCount"] == -1


def test_payload_marks_mirrors_the_integrity_gate_rejects():
    # A mirror must advertise a refs fingerprint some working-copy holder in its
    # group attested (current pin or recent history) or the relay rejects every
    # clone it serves ("repository failed integrity check"); the payload carries
    # that verdict per node so the mirror list can show WHICH node is affected.
    now = 1_000_000
    rows = [
        _row("a", "mainnode", "forkmesh", root="abc", synced="990000",
             state_hash="AAA"),
        _row("b", "in-sync", "forkmesh", root="abc", synced="980000",
             state_hash="aaa", source="remote-clone"),
        _row("c", "lagging", "forkmesh", root="abc", synced="970000",
             state_hash="old", source="remote-clone"),
        _row("d", "tampered", "forkmesh", root="abc", synced="960000",
             state_hash="bbb", source="remote-clone"),
        _row("e", "legacy", "forkmesh", root="abc", synced="950000",
             source="remote-clone"),
    ]
    payload = build_repo_mirrors_payload(
        "mainnode", "forkmesh", rows, {}, {}, now, 600_000, 5_000,
        history={"a": ["old"]},
    )
    verdicts = {m["node"]: m["integrity"] for m in payload["mirrors"]}
    assert verdicts == {
        "mainnode": "ok",        # the source matches its own attestation
        "in-sync": "ok",         # matches the source's current pin (case-insensitive)
        "lagging": "ok",         # matches a recent pin from the history window
        "tampered": "rejected",  # matches nothing the source ever attested
        "legacy": "unknown",     # no fingerprint published; gate checks live refs
    }


def test_payload_downgrades_rejected_to_healing_when_source_online():
    # Same mirrors as above, but the source of truth (mainnode) has a live host
    # right now. Clones of a failing mirror are then served from the source (see
    # Default._online_source_of_truth), so the failing node auto-heals instead of
    # blocking: its verdict is "healing", not the alarming "rejected".
    now = 1_000_000
    rows = [
        _row("a", "mainnode", "forkmesh", root="abc", synced="990000",
             state_hash="AAA"),
        _row("b", "in-sync", "forkmesh", root="abc", synced="980000",
             state_hash="aaa", source="remote-clone"),
        _row("c", "lagging", "forkmesh", root="abc", synced="970000",
             state_hash="old", source="remote-clone"),
        _row("d", "tampered", "forkmesh", root="abc", synced="960000",
             state_hash="bbb", source="remote-clone"),
        _row("e", "legacy", "forkmesh", root="abc", synced="950000",
             source="remote-clone"),
    ]
    payload = build_repo_mirrors_payload(
        "mainnode", "forkmesh", rows, {"a": now - 1_000}, {}, now, 600_000, 5_000,
        history={"a": ["old"]},
    )
    verdicts = {m["node"]: m["integrity"] for m in payload["mirrors"]}
    assert verdicts == {
        "mainnode": "ok",       # the source matches its own attestation
        "in-sync": "ok",        # matches the source's current pin
        "lagging": "ok",        # matches a recent pin from the history window
        "tampered": "healing",  # would be rejected, but the source is online
        "legacy": "unknown",    # no fingerprint published
    }
    # With the source OFFLINE the tamper gate is back in force: hard "rejected".
    offline = build_repo_mirrors_payload(
        "mainnode", "forkmesh", rows, {}, {}, now, 600_000, 5_000,
        history={"a": ["old"]},
    )
    assert {m["node"]: m["integrity"] for m in offline["mirrors"]}["tampered"] \
        == "rejected"


def test_payload_integrity_fails_open_without_source_attestation():
    # When no working-copy holder in the group ever attested a state, the gate
    # falls back to the mirror's own pins (legacy behaviour): nothing to compare
    # against, so nobody is flagged.
    now = 1_000_000
    rows = [
        _row("a", "mainnode", "forkmesh", root="abc", synced="990000"),
        _row("b", "mirror", "forkmesh", root="abc", synced="980000",
             state_hash="bbb", source="remote-clone"),
    ]
    payload = build_repo_mirrors_payload(
        "mainnode", "forkmesh", rows, {}, {}, now, 600_000, 5_000
    )
    assert all(m["integrity"] == "ok" for m in payload["mirrors"])


def test_payload_groups_mirror_with_missing_root_commit_by_name():
    # A mirror cloned from the relay can have an unset HEAD and publish an empty
    # rootCommit (issue #243). It must still group with the source of truth (which
    # does advertise a root) instead of dropping into its own name-keyed bucket and
    # vanishing from the owner's mirror-nodes list.
    now = 1_000_000
    rows = [
        _row("a", "mainnode", "forkmesh", root="abc", synced="990000", size=10),
        _row("b", "kaif-node", "forkmesh", root="", synced="980000", size=20),
        _row("c", "fork-owner", "forkmesh", root="def", synced="970000", size=30),
    ]
    payload = build_repo_mirrors_payload(
        "mainnode", "forkmesh", rows, {}, {}, now, 600_000, 5_000
    )

    # The empty-root mirror joins the source's root group by name; the genuine fork
    # (a different non-empty root) stays out.
    assert payload["groupKey"] == "root:abc"
    assert [m["node"] for m in payload["mirrors"]] == ["mainnode", "kaif-node"]


def test_payload_falls_back_to_repo_name_when_root_commit_is_absent():
    now = 1_000_000
    rows = [
        _row("a", "mainnode", "forkmesh", synced="990000"),
        _row("b", "backup", "ForkMesh", synced="980000"),
        _row("c", "backup", "other", synced="999000"),
    ]
    payload = build_repo_mirrors_payload(
        "mainnode", "forkmesh", rows, {}, {}, now, 600_000, 5_000
    )

    assert payload["groupKey"] == "name:forkmesh"
    assert [m["node"] for m in payload["mirrors"]] == ["mainnode", "backup"]
    assert payload["summary"]["online"] == 0
    assert payload["summary"]["cloneAvailable"] == 0


def test_payload_returns_none_for_missing_or_private_target():
    now = 1_000_000
    private_rows = [_row("a", "mainnode", "forkmesh", visibility="private")]
    public_rows = [_row("a", "mainnode", "forkmesh")]

    assert build_repo_mirrors_payload(
        "mainnode", "missing", public_rows, {}, {}, now, 600_000, 5_000
    ) is None
    assert build_repo_mirrors_payload(
        "mainnode", "forkmesh", private_rows, {}, {}, now, 600_000, 5_000
    ) is None


class _Clock:
    @staticmethod
    def now():
        return 1_000_000


class _Request:
    def __init__(self, method):
        self.method = method


def _response(data, status=200, **_kwargs):
    return {"status": status, "data": data}


def _load_handler(*, rows, presence=None, first_hosted=None):
    calls = []

    async def ensure_schema(_env):
        calls.append("schema")

    async def d1_all(_env, sql, *args):
        calls.append(sql)
        if "FROM repositories" in sql:
            return rows
        if "FROM host_presence" in sql:
            return presence or []
        if "FROM repo_first_hosted" in sql:
            return first_hosted or []
        return []

    async def decrypt_row(_env, data):
        return data

    namespace = {
        "Date": _Clock,
        "HOST_PRESENCE_STALE_MS": 600_000,
        "ensure_schema": ensure_schema,
        "d1_all": d1_all,
        "decrypt_row": decrypt_row,
        "_is_blocked_catalog_identity": lambda _env, _owner, _name: False,
        "json_response": _response,
    }
    handler, *_ = _load(
        "repo_mirrors_handler",
        "method_name",
        "_mirror_ms",
        "repo_mirror_group_key",
        "repo_mirror_same_group",
        "build_repo_mirrors_payload",
        "clone_state_pins",
        extra_globals=namespace,
    )
    return handler, calls


def test_repo_mirrors_handler_rejects_non_get_before_reading_tables():
    handler, calls = _load_handler(rows=[])

    response = asyncio.run(handler(object(), _Request("POST"), "mainnode", "forkmesh"))

    assert response == {"status": 405, "data": {"error": "method_not_allowed"}}
    assert calls == []


def test_repo_mirrors_handler_get_returns_public_mirrors_payload():
    handler, calls = _load_handler(
        rows=[
            {"key_bi": "a", "data": _row("a", "mainnode", "forkmesh", root="abc")["data"]},
            {"key_bi": "b", "data": _row("b", "kaif-node", "forkmesh", root="abc")["data"]},
        ],
        presence=[{"repo_bi": "a", "ts": 999_000}],
        first_hosted=[{"repo_bi": "b", "ts": 500_000}],
    )

    response = asyncio.run(handler(object(), _Request("GET"), "mainnode", "forkmesh"))

    assert response["status"] == 200
    assert response["data"]["ok"] is True
    assert response["data"]["summary"]["mirrors"] == 2
    assert [mirror["node"] for mirror in response["data"]["mirrors"]] == [
        "mainnode",
        "kaif-node",
    ]
    assert any("FROM repositories" in call for call in calls)
    assert any("FROM host_presence" in call for call in calls)
    assert any("FROM repo_first_hosted" in call for call in calls)
    # The handler also gathers the attested-pin history so each mirror carries
    # its clone-integrity verdict.
    assert any("FROM repo_state_history" in call for call in calls)
    assert all("integrity" in mirror for mirror in response["data"]["mirrors"])


def test_repo_mirrors_handler_returns_404_for_private_or_unpublished_target():
    private_handler, _ = _load_handler(
        rows=[
            {
                "key_bi": "a",
                "data": _row("a", "mainnode", "forkmesh", visibility="private")["data"],
                "is_private": 1,
            }
        ]
    )
    missing_handler, _ = _load_handler(
        rows=[
            {
                "key_bi": "a",
                "data": _row("a", "othernode", "forkmesh")["data"],
                "is_private": 0,
            }
        ]
    )

    private_response = asyncio.run(
        private_handler(object(), _Request("GET"), "mainnode", "forkmesh")
    )
    missing_response = asyncio.run(
        missing_handler(object(), _Request("GET"), "mainnode", "forkmesh")
    )

    assert private_response == {"status": 404, "data": {"error": "not_found"}}
    assert missing_response == {"status": 404, "data": {"error": "not_found"}}


ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
URLS_TEXT = (ENTRY.parent / "urls.py").read_text(encoding="utf-8")


def test_worker_exposes_repo_mirrors_route_and_uses_payload_builder():
    # The route pattern lives in urls.py now; entry.py imports and dispatches it.
    assert "REPO_MIRRORS_RE = re.compile" in URLS_TEXT
    assert "REPO_MIRRORS_RE" in ENTRY_TEXT
    assert "repo_mirrors_handler" in ENTRY_TEXT
    assert "build_repo_mirrors_payload(" in ENTRY_TEXT
    assert "host_presence" in ENTRY_TEXT
    assert "repo_first_hosted" in ENTRY_TEXT
    assert '"/mirrors"' in ENTRY_TEXT or "mirrors_match" in ENTRY_TEXT
