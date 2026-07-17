#!/usr/bin/env python3
"""Owner's offline issue submissions persist across reloads (issue #379).

When the owner of a repo files an issue while their source-of-truth node is down
but a mirror is still serving the repo, the submission lands in the relay inbox
and would previously vanish from the list on the next reload (the mirror-loaded
issue list overwrote the optimistic, session-only placeholder). These contract
tests pin the local-persistence path so such issues "show up fully" until the
owner's node returns and drains them - after which the numbered mirror copy
reconciles the local placeholder away.
"""

from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js


DASHBOARD_JS = assembled_dashboard_js()


def _slice(text, start_marker, end_marker):
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    return text[start:end]


def test_owner_check_and_mirror_helpers_exist():
    # The owner-of-source-of-truth gate and the "served by a mirror" liveness
    # check are what scope issue #379 to exactly the offline-owner case.
    assert "function isRepoOwner(repo)" in DASHBOARD_JS
    assert "function repoServedByMirror(repo)" in DASHBOARD_JS


def test_persistence_store_helpers_exist():
    for fn in (
        "function loadPendingIssues(repo)",
        "function savePendingIssue(repo, item)",
        "function reconcilePendingIssues(repo, mirrorIssues)",
    ):
        assert fn in DASHBOARD_JS, fn
    # Kept in localStorage under a dedicated key, per repo, so it survives reload.
    assert '"forkmesh.pendingIssues"' in DASHBOARD_JS


def test_submit_persists_only_for_offline_owner():
    # On successful submit the owner-offline branch is gated on BOTH ownership
    # and the source-of-truth being down while a mirror serves the repo.
    body = _slice(
        DASHBOARD_JS,
        "const ownerOffline = isRepoOwner(repo) && repoServedByMirror(repo);",
        "setRepoTabCount(\"issues\"",
    )
    assert "if (ownerOffline) savePendingIssue(repo, pendingItem);" in body
    # The hint copy tells the owner it will sync back when the node returns.
    assert "sync to your node when it comes back online" in DASHBOARD_JS


def test_load_merges_and_reconciles_pending():
    # loadRepoIssues folds persisted pending items in on top of the mirror list
    # and drops any the node has since drained (matched against mirror issues).
    load = _slice(
        DASHBOARD_JS,
        "async function loadRepoIssues(repo)",
        "async function loadRepoCollection(",
    )
    assert "reconcilePendingIssues(repo, items)" in load
    assert "const merged = pending.length ? [...pending, ...items] : items;" in load
    # The empty-mirror branch still surfaces the owner's offline submissions.
    assert "reconcilePendingIssues(repo, [])" in load


def test_reconcile_matches_on_title():
    body = _slice(
        DASHBOARD_JS,
        "function reconcilePendingIssues(repo, mirrorIssues)",
        "async function submitWebIssue",
    )
    # A pending item retires once a mirror issue with the same title appears.
    assert "issue.title" in body
    assert "item.title" in body
