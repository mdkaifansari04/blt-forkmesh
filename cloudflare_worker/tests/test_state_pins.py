#!/usr/bin/env python3
"""Clone integrity pins: a mirror must serve a repo state its group's
working-copy holder (the source of truth) actually attested — never just its
own self-published hash — while honest mirrors that lag the source by a few
publishes still clone (recent pin history)."""

import ast
from pathlib import Path


ENTRY = Path(__file__).resolve().parents[1] / "src" / "entry.py"
MIRRORS = ENTRY.parent / "mirrors.py"


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
    STATE_PIN_HISTORY,
) = _load("clone_state_pins", "repo_mirror_same_group", "STATE_PIN_HISTORY")


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
    assert pins == {"aaa", "bbb", "ccc"}


def test_source_without_any_attestation_is_unpinned():
    assert clone_state_pins(_rec("source", state=""), "kS", [], {}) is None


def test_mirror_is_validated_against_source_pins_not_its_own():



    mirror = _rec("mirror", source="remote-clone", state="evil")
    rows = [_row("kS", _rec("source", state="good")), _row("kM", mirror)]
    pins = clone_state_pins(mirror, "kM", rows, {})
    assert pins == {"good"}
    assert "evil" not in pins


def test_mirror_lagging_the_source_matches_pin_history():


    mirror = _rec("mirror", source="remote-clone", state="old")
    rows = [_row("kS", _rec("source", state="new")), _row("kM", mirror)]
    pins = clone_state_pins(mirror, "kM", rows, {"kS": ["old"]})
    assert pins == {"new", "old"}


def test_pin_history_window_absorbs_active_issue_churn():







    assert STATE_PIN_HISTORY >= 100



    history = ["s%d" % i for i in range(STATE_PIN_HISTORY)]
    mirror = _rec("mirror", source="remote-clone", state="s90")
    rows = [_row("kS", _rec("source", state="newest")), _row("kM", mirror)]
    pins = clone_state_pins(mirror, "kM", rows, {"kS": history})
    assert "s90" in pins


def test_fork_source_does_not_pin_the_mirror():


    mirror = _rec("mirror", source="remote-clone", state="own")
    rows = [_row("kF", _rec("forker", state="forked", root="otherroot"))]

    assert clone_state_pins(mirror, "kM", rows, {}) == {"own"}


def test_mirror_with_no_attesting_source_falls_back_to_self_pin():
    mirror = _rec("mirror", source="remote-clone", state="own")
    rows = [_row("kS", _rec("source", state=""))]
    assert clone_state_pins(mirror, "kM", rows, {}) == {"own"}

    bare = _rec("mirror", source="remote-clone", state="")
    assert clone_state_pins(bare, "kM", rows, {}) is None


def test_legacy_record_without_source_field_counts_as_local_node():


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


def test_publish_records_source_pins_into_history():


    text = _entry_text()
    assert "INSERT INTO repo_state_history" in text
    assert "record.get(\"source\") == \"local-node\"" in text
    assert "prior_state_hash" in text
    assert "history_states" in text
    assert "STATE_PIN_HISTORY" in text

    assert "UPDATE repo_state_history SET key_bi=" in text


def test_schema_has_pin_history_and_sticky_tables():
    text = _entry_text()
    assert "CREATE TABLE IF NOT EXISTS repo_state_history" in text
    assert "CREATE TABLE IF NOT EXISTS clone_sticky" in text
