// The header search box filters the page you are on, live, on top of the
// repository dropdown it already showed (adhoc #37). Source-text tests pin the
// wiring; this drives the real page to prove a keystroke actually narrows the
// rendered list and that clearing the box puts every row back.
const { test, expect } = require("@playwright/test");
const path = require("node:path");

const REPOS_PAGE_PATH = path.resolve(
  __dirname,
  "..",
  "..",
  "public",
  "dashboard",
  "repos",
  "index.html",
);

const REPO_PAGE_PATH = path.resolve(
  __dirname,
  "..",
  "..",
  "public",
  "dashboard",
  "repo.html",
);

const REPOSITORIES = [
  { owner: "alpha", name: "engine", description: "Physics core" },
  { owner: "beta", name: "atlas", description: "Map tiles" },
  { owner: "gamma", name: "beacon", description: "Signal relay" },
].map((repo, index) => ({
  ...repo,
  visibility: "public",
  source: "remote-clone",
  cloneUrl: `https://forkmesh.test/${repo.owner}/${repo.name}`,
  rootCommit: `root-${index}`,
  stateHash: `state-${index}`,
  updatedAt: String(index + 1),
}));

async function stubDashboard(page) {
  await page.route("https://cdn.tailwindcss.com/**", (route) =>
    route.fulfill({
      contentType: "text/javascript",
      body: "window.tailwind = {};",
    }),
  );
  await page.route(
    "https://cdn.jsdelivr.net/npm/lucide@0.468.0/dist/umd/lucide.min.js",
    (route) =>
      route.fulfill({
        contentType: "text/javascript",
        body: "window.lucide = { createIcons() {} };",
      }),
  );
  await page.route("**/dashboard/repos", (route) =>
    route.fulfill({
      path: REPOS_PAGE_PATH,
      contentType: "text/html; charset=utf-8",
    }),
  );
  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    const body = url.pathname === "/api/repositories"
      ? { ok: true, repositories: REPOSITORIES }
      : { ok: true };
    return route.fulfill({
      status: 200,
      contentType: "application/json; charset=utf-8",
      body: JSON.stringify(body),
    });
  });
}

test("header search filters the repositories on the page as you type", async ({
  page,
}) => {
  await stubDashboard(page);
  await page.goto("/dashboard/repos");

  const cards = page.locator("#repoList .repo-card");
  await expect(cards).toHaveCount(REPOSITORIES.length);

  const search = page.locator("[data-global-search]");
  await search.fill("atlas");

  // The page itself narrows - not just the dropdown.
  await expect(cards).toHaveCount(1);
  await expect(cards.first()).toContainText("atlas");
  await expect(page.locator("[data-repo-summary]")).toContainText("of 1");

  // ...and the dropdown it always showed is still there, with the footer
  // saying the page behind it was filtered too.
  const results = page.locator("[data-global-search-result]");
  await expect(results).toHaveCount(1);
  await expect(page.locator("[data-global-search-page-filter]")).toContainText(
    "1 match",
  );

  // A term that matches the owner rather than the repo name still filters,
  // since the cards are labelled with the owner.
  await search.fill("gamma");
  await expect(cards).toHaveCount(1);
  await expect(cards.first()).toContainText("beacon");

  // Clearing the box restores every row and retires the footer.
  await search.fill("");
  await expect(cards).toHaveCount(REPOSITORIES.length);
  await expect(page.locator("[data-global-search-page-filter]")).toBeHidden();
});

