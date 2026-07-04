#!/usr/bin/env python3
"""network_leaderboards per-node detail card (adhoc #213).

The website's Network page "Connected nodes" list only showed a name and an
online-minutes figure. The desktop client's Mirror nodes panel already shows
per-node commit/synced/size/issues/commits/branches/pulls/discussions/
platform/version/clones/website/artifacts (adhoc #56's catalog record
fields) -- this closes that gap for the website by having
network_leaderboards() sum those counters across every repo a node mirrors
and surface the most-recently-updated repo's commit/branch/platform/version/
sync time as the node's "latest" state.
"""

import ast
import asyncio
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"


def _load(*names, extra_globals=None):
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
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
    return namespace


NOW = 1_000_000_000_000


async def _identity(data):
    return data


async def _const(value):
    return value


def _run(repo_records):
    async def d1_all(env, sql, *args):
        if "repositories" in sql:
            return [{"key_bi": f"bi:{i}", "data": rec} for i, rec in enumerate(repo_records)]
        return []

    async def d1_first(env, sql, *args):
        return {}

    async def noop(*a, **k):
        return None

    captured = {}

    def json_response(payload, cache_seconds=None):
        captured.update(payload)
        return payload

    class FakeDate:
        @staticmethod
        def now():
            return NOW

        @staticmethod
        def parse(s):
            # Model updatedAt as sortable numeric strings ("1", "2", ...); real
            # values are ISO timestamps, but only relative ordering matters here.
            try:
                return float(s)
            except (TypeError, ValueError):
                return float("nan")

    g = _load(
        "network_leaderboards", "_catalog_updated_ms",
        extra_globals={
            "Date": FakeDate,
            "MAX_NODE_NAME": 64,
            "ONLINE_HISTORY_RETAIN_MS": 1000,
            "LEADERBOARD_LIMIT": 10,
            "NETWORK_LEADERBOARDS_CACHE_KEY": "k",
            "NETWORK_STATS_TTL": 20,
            "ensure_schema": noop,
            "d1_run": noop,
            "d1_all": d1_all,
            "d1_first": d1_first,
            "decrypt_row": (lambda env, data: _identity(data)),
            "_is_blocked_catalog_identity": (lambda env, o, n: False),
            "_short_presence_label": (lambda kind, key: key),
            "clean_string": (lambda s, n: str(s or "").strip()[:n]),
            "_funds_received_boards": (lambda env: _const(
                {"mainnode": [], "contributor": [], "project": []})),
            "edge_cache_match": (lambda key: _const(None)),
            "edge_cache_put": noop,
            "json_response": json_response,
        },
    )
    asyncio.run(g["network_leaderboards"](None))
    return captured


def test_sums_counters_across_a_nodes_repos_and_keeps_latest_state():
    out = _run([
        {
            "owner": "newnewnode", "name": "repoA", "sizeBytes": 100,
            "issueCount": 3, "commitCount": 10, "branchCount": 2,
            "pullCount": 1, "discussionCount": 0, "artifactCount": 0,
            "clonesServed": 5, "websiteServed": 7,
            "commit": "aaaaaaaaaaaa", "branch": "main", "lastSync": "1",
            "platform": "linux", "version": "0.5.1", "updatedAt": "1",
        },
        {
            "owner": "newnewnode", "name": "repoB", "sizeBytes": 50,
            "issueCount": 1, "commitCount": 4, "branchCount": 1,
            "pullCount": 0, "discussionCount": 2, "artifactCount": 1,
            "clonesServed": 0, "websiteServed": 3,
            "commit": "bbbbbbbbbbbb", "branch": "dev", "lastSync": "2",
            "platform": "linux", "version": "0.5.2", "updatedAt": "2",
        },
        {
            "owner": "vm1", "name": "repoC", "sizeBytes": 20,
            "issueCount": 0, "commitCount": 1, "branchCount": 1,
            "pullCount": 0, "discussionCount": 0, "artifactCount": 0,
            "clonesServed": 0, "websiteServed": 0,
            "commit": "cccccccccccc", "branch": "main", "lastSync": "1",
            "platform": "windows", "version": "0.5.1", "updatedAt": "1",
        },
    ])
    nodes = {n["name"]: n for n in out["nodes"]}
    assert set(nodes) == {"newnewnode", "vm1"}

    newnewnode = nodes["newnewnode"]
    assert newnewnode["sizeBytes"] == 150
    assert newnewnode["issueCount"] == 4
    assert newnewnode["commitCount"] == 14
    assert newnewnode["branchCount"] == 3
    assert newnewnode["pullCount"] == 1
    assert newnewnode["discussionCount"] == 2
    assert newnewnode["artifactCount"] == 1
    assert newnewnode["clonesServed"] == 5
    assert newnewnode["websiteServed"] == 10
    # repoB has the later updatedAt ("2" > "1"), so its state wins.
    assert newnewnode["commit"] == "bbbbbbbbbbbb"
    assert newnewnode["branch"] == "dev"
    assert newnewnode["version"] == "0.5.2"
    assert "_updatedMs" not in newnewnode

    vm1 = nodes["vm1"]
    assert vm1["sizeBytes"] == 20
    assert vm1["platform"] == "windows"


if __name__ == "__main__":
    test_sums_counters_across_a_nodes_repos_and_keeps_latest_state()
    print("ok")
