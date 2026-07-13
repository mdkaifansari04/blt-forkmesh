#!/usr/bin/env python3
"""Live dashboard surfaces in-place mirror serving (adhoc #61): a repo whose own
host is down but whose mirror is online must read as available ("served by
mirror"), not "host offline", on both the repo card and the repo detail page."""

from pathlib import Path
import json
import subprocess

from _dashboard_bundle import assembled_dashboard_js


PUBLIC = Path(__file__).resolve().parents[1] / "public"
# dashboard.js is built from ordered public/dashboard/js/*.js fragments before
# deploy (see src/dashboard_bundle.py).
DASHBOARD_JS = assembled_dashboard_js()


def test_dashboard_defines_group_liveness_helpers():
    # The availability verdict comes from the worker's group-liveness flag
    # (cloneOnline), falling back to liveHost for older payloads.
    assert "function repoIsLive(repo)" in DASHBOARD_JS
    assert "function repoServedByMirror(repo)" in DASHBOARD_JS
    assert "repo?.cloneOnline ?? repo?.liveHost" in DASHBOARD_JS


def test_repo_card_uses_group_liveness_not_raw_livehost():
    card = DASHBOARD_JS[
        DASHBOARD_JS.index("function repositoryCard(group)")
        : DASHBOARD_JS.index("function updateRepositoryPagination(")
    ]
    assert "const live = repoIsLive(origin);" in card
    assert "const viaMirror = repoServedByMirror(origin);" in card
    # A repo served by a live mirror reads as "via mirror" / "served by mirror",
    # never a flat "offline".
    assert "viaMirror ? \"via mirror\"" in card
    assert "viaMirror ? \"served by mirror\"" in card


def test_repo_card_groups_mirrors_under_source_of_truth():
    # The list shows one card per logical repo (grouped by root commit, name
    # fallback), named after the source of truth (the node that published from a
    # local working copy, source === "local-node"), not each mirror separately.
    assert "function groupRepositories(" in DASHBOARD_JS
    source_of_truth = DASHBOARD_JS[
        DASHBOARD_JS.index("function sourceOfTruth(group)")
        : DASHBOARD_JS.index("function sourceOfTruth(group)") + 500
    ]
    assert '(m.source || "").trim() === "local-node"' in source_of_truth
    assert "|| group.primary" in source_of_truth

    card = DASHBOARD_JS[
        DASHBOARD_JS.index("function repositoryCard(group)")
        : DASHBOARD_JS.index("function updateRepositoryPagination(")
    ]
    # Title, clone command, and Browse target all name the source of truth.
    assert "const origin = sourceOfTruth(group);" in card
    assert "const key = repoKey(origin);" in card
    assert 'escapeHtml(origin.owner || "owner")' in card
    assert 'escapeHtml(origin.name || "repository")' in card
    assert "cloneUrl(origin)" in card
    # A grouped repo advertises how many nodes mirror it.
    assert "const nodeCount = group.members.length;" in card
    assert "${nodeCount} nodes" in card


def test_remote_clone_only_group_uses_clone_url_canonical_identity():
    # Exercise the actual assembled browser helpers: when the only rows are
    # mirror-owned remote clones, the card/detail URL identity is inferred from
    # cloneUrl while the mirror rows remain aliases for direct route lookup.
    transformed = DASHBOARD_JS.replace(
        "  applyDashboardTheme(readDashboardTheme());\n"
        "  renderLongDiffPreference();\n"
        "  if (initSharedChrome()) {\n"
        "    (PAGE_INITS[currentPage()] || initHomePage)();\n"
        "    initPageHistory();\n"
        "  }",
        """  globalThis.__dashboardExports = {
    state,
    groupRepositories,
    sourceOfTruth,
    findRepository,
    repositoryCard,
    cloneUrl,
    repoPathUrl,
  };""",
    )
    script = "const SOURCE = " + json.dumps(transformed) + ";\n" + """
const assert = require("assert");
const vm = require("vm");
global.location = { origin: "https://forkmesh.test", pathname: "/dashboard", search: "" };
global.localStorage = { getItem() { return null; }, setItem() {}, removeItem() {} };
const emptyClassList = { add() {}, remove() {}, toggle() {}, contains() { return false; } };
global.document = {
  body: { classList: emptyClassList },
  documentElement: { dataset: {}, style: {} },
  cookie: "",
  querySelector() { return null; },
  querySelectorAll() { return []; },
  addEventListener() {},
};
global.window = {
  matchMedia() { return { addEventListener() {}, addListener() {} }; },
  addEventListener() {},
  history: { pushState() {} },
  lucide: { createIcons() {} },
  setTimeout() {},
  clearInterval() {},
};
global.navigator = {};
vm.runInThisContext(SOURCE);
const {
  state,
  groupRepositories,
  sourceOfTruth,
  findRepository,
  repositoryCard,
  cloneUrl,
  repoPathUrl,
} = global.__dashboardExports;
const repos = [
  {
    owner: "mirror3",
    name: "forkmesh",
    source: "remote-clone",
    cloneUrl: "https://forkmesh.com/mainnode/forkmesh",
    rootCommit: "abc",
    liveHost: true,
    cloneOnline: true,
    lastSync: 2000,
  },
  {
    owner: "mirror4",
    name: "forkmesh",
    source: "remote-clone",
    cloneUrl: "https://forkmesh.com/mainnode/forkmesh.git",
    rootCommit: "abc",
    liveHost: true,
    cloneOnline: true,
    lastSync: 1000,
  },
];
state.repositories = repos;
const group = groupRepositories(repos)[0];
const origin = sourceOfTruth(group);
assert.equal(origin.owner, "mainnode");
assert.equal(origin.name, "forkmesh");
assert.equal(origin.liveHost, false);
assert.equal(origin.cloneOnline, true);
assert.equal(cloneUrl(origin), "https://forkmesh.test/mainnode/forkmesh");
assert.equal(repoPathUrl(origin), "/mainnode/forkmesh");
assert.equal(findRepository("mainnode/forkmesh").owner, "mainnode");
assert.equal(findRepository("mirror3/forkmesh").owner, "mainnode");
const card = repositoryCard(group);
assert(card.includes("mainnode/"));
assert(!card.includes("mirror3/</span>forkmesh"));
"""
    result = subprocess.run(
        ["node"],
        input=script,
        check=True,
        capture_output=True,
        text=True,
    )
    assert result.stderr == ""


