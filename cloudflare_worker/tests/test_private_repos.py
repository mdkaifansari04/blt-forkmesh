#!/usr/bin/env python3
"""Private-repo catalog-record checks (stdlib only).

Loads the real safe_catalog_record (and the helpers it needs) straight out of
src/entry.py without importing the Workers-only JS runtime, the same way
test_git_http.py isolates decode_git_request_body. Verifies that the visibility
field is normalized so only the literal "private" hides a repo, and that the
public default is preserved for absent/garbled values.
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
_WANT_FUNCS = ("clean_string", "safe_segment", "safe_catalog_record")


def _load_record_builder():
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
        "unquote": unquote,
        # Same shapes safe_segment relies on in entry.py.
        "ROOM_NAME_RE": re.compile(r"^[A-Za-z0-9._:-]+$"),
        "MAX_REPO_SEGMENT": 80,
    }
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return namespace["safe_catalog_record"]


safe_catalog_record = _load_record_builder()


def _base(**overrides):
    data = {"owner": "alice", "name": "secret", "maintainer": "PUBKEY"}
    data.update(overrides)
    return data


def test_visibility_defaults_to_public_when_absent():
    rec = safe_catalog_record(_base())
    assert rec is not None
    assert rec["visibility"] == "public"


def test_explicit_private_is_kept():
    rec = safe_catalog_record(_base(visibility="private"))
    assert rec["visibility"] == "private"


def test_only_literal_private_hides_a_repo():
    # Anything other than the exact string "private" must fall back to public so a
    # typo/garbled value can never accidentally hide a repo.
    for value in ("Private", "PRIVATE", "priv", "", "true", 1, None, "public"):
        rec = safe_catalog_record(_base(visibility=value))
        assert rec["visibility"] == "public", value


def test_missing_required_fields_still_rejected():
    assert safe_catalog_record({"owner": "", "name": "x", "maintainer": "k"}) is None
    assert safe_catalog_record("not a dict") is None


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
    # Only the viewer's own private repos are added (matched by owner blind index);
    # public repos stay visible to everyone.
    assert "is_private = 0 OR owner_bi = ?" in ENTRY_TEXT


def test_authenticated_listing_is_not_edge_cached():
    # Per-viewer responses (with private repos) must never touch the shared public
    # cache, or private repos would leak to anonymous visitors.
    assert "if authed_viewer:\n            return json_response(payload)" in ENTRY_TEXT
