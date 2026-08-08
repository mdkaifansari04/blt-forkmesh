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
    STATE_PIN_HISTORY,
    admission_override_pins,
) = _load("clone_state_pins", "repo_mirror_same_group", "STATE_PIN_HISTORY",
          "admission_override_pins")


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


def test_pin_history_window_absorbs_active_issue_churn():
    # Every issue/PR/discussion action republishes the catalog with a fresh
    # state hash, so a repo under active collaboration churns pins fast. The
    # window must be deep enough that a mirror lagging by many issue edits
    # between its periodic re-syncs still serves .forkmesh/issues/ instead of
    # falling out of the accepted set and forcing the "unavailable until a live
    # desktop host serves" fallback. Guard against the window regressing to the
    # handful-of-publishes depth that produced that symptom.
    assert STATE_PIN_HISTORY >= 100

    # A mirror pinned to a state that is dozens of publishes behind the source
    # still resolves as long as that state is inside the retained history.
    history = ["s%d" % i for i in range(STATE_PIN_HISTORY)]
    mirror = _rec("mirror", source="remote-clone", state="s90")
    rows = [_row("kS", _rec("source", state="newest")), _row("kM", mirror)]
    pins = clone_state_pins(mirror, "kM", rows, {"kS": history})
    assert "s90" in pins


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


def test_admission_override_pins_collects_only_valid_member_states():
    # The override widens the accepted set to exactly the states registered
    # group members currently advertise — never arbitrary or malformed input.
    members = [
        _row("kA", _rec("alice", state="a" * 64)),
        _row("kB", _rec("bob", source="remote-clone", state="B" * 64)),
        _row("kC", _rec("carol", state="not-a-hash")),   # dropped
        _row("kD", _rec("dave", state="")),               # dropped
        {"data": None},                                    # dropped
    ]
    assert admission_override_pins(members) == {"a" * 64, "b" * 64}
    assert admission_override_pins([]) == set()
    assert admission_override_pins(None) == set()


def test_override_widens_a_real_source_pin_never_fabricates_one():
    # A mirror stuck out of the window (state "own") is normally rejected in
    # favor of the source's "good" pin. The override is applied on TOP of that
    # real pin set — it admits the mirror's own advertised state, but it can
    # only ever be reached when a genuine source pin already exists.
    src_state = "1" * 64
    mirror_state = "2" * 64
    mirror = _rec("mirror", source="remote-clone", state=mirror_state)
    rows = [_row("kS", _rec("source", state=src_state)), _row("kM", mirror)]
    strict = clone_state_pins(mirror, "kM", rows, {})
    assert strict == {src_state}                     # mirror excluded by default
    widened = set(strict) | admission_override_pins(rows)
    assert widened == {src_state, mirror_state}      # override lets it serve

    # With no source attestation at all, clone_state_pins is None and the
    # resolver's `if pins and ...override...` guard means the override is never
    # consulted — an unpinned repo can never be force-opened.
    bare_mirror = _rec("mirror", source="remote-clone", state="")
    unpinned_rows = [_row("kS", _rec("source", state=""))]
    assert clone_state_pins(bare_mirror, "kM", unpinned_rows, {}) is None


def test_admission_override_endpoint_is_signed_bounded_and_ttl_healing():
    # The override is an authenticated, self-expiring lever — not a permanent
    # relaxation of the integrity gate. Guard the security-critical properties.
    text = ENTRY.read_text(encoding="utf-8")
    # Reachable via a POST route.
    assert '"/api/mirrors/admission-override"' in text
    assert "_admin_set_admission_override" in text
    # Authenticated by the node's admin key (same auth as other admin mutations)
    # over a domain-separated canonical string.
    assert "forkmesh-admission-override-v1" in text
    assert "_admin_authorized(env, node, ts, sig, canonical)" in text
    # Stored with a native KV TTL and a hard ceiling, so it always heals back to
    # the strict policy on its own.
    assert "ADMISSION_OVERRIDE_MAX_TTL" in text
    assert "expirationTtl" in text
    # Consulted ONLY on the would-be-503 path, guarded so an unpinned repo (pins
    # is None) is never force-opened.
    assert "if not (pins and await _admission_override_active(" in text
    assert "pins = set(pins) | admission_override_pins(members)" in text
    # And audited like every other privileged mutation.
    assert '"admin.admission_override"' in text


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


def test_publish_records_source_pins_into_history():
    # Only working-copy holders' verified attestations enter the history that
    # mirrors are validated against, pruned to the newest STATE_PIN_HISTORY.
    text = _entry_text()
    assert "INSERT INTO repo_state_history" in text
    assert "record.get(\"source\") == \"local-node\"" in text
    assert "prior_state_hash" in text
    assert "history_states" in text
    assert "STATE_PIN_HISTORY" in text
    # Renaming an account carries its attested history to the new namespace.
    assert "UPDATE repo_state_history SET key_bi=" in text


def test_schema_has_pin_history_and_sticky_tables():
    text = _entry_text()
    assert "CREATE TABLE IF NOT EXISTS repo_state_history" in text
    assert "CREATE TABLE IF NOT EXISTS clone_sticky" in text
