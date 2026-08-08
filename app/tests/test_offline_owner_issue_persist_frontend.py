#!/usr/bin/env python3
"""Issue submission goes directly to an eligible mirror for materialization."""

from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js


DASHBOARD_JS = assembled_dashboard_js()


def _slice(text, start_marker, end_marker):
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    return text[start:end]


def test_submit_uses_eligible_mirror_delivery_not_owner_persistence():
    body = _slice(
        DASHBOARD_JS,
        "async function handleIssueComposeSubmit(repo, form)",
        "// --- CSV issue import",
    )
    assert "first eligible online mirror" in body
    assert "savePendingIssue" not in body
    assert "maintainer's inbox" not in body


def test_load_reads_only_committed_mirror_issues():
    load = _slice(
        DASHBOARD_JS,
        "async function loadRepoIssues(repo)",
        "async function loadRepoCollection(",
    )
    assert "state.issuesView.items = items;" in load
    assert "reconcilePendingIssues" not in load
