#!/usr/bin/env python3
"""Durable content-free clone and website counters for headless mirrors."""

import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import headless_mirror_refresh as refresh  # noqa: E402
import mirror_gateway as gateway  # noqa: E402


def test_gateway_initializes_truthful_zero_counters(tmp_path: Path):
    path = tmp_path / gateway.SERVICE_COUNTERS_FILE
    counters = gateway.GatewayServiceCounters(
        path,
        clock_ms=lambda: 1_765_000_000_000,
    )
    assert counters.ensure_repository("mirror6", "ForkMesh")
    assert counters.ensure_repository("mirror6", "forkmesh")
    assert not counters.ensure_repository("../mirror6", "forkmesh")
    counters.close()

    value = json.loads(path.read_text(encoding="utf-8"))
    assert value["repositories"] == {
        "mirror6/forkmesh": {
            "clonesServed": 0,
            "websiteServed": 0,
            "updatedAt": 1_765_000_000_000,
        }
    }


def test_gateway_counters_persist_only_completed_clone_and_browse_requests(
    tmp_path: Path,
):
    path = tmp_path / gateway.SERVICE_COUNTERS_FILE
    counters = gateway.GatewayServiceCounters(
        path,
        clock_ms=lambda: 1_765_000_000_000,
    )
    assert counters.record("mirror6", "forkmesh", "git-upload-pack")
    assert counters.record("mirror6", "forkmesh", "tree")
    assert counters.record("mirror6", "forkmesh", "raw")
    assert not counters.record("mirror6", "forkmesh", "git-info-refs")
    assert not counters.record("mirror6", "forkmesh", "actions-status")
    counters.close()

    assert path.stat().st_mode & 0o777 == 0o600
    value = json.loads(path.read_text(encoding="utf-8"))
    assert value == {
        "schemaVersion": 1,
        "type": gateway.SERVICE_COUNTERS_TYPE,
        "repositories": {
            "mirror6/forkmesh": {
                "clonesServed": 1,
                "websiteServed": 2,
                "updatedAt": 1_765_000_000_000,
            },
        },
    }
    reloaded = gateway.GatewayServiceCounters(
        path,
        clock_ms=lambda: 1_765_000_001_000,
    )
    assert reloaded.record("mirror6", "forkmesh", "stats")
    reloaded.close()
    assert (
        json.loads(path.read_text(encoding="utf-8"))["repositories"]
        ["mirror6/forkmesh"]["websiteServed"]
        == 3
    )


def test_gateway_route_counter_requires_successful_delivery_and_exact_route():
    calls = []
    application = SimpleNamespace(
        service_counters=SimpleNamespace(
            record=lambda owner, repository, operation: calls.append(
                (owner, repository, operation)
            ) or True
        )
    )
    record = gateway.GatewayApplication.record_service_counter
    target = "/v1/repositories/mirror6/forkmesh/tree?path=src"
    assert not record(
        application, target, "tree", 503, delivered=True
    )
    assert not record(
        application, target, "tree", 200, delivered=False
    )
    assert not record(
        application, target, "raw", 200, delivered=True
    )
    assert record(
        application, target, "tree", 200, delivered=True
    )
    assert calls == [("mirror6", "forkmesh", "tree")]


def test_refresh_publishes_exact_counters_and_rejects_unsafe_counter_files(
    tmp_path: Path,
):
    path = tmp_path / refresh.SERVICE_COUNTERS_FILE
    path.write_text(
        json.dumps({
            "schemaVersion": 1,
            "type": refresh.SERVICE_COUNTERS_TYPE,
            "repositories": {
                "mirror6/forkmesh": {
                    "clonesServed": 7,
                    "websiteServed": 12,
                    "updatedAt": 1_765_000_000_000,
                },
            },
        }),
        encoding="utf-8",
    )
    os.chmod(path, 0o600)
    config = SimpleNamespace(
        gateway_config_path=tmp_path / "gateway.json",
        node_owner="mirror6",
        repository_name="ForkMesh",
    )
    assert refresh._sample_gateway_service_counters(config) == {
        "clonesServed": "7",
        "websiteServed": "12",
    }

    os.chmod(path, 0o644)
    assert refresh._sample_gateway_service_counters(config) == {}
    path.unlink()
    target = tmp_path / "outside.json"
    target.write_text("{}", encoding="utf-8")
    path.symlink_to(target)
    assert refresh._sample_gateway_service_counters(config) == {}
