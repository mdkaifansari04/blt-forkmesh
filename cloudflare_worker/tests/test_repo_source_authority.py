"""A second same-account checkout cannot replace the canonical repo device."""

import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ENTRY = (ROOT / "cloudflare_worker/src/entry.py").read_text(encoding="utf-8")
SCHEMA = (ROOT / "cloudflare_worker/src/schema.py").read_text(encoding="utf-8")
QT_REPOS = (ROOT / "qt_client/src/MainWindowRepos.cpp").read_text(
    encoding="utf-8")


def test_schema_tracks_one_authority_and_many_device_mirrors():
    assert "repo_source_authorities" in SCHEMA
    assert "repo_bi TEXT PRIMARY KEY, node_id TEXT NOT NULL" in SCHEMA
    assert "PRIMARY KEY (repo_bi, node_id)" in SCHEMA
    migration = ROOT / "cloudflare_worker/migrations/0117_repo_source_authorities.sql"
    assert migration.exists()


def test_catalog_demotes_other_devices_before_canonical_write():
    authority = ENTRY.index("SELECT node_id,machine_name FROM repo_source_authorities")
    different_device = ENTRY.index("and secondary_device")
    mirror_write = ENTRY.index("INSERT INTO repo_device_mirrors")
    canonical_write = ENTRY.index("_contribution_write_catalog_state", authority)
    assert authority < different_device < mirror_write < canonical_write
    assert 'mirror_record["source"] = "remote-clone"' in ENTRY
    assert '"mirror": True' in ENTRY
    assert "status=202" in ENTRY[different_device:canonical_write]


def test_authority_decision_is_lazy_and_never_transfers_implicitly():
    tree = ast.parse(ENTRY)
    function = next(
        node for node in tree.body
        if isinstance(node, ast.FunctionDef)
        and node.name == "_repo_publication_authority")
    namespace = {}
    exec(compile(ast.fix_missing_locations(ast.Module(
        body=[function], type_ignores=[])), "entry.py", "exec"), namespace)
    decide = namespace["_repo_publication_authority"]

    assert decide("node-main", "", "node-main") == ("node-main", False)
    assert decide("node-main", "", "node-clone") == ("node-main", True)
    # Backward-compatible lazy migration adopts an existing record's device.
    assert decide("", "legacy-main", "node-clone") == ("legacy-main", True)
    # A legacy row with no device identity lets the first modern signed device
    # establish the durable authority once.
    assert decide("", "", "first-modern") == ("first-modern", False)


def test_catalog_returns_canonical_and_secondary_nodes_explicitly():
    assert 'rec["canonicalNodeId"] = authority_node_id' in ENTRY
    assert 'rec["sourceAuthority"] = (' in ENTRY
    assert 'mirror["source"] = "remote-clone"' in ENTRY
    assert 'mirror["sourceAuthority"] = False' in ENTRY
    assert "SELECT node_id,data FROM repo_device_mirrors WHERE repo_bi=?" in ENTRY
    assert 'device_record["source"] = "remote-clone"' in ENTRY
    assert 'record.get("machineName") or record.get("owner", "")' in ENTRY


def test_qt_publication_binds_device_identity_and_source_kind():
    # These are catalog-v2 signed fields, so the Worker can distinguish devices
    # without trusting a display label supplied outside the signature.
    assert '{"nodeId", selfNodeId}' in QT_REPOS
    assert '{"machineName", machineNodeName()}' in QT_REPOS
    assert 'QStringLiteral("local-node")' in QT_REPOS
    assert 'QStringLiteral("remote-clone")' in QT_REPOS


def test_repo_deletion_removes_authority_and_secondary_records():
    delete_start = ENTRY.index("async def _delete_repo_scoped_state")
    delete_end = ENTRY.index("async def _delete_repo_namespace", delete_start)
    deletion = ENTRY[delete_start:delete_end]
    assert "DELETE FROM repo_device_mirrors WHERE repo_bi=?" in deletion
    assert "DELETE FROM repo_source_authorities WHERE repo_bi=?" in deletion
