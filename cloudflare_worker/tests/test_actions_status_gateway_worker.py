"""Private, bounded mirror Actions status transport contracts."""

import ast
import json
import os
from pathlib import Path
import re
from types import SimpleNamespace
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / "cloudflare_worker"
ENTRY = WORKER / "src" / "entry.py"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(WORKER / "src"))

import edge_routing  # noqa: E402
import mirror_gateway as gateway  # noqa: E402
import urls  # noqa: E402


NOW = 1_784_920_000_000
COMMIT = "a" * 40


def summary(**changes):
    value = {
        "schemaVersion": 1,
        "type": "forkmesh.mirror-actions-summary",
        "node": "mirror2",
        "updatedAt": NOW - 1000,
        "expiresAt": NOW + 9 * 60 * 1000,
        "runs": [
            {
                "id": 7,
                "owner": "forkmesh",
                "repository": "forkmesh",
                "workflow": "Deploy",
                "commit": COMMIT,
                "ref": "refs/heads/main",
                "status": "success",
                "createdAt": NOW - 5000,
                "startedAt": NOW - 4000,
                "finishedAt": NOW - 2000,
                "logTail": "token=[REDACTED]\nAll steps completed.",
            },
            {
                "id": 8,
                "owner": "other",
                "repository": "forkmesh",
                "workflow": "Other owner",
                "commit": "b" * 40,
                "ref": "refs/heads/main",
                "status": "queued",
                "createdAt": NOW - 1000,
                "startedAt": 0,
                "finishedAt": 0,
                "logTail": "",
            },
        ],
    }
    value.update(changes)
    return value


