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


DASHBOARD = assembled_dashboard()


DASHBOARD_JS = assembled_dashboard_js()
STYLES = (PUBLIC / "styles.css").read_text(encoding="utf-8")
INDEX = (PUBLIC / "index.html").read_text(encoding="utf-8")


def test_dashboard_has_open_closed_all_filter_defaulting_to_open():
    assert 'class="issue-filter"' in DASHBOARD
    for state in ("open", "closed", "all"):
        assert f'data-state="{state}"' in DASHBOARD

    open_btn = DASHBOARD[
        DASHBOARD.index('id="issue-filter-open"') : DASHBOARD.index('id="issue-filter-closed"')
    ]
    assert "is-active" in open_btn
    assert 'aria-pressed="true"' in open_btn


def test_styles_define_issue_filter_control():
    assert ".issue-filter" in STYLES
    assert ".issue-filter button.is-active" in STYLES
    assert ".panel-header-aside" in STYLES







def test_dashboard_js_defaults_issue_filter_to_open():

    assert 'filter: "open"' in DASHBOARD_JS


    assert '["open", "closed", "all"].map((stateName)' in DASHBOARD_JS
    assert 'data-dashboard-issue-filter="${stateName}"' in DASHBOARD_JS
    assert 'stateName === "open" ? "true" : "false"' in DASHBOARD_JS


def test_dashboard_js_filters_issue_list_by_state():
    render = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoIssues")
        : DASHBOARD_JS.index("function setIssueFilter")
    ]
    assert 'if (issuesView.filter === "all") return true;' in render


    assert 'if (issuesView.filter === "open") return issue.status !== "closed";' in render
    assert 'return issue.status === "closed";' in render


def test_dashboard_js_loads_issues_from_git_tree_and_counts_open():
    load = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoIssues")
        : DASHBOARD_JS.index("async function loadRepoCollection")
    ]

    assert 'repoLiveUrl(repo, "tree", { path: ".forkmesh/issues" })' in load
    assert "issueJsonPath(Number(entry.name))" in load



    assert '.forkmesh/issues/${statusDir}' in load
    assert "issueJsonPath(Number(entry.name), statusDir)" in load
    assert "parseIssueJson(blobText(blob), number)" in load


    assert 'issuesView.filter = "open";' in load




    assert 'issue.status !== "closed" && !issue.deleted).length' in load
    assert 'setRepoTabCount("issues", openIssues);' in load
    assert "renderRepoIssues();" in load


def test_dashboard_js_open_load_pages_only_open_and_defers_closed():




    load = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoIssues")
        : DASHBOARD_JS.index("async function fetchIssuePage")
    ]


    for marker in ("const openPaths = new Map();",
                   "const closedPaths = new Map();",
                   "const legacyPaths = new Map();"):
        assert marker in load

    assert "const pathByNumber = new Map([...legacyPaths, ...openPaths]);" in load


    assert "state.issuesView.closedPaths = closedPaths;" in load
    assert "state.issuesView.closedLoaded = closedPaths.size === 0;" in load


def test_dashboard_js_issue_counts_come_from_folder_listing_not_the_page():





    load = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoIssues")
        : DASHBOARD_JS.index("async function fetchIssuePage")
    ]
    assert "const openIssues = openPaths.size + legacyOpen;" in load
    assert "const closedIssues = closedPaths.size + legacyClosed;" in load
    assert 'setRepoCollectionCounts("issues", openIssues, closedIssues);' in load


def test_dashboard_js_lazy_loads_closed_issues_on_filter_switch():



    setter = DASHBOARD_JS[
        DASHBOARD_JS.index("function setIssueFilter")
        : DASHBOARD_JS.index("async function loadRepoIssues")
    ]
    assert '(filter === "closed" || filter === "all")' in setter
    assert "!state.issuesView.closedLoaded && !state.issuesView.closedLoading" in setter
    assert "loadClosedIssues();" in setter
    closed = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadClosedIssues")
        : DASHBOARD_JS.index("async function loadRepoCollection")
    ]
    assert "if (view.closedLoaded || view.closedLoading) return;" in closed
    assert "fetchIssuePage(" in closed
    assert "view.closedLoaded = true;" in closed


def test_dashboard_js_shared_issue_page_fetcher_batches_and_retries():



    page = DASHBOARD_JS[
        DASHBOARD_JS.index("async function fetchIssuePage")
        : DASHBOARD_JS.index("async function loadClosedIssues")
    ]
    assert "fetchRepoBlobs(" in page
    assert "missing = stillMissing;" in page
    assert "missing.forEach((number) => items.push(placeholderIssue(number)));" in page
    assert 'repoLiveUrl(repo, "blob"' not in page


