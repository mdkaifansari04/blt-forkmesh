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
# The dashboard shell is split into HTML partials and prebuilt before deploy;
# assert against the assembled document a browser actually receives.
DASHBOARD = assembled_dashboard()
# dashboard.js is likewise built from ordered public/dashboard/js/*.js fragments
# before deploy (see src/dashboard_bundle.py).
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
    # Published issues come from the repo's .forkmesh/issues/ git folder, not the inbox.
    assert 'repoLiveUrl(repo, "tree", { path: ".forkmesh/issues" })' in load
    assert "issueJsonPath(Number(entry.name))" in load
    assert "parseIssueJson(blobText(blob), number)" in load
    # The default Open view is rendered through the filter, and the tab badge
    # counts the open issues.
    assert 'issuesView.filter = "open";' in load
    # The badge counts open issues across the mirror list plus any persisted
    # offline-owner submissions folded in (merged; see issue #379).
    assert 'setRepoTabCount("issues", merged.filter((issue) => issue.status === "open").length);' in load
    assert "renderRepoIssues();" in load


def test_dashboard_js_shows_per_tab_counts():
    # Every repo tab carries a count badge, filled from the bundled root-tree
    # tallies plus the open-issue and mirror counts.
    assert 'data-dashboard-repo-tab-count="${tab}"' in DASHBOARD_JS
    assert "function applyServedCounts(counts)" in DASHBOARD_JS
    assert 'setRepoTabCount("issues", openIssues);' in DASHBOARD_JS
    assert 'setRepoTabCount("pulls", Number(counts.pulls));' in DASHBOARD_JS
    assert 'setRepoTabCount("discussions", Number(counts.discussions));' in DASHBOARD_JS
    assert 'setRepoTabCount("mirrors", mirrors.length);' in DASHBOARD_JS


def test_dashboard_js_issue_header_counts_open_not_total():
    # Issue #397: every issue count the web badges must be the OPEN count, not
    # open+closed. The served tree tallies now carry an open/closed split, and
    # applyServedCounts badges the Issues tab from the open count (falling back
    # to the bundled total only for older hosts), filling the panel's
    # "N Open / N Closed" header from the served split.
    served = DASHBOARD_JS[
        DASHBOARD_JS.index("function applyServedCounts(counts)")
        : DASHBOARD_JS.index("function issueMatchesQuery")
    ]
    assert "Number.isFinite(Number(counts.openIssues))" in served
    assert "? Number(counts.openIssues)" in served
    assert ": Number(counts.issues);" in served
    assert 'setRepoTabCount("issues", openIssues);' in served
    assert 'setRepoCollectionCounts("issues", openIssues, Number(counts.closedIssues));' in served


def test_repo_host_serves_open_and_closed_issue_split():
    # The desktop host that computes the served root-tree counts reports an
    # open/closed issue split so the website can badge headers with the open
    # count only (issue #397). Closed issues keep their .forkmesh/issues/<n>/
    # directory, so a bare directory count would overstate the open total.
    repo_host = (
        Path(__file__).resolve().parents[2] / "qt_client" / "src" / "RepoHost.cpp"
    ).read_text(encoding="utf-8")
    assert "int countOpenIssues(const QString &mirrorPath" in repo_host
    # Reads each issue record's top-level status; anything not "closed" is open.
    assert "issue-%1.json" in repo_host
    assert 'value(QStringLiteral("status"))' in repo_host
    assert 'if (status != QLatin1String("closed"))' in repo_host
    # rootCountsFor emits the split alongside the legacy total.
    counts = repo_host[
        repo_host.index("QJsonObject rootCountsFor")
        : repo_host.index("QJsonObject rootCountsFor") + 900
    ]
    assert '{"openIssues", openIssues}' in counts
    assert '{"closedIssues", closedIssues}' in counts
    assert '{"issues", openIssues + closedIssues}' in counts


def test_advertised_catalog_issue_count_is_open_only():
    # adhoc #29: the catalog's issueCount seeds the Issues tab badge on the FIRST
    # repo render (before the live-served open/closed split from #397 arrives),
    # and catalog.py documents it as the OPEN count. The desktop advert must
    # therefore count open issues, not every .forkmesh/issues/<n> directory
    # (closed issues keep their directory on disk, so a bare dir count showed all
    # issues). mirrorIssueCount now delegates to a status-aware open counter.
    internal = (
        Path(__file__).resolve().parents[2]
        / "qt_client"
        / "src"
        / "MainWindowInternal.h"
    ).read_text(encoding="utf-8")
    assert "inline int mirrorOpenIssueCount(const QString &mirrorPath" in internal
    open_counter = internal[
        internal.index("inline int mirrorOpenIssueCount")
        : internal.index("inline int mirrorIssueCount")
    ]
    # Reads each record's status; anything not "closed" counts as open (matching
    # RepoHost::countOpenIssues and the web's parseIssueJson default).
    assert 'issue-%1.json' in open_counter
    assert 'value(QStringLiteral("status"))' in open_counter
    assert 'if (status != QLatin1String("closed"))' in open_counter
    # The advertised issueCount is the open count, not the raw directory tally.
    issue_count = internal[
        internal.index("inline int mirrorIssueCount")
        : internal.index("inline int mirrorIssueCount") + 260
    ]
    assert "return mirrorOpenIssueCount(mirrorPath, branch);" in issue_count
    assert 'mirrorNumberedDirCount(mirrorPath, branch, QStringLiteral(".forkmesh/issues"))' not in issue_count