def write_summary(path, value):
    path.write_text(
        json.dumps(value, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )
    path.chmod(0o600)


def worker_validator():
    tree = ast.parse(ENTRY_TEXT, filename=str(ENTRY))
    node = next(
        item for item in tree.body
        if isinstance(item, ast.FunctionDef)
        and item.name == "_https_mirror_actions_result"
    )
    namespace = {
        "HTTPS_MIRROR_ACTIONS_LEASE_MAX_MS": 15 * 60 * 1000,
        "HTTPS_MIRROR_ACTIONS_CLOCK_SKEW_MS": 60 * 1000,
        "HTTPS_MIRROR_ACTIONS_RUN_LIMIT": 20,
        "HTTPS_MIRROR_ACTIONS_LOG_TAIL_MAX_BYTES": 16 * 1024,
        "re": re,
    }
    exec(compile(
        ast.fix_missing_locations(ast.Module(body=[node], type_ignores=[])),
        str(ENTRY),
        "exec",
    ), namespace)
    return namespace["_https_mirror_actions_result"]


def test_owner_only_summary_is_lease_checked_filtered_and_bounded(tmp_path):
    path = tmp_path / "actions-summary.json"
    write_summary(path, summary())
    result = gateway._load_actions_summary(
        path,
        node="mirror2",
        owner="forkmesh",
        repository="forkmesh",
        now_ms=NOW,
    )
    assert result["node"] == "mirror2"
    assert len(result["runs"]) == 1
    assert result["runs"][0] == {
        "id": 7,
        "workflow": "Deploy",
        "commit": COMMIT,
        "ref": "refs/heads/main",
        "status": "success",
        "createdAt": NOW - 5000,
        "startedAt": NOW - 4000,
        "finishedAt": NOW - 2000,
        "logTail": "token=[REDACTED]\nAll steps completed.",
    }
    assert "owner" not in result["runs"][0]
    assert "repository" not in result["runs"][0]

    write_summary(path, summary(expiresAt=NOW))
    with pytest.raises(gateway.GatewayError, match="unavailable"):
        gateway._load_actions_summary(
            path,
            node="mirror2",
            owner="forkmesh",
            repository="forkmesh",
            now_ms=NOW,
        )

    write_summary(path, summary())
    path.chmod(0o640)
    with pytest.raises(gateway.GatewayError, match="unavailable"):
        gateway._load_actions_summary(
            path,
            node="mirror2",
            owner="forkmesh",
            repository="forkmesh",
            now_ms=NOW,
        )


def test_summary_reader_rejects_symlinks_duplicates_and_unredacted_shape(tmp_path):
    real = tmp_path / "real.json"
    write_summary(real, summary())
    link = tmp_path / "actions-summary.json"
    link.symlink_to(real)
    with pytest.raises(gateway.GatewayError, match="unavailable"):
        gateway._load_actions_summary(
            link,
            node="mirror2",
            owner="forkmesh",
            repository="forkmesh",
            now_ms=NOW,
        )

    duplicate = tmp_path / "duplicate.json"
    duplicate.write_text(
        '{"schemaVersion":1,"schemaVersion":1,"type":'
        '"forkmesh.mirror-actions-summary","node":"mirror2",'
        f'"updatedAt":{NOW - 1},"expiresAt":{NOW + 1000},"runs":[]}}',
        encoding="utf-8",
    )
    duplicate.chmod(0o600)
    with pytest.raises(gateway.GatewayError, match="unavailable"):
        gateway._load_actions_summary(
            duplicate,
            node="mirror2",
            owner="forkmesh",
            repository="forkmesh",
            now_ms=NOW,
        )

    value = summary()
    value["runs"][0]["variables"] = {"SECRET": "do-not-return"}
    write_summary(real, value)
    with pytest.raises(gateway.GatewayError, match="unavailable"):
        gateway._load_actions_summary(
            real,
            node="mirror2",
            owner="forkmesh",
            repository="forkmesh",
            now_ms=NOW,
        )


def test_gateway_response_has_no_store_and_no_local_path(tmp_path):
    path = tmp_path / "actions-summary.json"
    write_summary(path, summary())
    app = object.__new__(gateway.GatewayApplication)
    app.config = SimpleNamespace(
        node="mirror2", actions_summary_path=path)
    app.clock_ms = lambda: NOW
    response = app._actions_status("forkmesh", "forkmesh")
    assert response.status == 200
    assert response.headers["Cache-Control"] == "no-store"
    payload = json.loads(response.body)
    assert payload["runs"][0]["logTail"].endswith("All steps completed.")
    encoded = response.body.decode()
    assert str(path) not in encoded
    assert "SECRET" not in encoded


def test_worker_revalidates_every_field_and_strips_route_identity():
    validate = worker_validator()
    gateway_value = gateway._load_actions_summary
    del gateway_value
    value = {
        "ok": True,
        "node": "mirror2",
        "updatedAt": NOW - 1000,
        "expiresAt": NOW + 1000,
        "runs": [{
            key: val for key, val in summary()["runs"][0].items()
            if key not in {"owner", "repository"}
        }],
    }
    checked = validate(value, "mirror2", NOW)
    assert checked == value
    value["runs"][0]["logTail"] = "x" * (16 * 1024 + 1)
    assert validate(value, "mirror2", NOW) is None
    value["runs"][0]["logTail"] = ""
    value["runs"][0]["workflowPath"] = ".forkmesh/deploy.yml"
    assert validate(value, "mirror2", NOW) is None


def test_route_requires_account_or_org_write_and_attested_operation():
    assert urls.REPO_ACTION_RUNS_RE.fullmatch(
        "/api/repo/forkmesh/forkmesh/actions/runs")
    assert "actions-status" in edge_routing.PUBLIC_OPERATIONS
    assert "actions-status" in gateway.PUBLIC_OPERATIONS
    assert "actions-status" not in gateway.DEFAULT_PUBLIC_OPERATIONS
    target = edge_routing.masked_target_url(
        "https://mirror.example.test",
        "forkmesh",
        "forkmesh",
        "actions-status",
    )
    assert target.endswith(
        "/v1/repositories/forkmesh/forkmesh/actions-status")
    handler = ENTRY_TEXT[
        ENTRY_TEXT.index("async def repository_actions_status_handler"):
        ENTRY_TEXT.index("\n\ndef _https_mirror_merge_response")
    ]
    for required in (
        "_account_session_record",
        "_account_owns_node",
        "_org_write_allowed",
        "_https_mirror_actions_proxy",
        "_audit_sensitive_action",
        "private, no-store",
    ):
        assert required in handler
    source = (ROOT / "tools" / "mirror_gateway.py").read_text()
    assert (
        "actions-status requires an owner-only actionsSummaryPath"
        in source
    )
    assert "self._authorize(method, target, headers, body)" in source
