#!/usr/bin/env python3
"""Repository lists are labelled with the logical owner (adhoc #94).

Bytes are published by nodes, so the catalog route for a mirrored repo reads
``mirror8/forkmesh``. The catalog already resolves each route to the user or
organization that owns the work (``ownerKind`` / ``logicalOwner`` /
``logicalOwners``); the dashboard must show that identity — ``forkmesh/forkmesh``
— while every routing key stays physical so lookups keep matching catalog rows.

The same cards show the repository's own root logo, which is served by a mirror
and can therefore fail after the card rendered: the image must fall back to the
generated artwork and then to the icon, never to a broken image.
"""

import json
import subprocess

from _dashboard_bundle import assembled_dashboard_js


DASHBOARD_JS = assembled_dashboard_js()
BOOT = (
    "  applyDashboardTheme(readDashboardTheme());\n"
    "  if (initSharedChrome()) {\n"
    "    startAccountSessionWatch();\n"
    "    (PAGE_INITS[currentPage()] || initHomePage)();\n"
    "    initPageHistory();\n"
    "  }"
)
BROWSER_STUBS = """
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
  setInterval() {},
  clearInterval() {},
};
global.navigator = {};
vm.runInThisContext(SOURCE);
"""


def _run(exports, body):
    transformed = DASHBOARD_JS.replace(
        BOOT, "  globalThis.__dashboardExports = { " + exports + " };")
    assert transformed != DASHBOARD_JS, "boot anchor drifted"
    script = (
        "const SOURCE = " + json.dumps(transformed) + ";\n"
        + BROWSER_STUBS + body
    )
    result = subprocess.run(
        ["node"], input=script, check=True, capture_output=True, text=True)
    assert result.stderr == ""
    return result.stdout


def test_card_titles_prefer_the_organization_over_the_serving_node():
    output = _run(
        "state, groupRepositories, repositoryCard, groupDisplayOwner",
        """
const { state, groupRepositories, repositoryCard, groupDisplayOwner } =
  global.__dashboardExports;
const repos = [
  {
    owner: "mirror8",
    name: "forkmesh",
    source: "local-node",
    rootCommit: "abc",
    liveHost: true,
    lastSync: 3000,
    ownerKind: "user",
    logicalOwner: "lucianoms",
    logicalOwners: [
      { kind: "user", owner: "lucianoms" },
      { kind: "organization", owner: "forkmesh" },
    ],
  },
  {
    owner: "mirror9",
    name: "forkmesh",
    source: "remote-clone",
    cloneUrl: "https://forkmesh.com/mirror8/forkmesh",
    rootCommit: "abc",
    lastSync: 1000,
  },
];
state.repositories = repos;
const group = groupRepositories(repos)[0];
assert.equal(groupDisplayOwner(group), "forkmesh");
const card = repositoryCard(group);
// The visible title is the organization; the node name never appears as the
// owner segment.
assert(card.includes(">forkmesh/</span>"));
assert(!card.includes(">mirror8/</span>"));
// Routing keys stay physical so findRepository/catalog lookups keep working.
assert(card.includes('data-dashboard-open-repo="mirror8/forkmesh"'));
console.log("ok");
""",
    )
    assert output.strip() == "ok"


def test_display_owner_falls_back_to_the_account_then_the_node():
    output = _run(
        "repoDisplayOwner, repoDisplayKey",
        """
const { repoDisplayOwner, repoDisplayKey } = global.__dashboardExports;
// No organization alias: the owning account still beats the machine name.
assert.equal(
  repoDisplayOwner({
    owner: "mirror8",
    name: "forkmesh",
    ownerKind: "user",
    logicalOwner: "lucianoms",
  }),
  "lucianoms",
);
// An unresolved route (old payload, or a node with no account link) keeps the
// physical owner rather than rendering an empty segment.
assert.equal(repoDisplayOwner({ owner: "mirror8", name: "forkmesh" }), "mirror8");
assert.equal(repoDisplayKey({ owner: "mirror8", name: "forkmesh" }), "mirror8/forkmesh");
// Unknown owner kinds are ignored: only user/organization identities are public.
assert.equal(
  repoDisplayOwner({
    owner: "mirror8",
    name: "forkmesh",
    logicalOwners: [{ kind: "node", owner: "mirror9" }],
  }),
  "mirror8",
);
console.log("ok");
""",
    )
    assert output.strip() == "ok"


def test_repository_links_follow_the_organization_label():





    output = _run(
        "state, groupRepositories, groupLinkUrl, repoLinkUrl,"
        " homeFeedRepositoryCard",
        """
const {
  state,
  groupRepositories,
  groupLinkUrl,
  repoLinkUrl,
  homeFeedRepositoryCard,
} = global.__dashboardExports;
const aliased = [
  {
    owner: "mirror6",
    name: "forkmesh",
    source: "local-node",
    rootCommit: "abc",
    liveHost: true,
    lastSync: 3000,
    ownerKind: "user",
    logicalOwner: "lucianoms",
    logicalOwners: [
      { kind: "user", owner: "lucianoms" },
      { kind: "organization", owner: "forkmesh" },
    ],
  },
];
state.repositories = aliased;
const group = groupRepositories(aliased)[0];
assert.equal(groupLinkUrl(group), "/forkmesh/forkmesh");
assert.equal(groupLinkUrl(group, "blob", "README.md"), "/forkmesh/forkmesh/blob/README.md");
assert.equal(repoLinkUrl(aliased[0]), "/forkmesh/forkmesh");
const card = homeFeedRepositoryCard(group);
assert(card.includes('href="/forkmesh/forkmesh"'));
assert(!card.includes('href="/mirror6/forkmesh"'));
// User identities are labels only — no worker rewrite backs /<user>/<repo>.
const userOwned = [
  {
    owner: "mirror6",
    name: "sidecar",
    source: "local-node",
    rootCommit: "def",
    lastSync: 3000,
    ownerKind: "user",
    logicalOwner: "lucianoms",
    logicalOwners: [{ kind: "user", owner: "lucianoms" }],
  },
];
state.repositories = userOwned;
assert.equal(groupLinkUrl(groupRepositories(userOwned)[0]), "/mirror6/sidecar");
assert.equal(repoLinkUrl(userOwned[0]), "/mirror6/sidecar");
console.log("ok");
""",
    )
    assert output.strip() == "ok"


def test_repository_filter_matches_the_logical_owner():
    matcher = DASHBOARD_JS[
        DASHBOARD_JS.index("function repositoryMatchesQuery(repo, query)")
        : DASHBOARD_JS.index("function repositoryTermsBadge(")
    ]
    assert "repoLogicalOwners(repo).map((value) => value.owner)" in matcher


def test_root_logo_failure_falls_back_instead_of_breaking_the_image():
    hydrate = DASHBOARD_JS[
        DASHBOARD_JS.index("function hydrateNativeRepositoryLogos(root)")
        : DASHBOARD_JS.index("function repoActivitySparkline(")
    ]


    assert "image.onerror" in hydrate
    assert "image.dataset.logoFallbackUsed" in hydrate
    assert "hideNativeRepositoryLogo(image)" in hydrate

    loader = DASHBOARD_JS[
        DASHBOARD_JS.index("function loadNativeRepositoryLogo(endpoint)")
        : DASHBOARD_JS.index("function showNativeRepositoryLogo(")
    ]
    assert "body?.logo?.dataUrl" in loader
    assert "body?.logo?.fallbackDataUrl" in loader

    assert loader.count("nativeRepositoryLogoDataUrl(") == 2
