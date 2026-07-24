#!/usr/bin/env python3
"""Private-repo catalog-record checks (stdlib only).

Loads the real safe_catalog_record (and the helpers it needs) straight out of
src/entry.py without importing the Workers-only JS runtime, the same way
test_git_http.py isolates decode_git_request_body. Verifies that visibility
fails closed: only an explicit public value makes a repository discoverable.
"""

import ast
import re
from pathlib import Path
from urllib.parse import unquote


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
# clean_string/safe_segment/safe_catalog_record were extracted into catalog.py;
# parse both sources so the AST loader below still finds them.
CATALOG = ENTRY.parent / "catalog.py"

# Names pulled verbatim from entry.py; the rest of the module (JS imports, async
# crypto) is never executed.
_WANT_FUNCS = (
    "clean_string", "clean_int_series", "clean_optional_integer",
    "clean_optional_usage", "clean_logo_metadata", "safe_segment",
    "safe_catalog_record", "safe_contribution_transport")


def _load_catalog_helpers():
    tree = ast.parse(
        ENTRY.read_text(encoding="utf-8") + "\n"
        + CATALOG.read_text(encoding="utf-8"), filename=str(ENTRY))
    funcs = [
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name in _WANT_FUNCS
    ]
    module = ast.fix_missing_locations(ast.Module(body=funcs, type_ignores=[]))
    namespace = {
        "re": re,
        "unquote": unquote,
        # Same shapes safe_segment relies on in entry.py.
        "ROOM_NAME_RE": re.compile(r"^[A-Za-z0-9._:-]+$"),
        "MAX_REPO_SEGMENT": 80,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace


_CATALOG_HELPERS = _load_catalog_helpers()
safe_catalog_record = _CATALOG_HELPERS["safe_catalog_record"]
safe_contribution_transport = _CATALOG_HELPERS["safe_contribution_transport"]


def _base(**overrides):
    data = {"owner": "alice", "name": "secret", "maintainer": "PUBKEY"}
    data.update(overrides)
    return data


def test_visibility_defaults_to_private_when_absent():
    rec = safe_catalog_record(_base())
    assert rec is not None
    assert rec["visibility"] == "private"


def test_explicit_private_is_kept():
    rec = safe_catalog_record(_base(visibility="private"))
    assert rec["visibility"] == "private"


def test_catalog_keeps_only_a_public_solana_address():
    valid = "11111111111111111111111111111111"
    assert safe_catalog_record(_base(solana=valid))["solana"] == valid
    assert safe_catalog_record(
        _base(solana="not-public-wallet-material"))["solana"] == ""


def test_only_literal_public_exposes_a_repo():
    # Anything other than the exact string "public" stays undiscoverable. A
    # malformed publisher may hide a public repo, but it cannot expose private
    # metadata.
    for value in ("Private", "PRIVATE", "private", "priv", "", "true", 1, None):
        rec = safe_catalog_record(_base(visibility=value))
        assert rec["visibility"] == "private", value
    assert safe_catalog_record(_base(visibility="public"))["visibility"] == "public"


def test_missing_required_fields_still_rejected():
    assert safe_catalog_record({"owner": "", "name": "x", "maintainer": "k"}) is None
    assert safe_catalog_record("not a dict") is None


def test_activity_weeks_are_clamped_and_padded():
    rec = safe_catalog_record(_base(activityWeeks=[1, "2", -5, "bad", 2_000_000]))
    assert rec["activityWeeks"][-5:] == [1, 2, 0, 0, 1_000_000]
    assert len(rec["activityWeeks"]) == 52


def test_public_host_telemetry_is_bounded_and_absence_stays_unknown():
    unknown = safe_catalog_record(_base())
    assert not {
        "cpuPercent",
        "memUsedBytes",
        "memTotalBytes",
        "diskUsedBytes",
        "diskTotalBytes",
    }.intersection(unknown)
    assert unknown.get("cpuPercent") is None
    assert unknown.get("memUsedBytes") is None
    assert unknown.get("memTotalBytes") is None
    assert unknown.get("diskUsedBytes") is None
    assert unknown.get("diskTotalBytes") is None

    reported = safe_catalog_record(_base(
        cpuPercent=149,
        memUsedBytes=900,
        memTotalBytes=800,
        diskUsedBytes=300,
        diskTotalBytes=1000,
    ))
    assert reported["cpuPercent"] == 100
    assert (reported["memUsedBytes"], reported["memTotalBytes"]) == (800, 800)
    assert (reported["diskUsedBytes"], reported["diskTotalBytes"]) == (300, 1000)


def test_partial_or_malformed_host_telemetry_stays_unknown():
    record = safe_catalog_record(_base(
        cpuPercent=-1,
        memUsedBytes=100,
        # no total: an isolated "used" number is not meaningful
        diskUsedBytes=10,
        diskTotalBytes=0,
    ))
    assert record.get("cpuPercent") is None
    assert record.get("memUsedBytes") is None
    assert record.get("memTotalBytes") is None
    assert record.get("diskUsedBytes") is None
    assert record.get("diskTotalBytes") is None

    record = safe_catalog_record(_base(
        cpuPercent=12.5,
        memUsedBytes=True,
        memTotalBytes=1024,
    ))
    assert record.get("cpuPercent") is None
    assert record.get("memUsedBytes") is None
    assert record.get("memTotalBytes") is None


def test_native_logo_metadata_is_bounded_and_source_content_is_dropped():
    metadata = {
        "description": "developer platform" * 100,
        "languages": {
            "TypeScript": 321,
            **{"Language-%02d" % index: 1 for index in range(20)},
        },
        "topics": ["topic-%02d" % index for index in range(20)],
        "fileStructure": ["path-%02d" % index for index in range(40)],
        "frameworks": ["framework-%02d" % index for index in range(20)],
        "projectCategory": "web application" * 20,
        # A publisher cannot smuggle source through the logo metadata envelope.
        "sourceContent": "PRIVATE_SOURCE_BODY",
    }
    record = safe_catalog_record(_base(
        visibility="private", logoMetadata=metadata))
    logo = record["logoMetadata"]

    assert len(logo["description"]) == 500
    assert len(logo["languages"]) == 12
    assert len(logo["topics"]) == 12
    assert len(logo["fileStructure"]) == 24
    assert len(logo["frameworks"]) == 12
    assert len(logo["projectCategory"]) == 80
    assert "sourceContent" not in logo
    assert "PRIVATE_SOURCE_BODY" not in repr(record)


def test_contribution_transport_is_bounded_but_never_enters_public_record():
    data = _base(
        contributionPayload="payload-bytes",
        contributionSig="signature-bytes",
    )

    transport = safe_contribution_transport(data)
    record = safe_catalog_record(data)

    assert transport == {
        "present": True,
        "payload": "payload-bytes",
        "signature": "signature-bytes",
        "warning": "",
    }
    assert "contributionPayload" not in record
    assert "contributionSig" not in record


def test_contribution_transport_reports_oversize_without_truncating_into_valid_data():
    transport = safe_contribution_transport(_base(
        contributionPayload="A" * (64 * 1024 + 1),
        contributionSig="signature",
    ))

    assert transport["present"] is True
    assert transport["payload"] == ""
    assert transport["warning"] == "contribution_payload_too_large"


# --- Logged-in viewer: catalog listing token contract -----------------------
# A logged-in owner additionally receives their own private repos from the
# catalog GET, gated by a forkmesh-catalog-view-v1 token. The signature is
# produced by the Qt client and verified by the worker, so the canonical string
# must match byte-for-byte on both sides; these tests pin that contract (and the
# owner-scoped SQL) so a one-sided edit can't silently break it.
# MainWindow.cpp is split into feature TUs (MainWindow*.cpp); scan them all.
QT_SRC = Path(__file__).resolve().parents[2] / "qt_client" / "src"
ENTRY_TEXT = ENTRY.read_text(encoding="utf-8")
QT_TEXT = "\n".join(
    p.read_text(encoding="utf-8") for p in sorted(QT_SRC.glob("MainWindow*.cpp"))
)


def test_listing_token_canonical_matches_across_worker_and_client():
    # Worker builds:  "forkmesh-catalog-view-v1\n" + viewer + "\n" + str(ts)
    assert (
        '"forkmesh-catalog-view-v1\\n" + viewer + "\\n" + str(ts)' in ENTRY_TEXT
    )
    # Qt client signs the same prefix + viewer + ts (separated by newlines).
    assert '"forkmesh-catalog-view-v1\\n" + viewer + "\\n" + ts' in QT_TEXT


def test_authenticated_listing_is_scoped_to_the_viewers_own_repos():
    # The authenticated branch adds the viewer's own private repos and private
    # repos shared with them; public repos stay visible to everyone.
    assert "is_private = 0 OR owner_bi = ? OR key_bi IN" in ENTRY_TEXT
    assert "SELECT repo_bi FROM repo_shares WHERE grantee_bi = ?" in ENTRY_TEXT


def test_authenticated_listing_is_not_edge_cached():
    # Per-viewer responses (with private repos) must never touch the shared public
    # cache, or private repos would leak to anonymous visitors.
    assert "if authed_viewer or bypass_cache:" in ENTRY_TEXT
    assert 'cache_control="no-store, max-age=0, must-revalidate"' in ENTRY_TEXT


def test_organization_alias_listing_does_not_publish_private_repo_names():
    start = ENTRY_TEXT.index("async def org_repos_handler")
    handler = ENTRY_TEXT[
        start:ENTRY_TEXT.index(
            "# --- Admins + manual email verification", start)
    ]
    assert "await _repo_is_private(env, node_owner, linked_repo)" in handler
    assert "await _repo_shared_with(" in handler
    assert '"repos": visible' in handler
    assert 'cache_control="no-store"' in handler


def test_organization_profile_does_not_publish_private_repo_names():
    start = ENTRY_TEXT.index("async def org_handler")
    handler = ENTRY_TEXT[
        start:ENTRY_TEXT.index("async def org_members_handler", start)
    ]
    assert "await _repo_is_private(env, node_owner, linked_repo)" in handler
    assert "await _repo_shared_with(" in handler
    assert '"repos": visible_repos' in handler
    assert 'cache_control="no-store"' in handler
