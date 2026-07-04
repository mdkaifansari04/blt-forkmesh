#!/usr/bin/env python3
"""Clone integrity pins: a mirror must serve a repo state its group's
working-copy holder (the source of truth) actually attested — never just its
own self-published hash — while honest mirrors that lag the source by a few
publishes still clone (recent pin history)."""

import ast
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
MIRRORS = ENTRY.parent / "mirrors.py"
# Mirror grouping / clone-selection helpers were extracted from entry.py into
# mirrors.py; parse both sources so the AST loaders below still find them.
_WORKER_SRC = ENTRY.read_text(encoding="utf-8") + "\n" + MIRRORS.read_text(encoding="utf-8")


def _load(*names):
    tree = ast.parse(_WORKER_SRC, filename=str(ENTRY))
    selected = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Assign))
        and (getattr(node, "name", None) in names
             or any(getattr(t, "id", None) in names
                    for t in getattr(node, "targets", [])))
    ]
    module = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    namespace = {}
    exec(compile(module, str(ENTRY), "exec"), namespace)
    return [namespace[name] for name in names]


(
    clone_state_pins,
    repo_mirror_same_group,
) = _load("clone_state_pins", "repo_mirror_same_group")


def _rec(owner, *, source="local-node", state="", root="root1", name="forkmesh"):
    return {"owner": owner, "name": name, "source": source,
            "stateHash": state, "rootCommit": root}


def _row(key, rec):
    return {"key_bi": key, "data": rec}


def test_unpublished_target_is_unpinned():
    assert clone_state_pins(None, "k", [], {}) is None
    assert clone_state_pins({}, "k", [], {}) is None


def test_source_is_checked_against_its_own_pin_and_history():
    target = _rec("source", state="AAA")
    pins = clone_state_pins(target, "kS", [], {"kS": ["bbb", "ccc"]})
    assert pins == {"aaa", "bbb", "ccc"}  # normalized lowercase


def test_source_without_any_attestation_is_unpinned():
    assert clone_state_pins(_rec("source", state=""), "kS", [], {}) is None


def test_mirror_is_validated_against_source_pins_not_its_own():
    # The mirror self-attests "evil" — that hash must NOT be acceptable when a
    # working-copy holder in its group has attested the real state. This is the
    # whole point: a tampered mirror can always republish a matching self-pin.
    mirror = _rec("mirror", source="remote-clone", state="evil")
    rows = [_row("kS", _rec("source", state="good")), _row("kM", mirror)]
    pins = clone_state_pins(mirror, "kM", rows, {})
    assert pins == {"good"}
    assert "evil" not in pins


def test_mirror_lagging_the_source_matches_pin_history():
    # Source pushed + republished (pin now "new"); a mirror still serving the
    # previous state must clone fine because "old" is in the recent history.
    mirror = _rec("mirror", source="remote-clone", state="old")
    rows = [_row("kS", _rec("source", state="new")), _row("kM", mirror)]
    pins = clone_state_pins(mirror, "kM", rows, {"kS": ["old"]})
    assert pins == {"new", "old"}


def test_fork_source_does_not_pin_the_mirror():
    # A different root commit is a different logical repo; its attestations
    # must not become acceptable states for this mirror's group.
    mirror = _rec("mirror", source="remote-clone", state="own")
    rows = [_row("kF", _rec("forker", state="forked", root="otherroot"))]
    # No same-group source exists -> legacy fallback to the mirror's own pin.
    assert clone_state_pins(mirror, "kM", rows, {}) == {"own"}


def test_mirror_with_no_attesting_source_falls_back_to_self_pin():
    mirror = _rec("mirror", source="remote-clone", state="own")
    rows = [_row("kS", _rec("source", state=""))]  # source never attested
    assert clone_state_pins(mirror, "kM", rows, {}) == {"own"}
    # ...and with no self pin either, the repo is simply unpinned (fail-open).
    bare = _rec("mirror", source="remote-clone", state="")
    assert clone_state_pins(bare, "kM", rows, {}) is None


def test_legacy_record_without_source_field_counts_as_local_node():
    # Records published before the `source` field existed default to
    # local-node on write, and clone_state_pins applies the same default.
    legacy = {"owner": "old", "name": "forkmesh", "stateHash": "lll",
              "rootCommit": "root1"}
    mirror = _rec("mirror", source="remote-clone", state="own")
    assert clone_state_pins(mirror, "kM", [_row("kL", legacy)], {}) == {"lll"}
    assert clone_state_pins(legacy, "kL", [], {}) == {"lll"}


def test_multiple_working_copy_holders_union_their_pins():
    mirror = _rec("mirror", source="remote-clone", state="own")
    rows = [
        _row("kA", _rec("alice", state="a1")),
        _row("kB", _rec("bob", state="b1")),
    ]
    assert clone_state_pins(mirror, "kM", rows, {"kB": ["b0"]}) == \
        {"a1", "b1", "b0"}


def _entry_text():
    # schema DDL now lives in schema.py; concatenate it for the source checks.
    return (ENTRY.read_text(encoding="utf-8") + "\n"
            + (ENTRY.parent / "schema.py").read_text(encoding="utf-8"))


def _method_source(class_name, method_name):
    tree = ast.parse(_entry_text(), filename=str(ENTRY))
    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            for item in node.body:
                if (isinstance(item, (ast.FunctionDef, ast.AsyncFunctionDef))
                        and item.name == method_name):
                    return ast.unparse(item)
    raise AssertionError(
        "%s.%s not found in entry.py" % (class_name, method_name))


def test_git_gate_checks_membership_in_the_pin_set():
    # The DO's info/refs gate must consult _state_pins (the group-aware set),
    # not a single self-attested hash.
    src = _method_source("ForkMeshHost", "_git")
    assert "_state_pins" in src
    assert "not in pinned" in src
    assert "failed integrity check" in src


def test_publish_records_source_pins_into_history():
    # Only working-copy holders' verified attestations enter the history that
    # mirrors are validated against, pruned to the newest STATE_PIN_HISTORY.
    text = _entry_text()
    assert "INSERT INTO repo_state_history" in text
    assert "record.get(\"source\") == \"local-node\"" in text
    assert "STATE_PIN_HISTORY" in text
    # Renaming an account carries its attested history to the new namespace.
    assert "UPDATE repo_state_history SET key_bi=" in text


def test_clone_of_mirror_prefers_online_source_of_truth():
    # A public clone routes to the logical repo's source of truth while it is
    # online, so a mirror whose refs fail the integrity pin still clones (from the
    # authoritative source, never its own bytes). _git_host must consult
    # _online_source_of_truth before serving the named mirror and forward there.
    host = _method_source("Default", "_git_host")
    assert "_online_source_of_truth" in host
    assert "_forward_to_node" in host

    finder = _method_source("Default", "_online_source_of_truth")
    # It is scoped to mirrors: a working-copy holder ("local-node") is served
    # directly, never redirected...
    assert "local-node" in finder
    # ...it groups by the same logical repo...
    assert "repo_mirror_same_group" in finder
    # ...and only forwards while the source actually has a live host.
    assert "_source_has_live_host" in finder


def test_schema_has_pin_history_and_sticky_tables():
    text = _entry_text()
    assert "CREATE TABLE IF NOT EXISTS repo_state_history" in text
    assert "CREATE TABLE IF NOT EXISTS clone_sticky" in text