def test_repo_list_paginates_grouped_repos_not_raw_mirrors():
    pager = DASHBOARD_JS[
        DASHBOARD_JS.index("function updateRepositoryPagination(")
        : DASHBOARD_JS.index("function updateRepositoryPagination(") + 800
    ]
    # Pagination and the count summary operate on grouped logical repos.
    assert "const total = state.filteredGroups.length;" in pager
    assert "const visible = state.filteredGroups.slice(start, end);" in pager


def test_repo_detail_reflects_mirror_serving():
    detail = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderRepoDetail(repo)")
        : DASHBOARD_JS.index("function findRepository(")
    ]
    assert "const live = repoIsLive(repo);" in detail
    assert "const viaMirror = repoServedByMirror(repo);" in detail
    # The title-row badge keys off the group verdict. (The About rail's
    # Clone row and the header Data/Host chips were removed in the metadata
    # cleanup — this badge is the remaining availability surface.)
    assert "viaMirror ? \"served by mirror\" : live ? \"host online\" : \"host offline\"" in detail


def test_served_by_badge_includes_serving_node_counters():
    served_by = DASHBOARD_JS[
        DASHBOARD_JS.index("function servedMirrorStats(name)")
        : DASHBOARD_JS.index("function repoExplorerRowClass(")
    ]
    assert "mirror.clonesServed" in served_by
    assert "mirror.websiteServed" in served_by
    assert "${formatCount(clones)} clones" in served_by
    assert "${formatCount(website)} website requests" in served_by
    assert "`served by ${name}`, speed, servedMirrorStats(name)" in served_by

    mirrors = DASHBOARD_JS[
        DASHBOARD_JS.index("async function loadRepoMirrors(repo)")
        : DASHBOARD_JS.index("function renderRepoRelease(")
    ]
    # The tree/blob request may set servedBy before /mirrors has loaded; after
    # counters arrive, refresh the badge so it gains clone and website counts.
    assert "renderRepoServedBy(state.repoServedBy.name, state.repoServedBy.tookMs)" in mirrors


def test_live_mirror_rows_show_node_version_under_name():
    rows = DASHBOARD_JS[
        DASHBOARD_JS.index("function renderMirrorRow(mirror, servedBy)")
        : DASHBOARD_JS.index("function renderRepoLiveMirrorList(")
    ]
    assert "mirror.version || mirror.appVersion || mirror.clientVersion" in rows
    assert 'rawVersion[0].toLowerCase() === "v"' in rows
    assert "block min-w-0 truncate text-[10px] text-muted-foreground font-mono" in rows


def test_live_mirror_browse_reads_bypass_the_cached_fetchjson():
    # Everything the code tab (and commits/insights) reads over the live tunnel
    # must be fetched FRESH from whichever mirror the router round-robins to,
    # exactly like the issue/pull/discussion/release/README readers already do.
    # The cached, account-scoped fetchJson (cache:"default", inflight dedup,
    # bearer token) could otherwise pin the page to a stale copy from when the
    # source-of-truth host was online, or a different mirror answered — so a repo
    # served entirely by its mirrors would render stale/among rotating nodes.
    # fetchRepoJson is the no-store live reader; assert the browse surfaces use it
    # and no live-tunnel path is left on fetchJson.
    fresh_reads = (
        'fetchRepoJson(repoLiveUrl(repo, "tree"',
        'fetchRepoJson(repoLiveUrl(repo, "blob"',
        'fetchRepoJson(repoLiveUrl(repo, "history"))',
        'fetchRepoJson(repoLiveUrl(repo, "commit"',
        "fetchRepoJson(`${repoApiBase(repo)}/branches`)",
    )
    for read in fresh_reads:
        assert read in DASHBOARD_JS, "missing fresh live read: %s" % read
    # No live-tunnel browse read may fall back to the cached fetchJson. The only
    # remaining fetchJson repo calls are relay/catalog endpoints (/about,
    # /mirrors, /releases/downloads, /api/repositories), not host-tunnel reads.
    assert "fetchJson(repoLiveUrl(" not in DASHBOARD_JS
    assert "fetchJson(`${repoApiBase(repo)}/branches`)" not in DASHBOARD_JS
