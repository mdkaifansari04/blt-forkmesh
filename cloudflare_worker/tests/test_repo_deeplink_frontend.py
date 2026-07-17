#!/usr/bin/env python3
"""A refresh on any repo tab or record sub-page stays on that page (adhoc #25).

The repo browser is one prebuilt document (dashboard/repo.html) whose JS resolves
the tab / tree / blob / record from the URL, so "stay on this page after refresh"
is a two-part contract:

* the Worker must serve the repo document for every such deep link, and
* the bundle must restore that exact view on boot instead of snapping to Code.

Issue detail (/owner/repo/issues/<N>) used to be the one record kind that never
mirrored its number into the URL, so a refresh dropped back to the issues list;
these tests pin it deep-linking the same way pulls and discussions already do.
"""

import sys
from pathlib import Path

from _dashboard_bundle import assembled_dashboard_js

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

import static_routes  # noqa: E402

DASHBOARD_JS = assembled_dashboard_js()


# --- Server side: every repo tab / record path serves the repo document ------


def test_worker_serves_repo_document_for_every_tab_and_record_deep_link():
    for tab in static_routes.REPO_TAB_ROUTES:
        assert static_routes.looks_like_repo_route(f"/owner/repo/{tab}"), tab
    # Record deep links (a refreshed/shared issue, PR or discussion detail).
    for kind in ("issues", "pulls", "discussions"):
        assert static_routes.looks_like_repo_route(f"/owner/repo/{kind}/5"), kind
    # Code tree/blob deep links.
    assert static_routes.looks_like_repo_route("/owner/repo/tree/src")
    assert static_routes.looks_like_repo_route("/owner/repo/blob/README.md")


# --- Client side: the bundle restores that view on boot ----------------------


def test_issue_detail_is_a_recognized_record_deep_link():
    # On boot renderRepoDetail treats issues like pulls/discussions: a numeric
    # /owner/repo/issues/<N> suffix is parsed as a record route so the detail
    # (not the list) is what re-opens.
    assert (
        '["pulls", "discussions", "issues"].includes(routeKind) && /^\\d+$/.test(routePath)'
        in DASHBOARD_JS
    )


def test_feature_panel_loader_opens_the_deep_linked_issue_detail():
    panels = DASHBOARD_JS[
        DASHBOARD_JS.index("function loadRepoFeaturePanels")
        : DASHBOARD_JS.index("function updateRepoLiveCounts")
    ]
    # The issues branch opens the record detail directly when a deep link
    # carried its number, and only otherwise falls back to the list.
    assert 'recordRoute && recordRoute.kind === "issues" && recordRoute.number' in panels
    assert 'loadRepoRecordDetail(repo, "issues", recordRoute.number)' in panels
    assert "loadRepoIssues(repo);" in panels


def test_opening_an_issue_mirrors_its_number_into_the_url():
    assert (
        '["pulls", "discussions", "issues"].includes(kind) && /^\\d+$/.test(number)'
        in DASHBOARD_JS
    )


def test_issue_back_reloads_the_list_when_a_deep_link_never_fetched_it():
    # Stepping Back from a deep-linked issue detail (the list was never loaded)
    # must fetch it rather than flash an empty "No issues".
    assert (
        "if (state.issuesView.items.length) renderRepoIssues();\n"
        "          else loadRepoIssues(state.selectedRepo);"
        in DASHBOARD_JS
    )


def test_popstate_restores_issue_detail_and_list():
    # Browser Back/Forward between an issue detail and its list is handled the
    # same as pulls/discussions.
    assert (
        '["pulls", "discussions", "issues"].includes(kind)) {\n'
        "            if (/^\\d+$/.test(path)) {\n"
        "              loadRepoRecordDetail(repo, kind, path);"
        in DASHBOARD_JS
    )
