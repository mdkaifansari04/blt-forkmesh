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

# Names pulled verbatim from entry.py; the rest of the module (JS imports, async
# crypto) is never executed.
_WANT_FUNCS = ("clean_string", "safe_segment", "safe_catalog_record")


def _load_record_builder():
    tree = ast.parse(ENTRY.read_text(encoding="utf-8"), filename=str(ENTRY))
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