test("header search filters the repo page's file listing in place", async ({
  page,
}) => {
  const treeRequests = [];
  await page.route("https://cdn.tailwindcss.com/**", (route) =>
    route.fulfill({
      contentType: "text/javascript",
      body: "window.tailwind = {};",
    }),
  );
  await page.route(
    "https://cdn.jsdelivr.net/npm/lucide@0.468.0/dist/umd/lucide.min.js",
    (route) =>
      route.fulfill({
        contentType: "text/javascript",
        body: "window.lucide = { createIcons() {} };",
      }),
  );
  await page.route("**/alpha/engine", (route) =>
    route.fulfill({
      path: REPO_PAGE_PATH,
      contentType: "text/html; charset=utf-8",
    }),
  );
  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let body = { ok: true };
    if (url.pathname === "/api/repositories") {
      body = { ok: true, repositories: REPOSITORIES };
    } else if (url.pathname === "/api/repo/alpha/engine/tree") {
      treeRequests.push(url.pathname);
      body = {
        ok: true,
        commit: "0123456789abcdef",
        entries: [
          { name: "README.md", path: "README.md", type: "blob", message: "Docs" },
          { name: "engine.cpp", path: "engine.cpp", type: "blob", message: "Core" },
          { name: "physics", path: "physics", type: "tree", message: "Solver" },
        ],
        counts: {},
      };
    } else if (url.pathname === "/api/repo/alpha/engine/mirrors") {
      body = {
        ok: true,
        mirrors: [{ node: "alpha", status: "online", cloneAvailable: true }],
        summary: { mirrors: 1 },
      };
    }
    return route.fulfill({
      status: 200,
      contentType: "application/json; charset=utf-8",
      body: JSON.stringify(body),
    });
  });

  await page.goto("/alpha/engine");

  const rows = page.locator(
    "[data-repo-tree] [data-dashboard-tree-path], [data-repo-tree] [data-dashboard-blob-path]",
  );
  await expect(rows).toHaveCount(3);
  const treeReadsBeforeSearch = treeRequests.length;

  await page.locator("[data-global-search]").fill("physics");
  await expect(rows).toHaveCount(1);
  await expect(rows.first()).toContainText("physics");

  // Filtering re-renders the listing already fetched - a keystroke must not
  // turn into another mirror read.
  expect(treeRequests.length).toBe(treeReadsBeforeSearch);

  await page.locator("[data-global-search]").fill("");
  await expect(rows).toHaveCount(3);
});

test("header search filters the network page's connected nodes", async ({
  page,
}) => {
  await page.route("https://cdn.tailwindcss.com/**", (route) =>
    route.fulfill({
      contentType: "text/javascript",
      body: "window.tailwind = {};",
    }),
  );
  await page.route(
    "https://cdn.jsdelivr.net/npm/lucide@0.468.0/dist/umd/lucide.min.js",
    (route) =>
      route.fulfill({
        contentType: "text/javascript",
        body: "window.lucide = { createIcons() {} };",
      }),
  );
  await page.route("**/dashboard/network", (route) =>
    route.fulfill({
      path: path.resolve(
        __dirname, "..", "..", "public", "dashboard", "network", "index.html",
      ),
      contentType: "text/html; charset=utf-8",
    }),
  );
  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let body = { ok: true };
    if (url.pathname === "/api/repositories") {
      body = { ok: true, repositories: REPOSITORIES };
    } else if (url.pathname === "/api/network/overview") {
      body = {
        ok: true,
        stats: {
          hosts: 3,
          repos: 3,
          clients: 1,
          onlineNodes: ["mirror6", "mirror7", "workstation"],
        },
        leaderboards: {
          uptime: [
            { name: "mirror6", minutes: 300 },
            { name: "mirror7", minutes: 200 },
            { name: "workstation", minutes: 100 },
          ],
          nodes: [],
        },
        history: { hours: [] },
      };
    }
    return route.fulfill({
      status: 200,
      contentType: "application/json; charset=utf-8",
      body: JSON.stringify(body),
    });
  });

  await page.goto("/dashboard/network");

  const nodeList = page.locator("[data-network-node-list]");
  await expect(nodeList).toContainText("mirror6");
  await expect(nodeList).toContainText("workstation");

  await page.locator("[data-global-search]").fill("workstation");
  await expect(nodeList).toContainText("workstation");
  await expect(nodeList).not.toContainText("mirror6");

  await page.locator("[data-global-search]").fill("");
  await expect(nodeList).toContainText("mirror6");
});

test("a header search with no page matches empties the list without breaking it", async ({
  page,
}) => {
  await stubDashboard(page);
  await page.goto("/dashboard/repos");

  const cards = page.locator("#repoList .repo-card");
  await expect(cards).toHaveCount(REPOSITORIES.length);

  await page.locator("[data-global-search]").fill("no-such-repository");
  await expect(cards).toHaveCount(0);
  await expect(page.locator("[data-repo-summary]")).toContainText(
    "No repositories match this filter",
  );

  await page.locator("[data-global-search]").fill("engine");
  await expect(cards).toHaveCount(1);
});