def test_dashboard_js_shows_per_tab_counts():


    assert 'data-dashboard-repo-tab-count="${tab}"' in DASHBOARD_JS
    assert "function applyServedCounts(counts)" in DASHBOARD_JS
    assert 'setRepoTabCount("issues", openIssues);' in DASHBOARD_JS
    assert 'setRepoTabCount("pulls", Number(counts.pulls));' in DASHBOARD_JS
    assert 'setRepoTabCount("discussions", Number(counts.discussions));' in DASHBOARD_JS
    assert 'setRepoTabCount("mirrors", mirrors.length);' in DASHBOARD_JS


def test_dashboard_js_issue_header_counts_open_not_total():





    served = DASHBOARD_JS[
        DASHBOARD_JS.index("function applyServedCounts(counts)")
        : DASHBOARD_JS.index("function issueMatchesQuery")
    ]
    assert "Number.isFinite(Number(counts.openIssues))" in served
    assert "? Number(counts.openIssues)" in served
    assert ": Number(counts.issues);" in served
    assert 'setRepoTabCount("issues", openIssues);' in served
    assert 'setRepoCollectionCounts("issues", openIssues, Number(counts.closedIssues));' in served


def test_advertised_catalog_issue_count_is_open_only():






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


    assert 'issue-%1.json' in open_counter
    assert 'value(QStringLiteral("status"))' in open_counter
    assert 'if (status != QLatin1String("closed"))' in open_counter

    issue_count = internal[
        internal.index("inline int mirrorIssueCount")
        : internal.index("inline int mirrorIssueCount") + 260
    ]
    assert "return mirrorOpenIssueCount(mirrorPath, branch);" in issue_count
    assert 'mirrorNumberedDirCount(mirrorPath, branch, QStringLiteral(".forkmesh/issues"))' not in issue_count


def test_dashboard_js_surfaces_unreadable_issues_instead_of_dropping_them():





    load = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoIssues")
        : DASHBOARD_JS.index("async function loadRepoCollection")
    ]

    assert "else missing.push(number);" in load

    assert "missing.map((number) => pathByNumber.get(number))" in load
    assert "missing = stillMissing;" in load

    assert "missing.forEach((number) => items.push(placeholderIssue(number)));" in load

    assert "state.issuesView.missing = missing;" in load
    assert "state.issuesView.truncated = Math.max(0, numbered.length - dirs.length);" in load


def test_dashboard_js_placeholder_issue_counts_as_open_and_flags_load_failure():
    placeholder = DASHBOARD_JS[
        DASHBOARD_JS.index("function placeholderIssue(number)")
        : DASHBOARD_JS.index("function placeholderIssue(number)") + 500
    ]


    assert 'status: "open",' in placeholder
    assert "loadFailed: true," in placeholder
    assert "couldn't load from mirror" in placeholder


def test_dashboard_js_issue_panel_warns_with_a_retry_button_on_gaps():
    render = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoIssues")
        : DASHBOARD_JS.index("function setIssueFilter")
    ]


    assert "couldn't be read from the live mirror" in render
    assert "aren't shown" in render
    assert "data-repo-issues-reload" in render

    reload = DASHBOARD_JS[
        DASHBOARD_JS.index("[data-repo-issues-reload]")
        : DASHBOARD_JS.index("[data-repo-issues-reload]") + 400
    ]
    assert "loadRepoIssues(state.selectedRepo);" in reload


def test_homepage_links_to_active_nodes():

    assert 'href="/network"' in INDEX
    assert "View active nodes" in INDEX


def test_dashboard_js_batches_record_reads_and_lazy_loads_tabs():





    assert "async function fetchRepoBlobs(repo, paths, options = {})" in DASHBOARD_JS
    assert "/blobs?" in DASHBOARD_JS
    assert 'query.append("path", path)' in DASHBOARD_JS

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



    panels = DASHBOARD_JS[
        DASHBOARD_JS.index("function loadRepoFeaturePanels")
        : DASHBOARD_JS.index("function updateRepoLiveCounts")
    ]
    assert "state.loadedRepoTabs = {};" in panels
    assert '["commits", "issues", "projects", "pulls", "discussions", "releases", "insights", "sizemap", "agents"].includes(tab)' in DASHBOARD_JS


    assert 'setRepoTabCount("issues", openIssues);' in DASHBOARD_JS








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

    assert 'if (issuesView.filter === "all") return true;' in render
    assert 'if (issuesView.filter === "open") return issue.status !== "closed";' in render
    assert 'return issue.status === "closed";' in render

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




    render = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoDetail")
        : DASHBOARD_JS.index("navigateHistory(detailPath)")
    ]
    assert 'state.issuesView = { filter: "open", items: [], query: "" };' in render
