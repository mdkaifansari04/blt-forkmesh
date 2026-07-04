#!/usr/bin/env python3
"""Issues tab defaults to the Open view (issue #270).

The repo Issues tab used to list every issue regardless of state. These contract
tests pin the Open/Closed/All filter: Open is the default, closed issues are
opt-in, and the tab badge counts open issues.
"""

from pathlib import Path

from _dashboard_shell import assembled_dashboard
from _dashboard_bundle import assembled_dashboard_js


PUBLIC = Path(__file__).resolve().parents[1] / "public"
# The dashboard shell is split into HTML partials the Worker composes at request
# time; assert against the assembled document a browser actually receives.
DASHBOARD = assembled_dashboard()
# dashboard.js is likewise split into ordered public/dashboard/js/*.js fragments
# composed into one /dashboard.js by the Worker (see src/dashboard_bundle.py).
DASHBOARD_JS = assembled_dashboard_js()
STYLES = (PUBLIC / "styles.css").read_text(encoding="utf-8")
INDEX = (PUBLIC / "index.html").read_text(encoding="utf-8")


def test_dashboard_has_open_closed_all_filter_defaulting_to_open():
    assert 'class="issue-filter"' in DASHBOARD
    for state in ("open", "closed", "all"):
        assert f'data-state="{state}"' in DASHBOARD
    # The Open button is the active default.
    open_btn = DASHBOARD[
        DASHBOARD.index('id="issue-filter-open"') : DASHBOARD.index('id="issue-filter-closed"')
    ]
    assert "is-active" in open_btn
    assert 'aria-pressed="true"' in open_btn


def test_styles_define_issue_filter_control():
    assert ".issue-filter" in STYLES
    assert ".issue-filter button.is-active" in STYLES
    assert ".panel-header-aside" in STYLES


# --- Live dashboard (the page /owner/repo actually resolves to) -------------
# The repo browser users land on is dashboard.html + dashboard.js, so the
# Open-by-default behaviour has to live there to be visible on the website.


def test_dashboard_js_defaults_issue_filter_to_open():
    # The shared issues view starts on the Open filter.
    assert 'filter: "open"' in DASHBOARD_JS
    # The Issues panel renders Open/Closed/All toggles built from these states,
    # with Open pre-selected.
    assert '["open", "closed", "all"].map((stateName)' in DASHBOARD_JS
    assert 'data-dashboard-issue-filter="${stateName}"' in DASHBOARD_JS
    assert 'stateName === "open" ? "true" : "false"' in DASHBOARD_JS


def test_dashboard_js_filters_issue_list_by_state():
    render = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoIssues")
        : DASHBOARD_JS.index("function setIssueFilter")
    ]
    assert 'if (issuesView.filter === "all") return true;' in render
    assert 'if (issuesView.filter === "open") return issue.status === "open";' in render
    assert 'return issue.status !== "open";' in render


def test_dashboard_js_loads_issues_from_git_tree_and_counts_open():
    load = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoIssues")
        : DASHBOARD_JS.index("async function loadRepoCollection")
    ]
    # Published issues come from the repo's issues/ git folder, not the inbox.
    assert "/tree?path=" in load and "issues/${number}/issue.md" in load
    # The default Open view is rendered through the filter, and the tab badge
    # counts the open issues.
    assert 'issuesView.filter = "open";' in load
    assert 'setRepoTabCount("issues", items.filter((issue) => issue.status === "open").length);' in load
    assert "renderRepoIssues();" in load


def test_dashboard_js_shows_per_tab_counts():
    # Every repo tab carries a count badge, filled from the bundled root-tree
    # tallies plus the open-issue and mirror counts.
    assert 'data-dashboard-repo-tab-count="${tab}"' in DASHBOARD_JS
    assert "function applyServedCounts(counts)" in DASHBOARD_JS
    assert 'setRepoTabCount("pulls", Number(counts.pulls));' in DASHBOARD_JS
    assert 'setRepoTabCount("discussions", Number(counts.discussions));' in DASHBOARD_JS
    assert 'setRepoTabCount("mirrors", mirrors.length);' in DASHBOARD_JS


def test_homepage_links_to_active_nodes():
    # The homepage surfaces a link to the live network/active-nodes page.
    assert 'href="/network"' in INDEX
    assert "View active nodes" in INDEX


def test_dashboard_js_batches_record_reads_and_lazy_loads_tabs():
    # Record lists (issues/pulls/discussions) must fetch their markdown files
    # through ONE batched /blobs request — fetching each record as its own
    # /blob call fired 50+ parallel requests per page view and tripped the
    # relay's per-repo rate limit — and must only load on the tab's FIRST
    # view, not eagerly on every repo open.
    assert "async function fetchRepoBlobs(repo, paths)" in DASHBOARD_JS
    assert "/blobs?" in DASHBOARD_JS
    assert 'query.append("path", path)' in DASHBOARD_JS
    # Both list loaders go through the batch, never a per-record /blob loop.
    load = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoIssues")
        : DASHBOARD_JS.index("async function loadRepoCollection")
    ]
    assert "fetchRepoBlobs(" in load
    assert 'repoLiveUrl(repo, "blob"' not in load
    records = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoRecordsFromMirror")
        : DASHBOARD_JS.index("function renderRepoCollectionPagination")
    ]
    assert "fetchRepoBlobs(" in records
    assert 'repoLiveUrl(repo, "blob"' not in records
    # Issues are lazy like pulls/discussions: repo open loads commits+mirrors
    # only; the issue list waits for the first Issues-tab view.
    panels = DASHBOARD_JS[
        DASHBOARD_JS.index("function loadRepoFeaturePanels")
        : DASHBOARD_JS.index("function updateRepoLiveCounts")
    ]
    assert "state.loadedRepoTabs = {};" in panels
    assert '["issues", "pulls", "discussions", "releases", "agents"].includes(tab)' in DASHBOARD_JS
    # The tab badge still fills immediately from the root tree's bundled counts.
    assert 'setRepoTabCount("issues", Number(counts.issues));' in DASHBOARD_JS