def test_homepage_links_to_active_nodes():
    # The homepage surfaces a link to the live network/active-nodes page.
    assert 'href="/network"' in INDEX
    assert "View active nodes" in INDEX


def test_dashboard_js_batches_record_reads_and_lazy_loads_tabs():
    # Record lists (issues/pulls/discussions) must fetch their record files
    # through ONE batched /blobs request - fetching each record as its own
    # /blob call fired 50+ parallel requests per page view and tripped the
    # relay's per-repo rate limit - and must only load on the tab's FIRST
    # view, not eagerly on every repo open.
    assert "async function fetchRepoBlobs(repo, paths, options = {})" in DASHBOARD_JS
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
    # Issues and commits are lazy like pulls/discussions: repo open loads the
    # visible code view plus mirror summary; hidden tab data waits for the first
    # tab view.
    panels = DASHBOARD_JS[
        DASHBOARD_JS.index("function loadRepoFeaturePanels")
        : DASHBOARD_JS.index("function updateRepoLiveCounts")
    ]
    assert "state.loadedRepoTabs = {};" in panels
    assert '["commits", "issues", "projects", "pulls", "discussions", "releases", "insights", "agents"].includes(tab)' in DASHBOARD_JS
    # The tab badge still fills immediately from the root tree's served counts,
    # badging the OPEN issue count (issue #397) rather than the open+closed total.
    assert 'setRepoTabCount("issues", openIssues);' in DASHBOARD_JS


# --- Free-text issue search (adjacent to the Open/Closed/All filter) --------
# The Issues panel already had a GitHub-style search box (data-repo-filter-
# query="issues") sitting there decoratively with no listener behind it.
# These tests pin it now actually filtering the already-loaded issue list.


def test_issues_view_state_carries_a_search_query():
    assert 'issuesView: { filter: "open", items: [], query: "" }' in DASHBOARD_JS


def test_issue_search_matches_number_title_body_author_and_meta():
    render = DASHBOARD_JS[
        DASHBOARD_JS.index("function issueMatchesQuery")
        : DASHBOARD_JS.index("function renderRepoIssues")
    ]
    for marker in (
        'if (!q) return true;',
        '"#" + issue.number, String(issue.number), issue.title, issue.body,',
        'issue.author, issue.meta,',
        '.join(" ").toLowerCase();',
        'return haystack.includes(q);',
    ):
        assert marker in render


def test_render_repo_issues_applies_the_search_filter_after_the_status_filter():
    render = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoIssues")
        : DASHBOARD_JS.index("function setIssueFilter")
    ]
    # The three pinned status-filter lines stay exactly as before...
    assert 'if (issuesView.filter === "all") return true;' in render
    assert 'if (issuesView.filter === "open") return issue.status === "open";' in render
    assert 'return issue.status !== "open";' in render
    # ...with the search predicate layered on afterward, not replacing them.
    assert '}).filter((issue) => issueMatchesQuery(issue, issuesView.query));' in render


def test_issue_search_box_is_wired_to_a_delegated_input_listener():
    assert 'event.target?.matches?.(\'[data-repo-filter-query="issues"]\')' in DASHBOARD_JS
    listener = DASHBOARD_JS[
        DASHBOARD_JS.index(
            'event.target?.matches?.(\'[data-repo-filter-query="issues"]\')')
        : DASHBOARD_JS.index(
            'event.target?.matches?.("[data-repo-branch-search]")')
    ]
    assert "state.issuesView.query = event.target.value || \"\";" in listener
    assert "renderRepoIssues();" in listener


def test_issue_search_box_value_reflects_live_state_pulls_search_stays_static():
    assert (
        'value="${isPulls ? "is:pr is:open" : escapeHtml(state.issuesView.query || "")}"'
        in DASHBOARD_JS
    )


def test_opening_a_different_repo_resets_any_leftover_search_query():
    # The search box is rendered immediately when a repo opens (via
    # renderRepoCollectionPanel), before the Issues tab's own lazy load would
    # otherwise reset the query — so the reset has to happen here too, or a
    # freshly-opened repo's search box would show the PREVIOUS repo's text.
    render = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoDetail")
        : DASHBOARD_JS.index("navigateHistory(detailPath)")
    ]
    assert 'state.issuesView = { filter: "open", items: [], query: "" };' in render
