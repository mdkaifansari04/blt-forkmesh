import asyncio
import importlib
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))


def metrics_module():
    sys.modules.pop("api_metrics", None)
    return importlib.import_module("api_metrics")


def test_account_route_groups_never_publish_usernames():
    metrics = metrics_module()
    assert metrics._masked_group("/api/accounts/alice") == "accounts/*"
    assert (
        metrics._masked_group("/api/accounts/alice/followers")
        == "accounts/*"
    )
    assert "alice" not in metrics.route_group("/api/accounts/alice")


def test_room_badge_and_unknown_identifiers_are_never_public_groups():
    metrics = metrics_module()
    assert metrics._masked_group("/api/badges/alice") == "badges/*"
    assert metrics._masked_group("/api/room/private-team/ws") == "room/*"
    assert metrics._masked_group("/api/customer-name/private-repo") == "other"
    for group in (
        metrics._masked_group("/api/badges/alice"),
        metrics._masked_group("/api/room/private-team/ws"),
        metrics._masked_group("/api/customer-name/private-repo"),
    ):
        assert "alice" not in group
        assert "private-team" not in group
        assert "customer-name" not in group


def test_metric_upserts_stay_below_the_d1_bind_limit():
    metrics = metrics_module()
    calls = []

    async def ensure_schema(_env):
        calls.append(("schema", ()))

    async def d1_run(_env, sql, *args):
        calls.append((sql, args))

    metrics.ensure_schema = ensure_schema
    metrics.d1_run = d1_run
    for index in range(31):
        metrics._pending[(60_000, f"group-{index}", "2xx")] = [1, 2, 2]
    asyncio.run(metrics._flush(object(), 60_000))

    writes = [call for call in calls if call[0].startswith("INSERT")]
    assert len(writes) == 3
    assert all(len(args) <= 90 for _, args in writes)
    assert not metrics._pending


def test_metrics_table_has_a_roll_forward_migration():
    migration = (ROOT / "migrations/0123_api_metrics_minute.sql").read_text(
        encoding="utf-8"
    )
    assert "CREATE TABLE IF NOT EXISTS api_metrics_minute" in migration
