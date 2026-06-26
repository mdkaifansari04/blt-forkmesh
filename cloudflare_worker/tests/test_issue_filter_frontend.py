#!/usr/bin/env python3
"""Issues tab defaults to the Open view (issue #270).

The repo Issues tab used to list every issue regardless of state. These contract
tests pin the Open/Closed/All filter: Open is the default, closed issues are
opt-in, and the tab badge counts open issues.
"""

from pathlib import Path


PUBLIC = Path(__file__).resolve().parents[1] / "public"
INDEX = (PUBLIC / "index.html").read_text(encoding="utf-8")
CATALOG = (PUBLIC / "catalog.js").read_text(encoding="utf-8")
STYLES = (PUBLIC / "styles.css").read_text(encoding="utf-8")


def test_index_has_open_closed_all_filter_defaulting_to_open():
    assert 'class="issue-filter"' in INDEX
    for state in ("open", "closed", "all"):
        assert f'data-state="{state}"' in INDEX
    # The Open button is the active default.
    open_btn = INDEX[
        INDEX.index('id="issue-filter-open"') : INDEX.index('id="issue-filter-closed"')
    ]
    assert "is-active" in open_btn
    assert 'aria-pressed="true"' in open_btn


def test_catalog_defaults_issue_filter_to_open():
    assert 'let issueFilter = "open";' in CATALOG


def test_catalog_render_issue_list_filters_by_state():
    render = CATALOG[
        CATALOG.index("function renderIssueList")
        : CATALOG.index("async function loadIssues")
    ]
    # Open view shows only open; closed view shows everything that isn't open.
    assert 'if (issueFilter === "all") return true;' in render
    assert 'if (issueFilter === "open") return i.status === "open";' in render
    assert 'return i.status !== "open";' in render


def test_catalog_load_issues_renders_through_filter_and_counts_open():
    load = CATALOG[
        CATALOG.index("async function loadIssues")
        : CATALOG.index("function pullRow")
    ]
    assert "loadedIssues = issues;" in load
    assert 'const openCount = issues.filter((i) => i.status === "open").length;' in load
    assert "tabIssuesCountEl.textContent = String(openCount);" in load
    assert "renderIssueList();" in load
    # The unfiltered render of every issue is gone.
    assert "...issues.map((i) => issueRow" not in load


def test_styles_define_issue_filter_control():
    assert ".issue-filter" in STYLES
    assert ".issue-filter button.is-active" in STYLES
    assert ".panel-header-aside" in STYLES
