from pathlib import Path
import tomllib


ROOT = Path(__file__).resolve().parents[1]
ENTRY = (ROOT / "src" / "entry.py").read_text(encoding="utf-8")
WRANGLER = tomllib.loads(
    (ROOT / "wrangler.toml").read_text(encoding="utf-8")
)


def test_cache_payloads_stay_in_cache_api_and_kv_is_sparse_metadata_only():
    namespaces = {
        item["binding"]: item["id"]
        for item in WRANGLER.get("kv_namespaces", [])
    }
    assert namespaces["WORLD_CACHE_META"]
    assert "EDGE_CACHE_KV_SNAPSHOT_MINUTES = 10" in ENTRY
    assert '"maximumScheduledWritesPerDay"' in ENTRY
    assert '"privateResponsesCached": False' in ENTRY
    assert '"kvRole": "content-free global diagnostics snapshot only"' in ENTRY
    assert "if minute % EDGE_CACHE_KV_SNAPSHOT_MINUTES == 0:" in ENTRY
    assert "expirationTtl" in ENTRY


def test_cache_diagnostics_are_bounded_redacted_and_live():
    assert '"/api/world/cache-diagnostics"' in ENTRY
    assert "world_cache_diagnostics_handler" in ENTRY
    assert '"namespaceIdExposed": False' in ENTRY
    assert '"keyCountUsed": 1 if namespace is not None else 0' in ENTRY
    assert '"routes": {' in ENTRY
    assert "_edge_cache_record(" in ENTRY
    assert 'if int(_EDGE_CACHE_STATS["startedAt"]) <= 0:' in ENTRY
    assert "cache_control=\"no-store, max-age=0, must-revalidate\"" in ENTRY


def test_world_hot_reads_reuse_edge_and_isolate_results():
    assert "WORLD_RELAY_INSTANCES_CACHE_KEY" in ENTRY
    assert "await edge_cache_match(WORLD_RELAY_INSTANCES_CACHE_KEY)" in ENTRY
    assert "await edge_cache_put(WORLD_RELAY_INSTANCES_CACHE_KEY, response)" in ENTRY
    assert "WORLD_SYSTEM_CAPACITY_CACHE_MS = 60 * 1000" in ENTRY
    assert "_WORLD_SYSTEM_CAPACITY_CACHE.update({" in ENTRY
