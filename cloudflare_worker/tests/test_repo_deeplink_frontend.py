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

import static_routes

DASHBOARD_JS = assembled_dashboard_js()





def test_worker_serves_repo_document_for_every_tab_and_record_deep_link():
    for tab in static_routes.REPO_TAB_ROUTES:
        assert static_routes.looks_like_repo_route(f"/owner/repo/{tab}"), tab

    for kind in ("issues", "pulls", "discussions"):
        assert static_routes.looks_like_repo_route(f"/owner/repo/{kind}/5"), kind

    assert static_routes.looks_like_repo_route("/owner/repo/tree/src")
    assert static_routes.looks_like_repo_route("/owner/repo/blob/README.md")





def test_issue_detail_is_a_recognized_record_deep_link():



    assert (
        '["pulls", "discussions", "issues"].includes(routeKind) && /^\\d+$/.test(routePath)'
        in DASHBOARD_JS
    )


def test_feature_panel_loader_opens_the_deep_linked_issue_detail():
    panels = DASHBOARD_JS[
        DASHBOARD_JS.index("function loadRepoFeaturePanels")
        : DASHBOARD_JS.index("function updateRepoLiveCounts")
    ]


    assert 'recordRoute && recordRoute.kind === "issues" && recordRoute.number' in panels
    assert 'loadRepoRecordDetail(repo, "issues", recordRoute.number)' in panels
    assert "loadRepoIssues(repo);" in panels


def test_opening_an_issue_mirrors_its_number_into_the_url():
    assert (
        '["pulls", "discussions", "issues"].includes(kind) && /^\\d+$/.test(number)'
        in DASHBOARD_JS
    )


def test_issue_back_reloads_the_list_when_a_deep_link_never_fetched_it():


    assert (
        "if (state.issuesView.items.length) renderRepoIssues();\n"
        "          else loadRepoIssues(state.selectedRepo);"
        in DASHBOARD_JS
    )


def test_popstate_restores_issue_detail_and_list():


    assert (
        '["pulls", "discussions", "issues"].includes(kind)) {\n'
        "            if (/^\\d+$/.test(path)) {\n"
        "              loadRepoRecordDetail(repo, kind, path);"
        in DASHBOARD_JS
    )


def test_boot_tree_preload_cannot_steal_the_restored_tab():




    assert (
        'loadRepositoryTree(repo, routeKind === "tree" ? routePath : "", '
        '{ background: initialTab !== "code" });'
    ) in DASHBOARD_JS
    tree = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepositoryTree")
        : DASHBOARD_JS.index("async function loadRepositoryBlob")
    ]
    assert 'if (!background) setRepoTab("code");' in tree

    assert tree.count('navigateHistory(repoPathUrl(repo, "tree", path))') == 2
    assert tree.count('if (!background) navigateHistory(repoPathUrl(repo, "tree", path));') == 2
