const { test, expect } = require("@playwright/test");
const path = require("node:path");

const REPO_PAGE_PATH = path.resolve(
  __dirname,
  "..",
  "..",
  "public",
  "dashboard",
  "repo.html",
);
const DASHBOARD_JS_PATH = path.resolve(
  __dirname,
  "..",
  "..",
  "public",
  "dashboard.js",
);
const DASHBOARD_CSS_PATH = path.resolve(
  __dirname,
  "..",
  "..",
  "public",
  "dashboard",
  "tailwind.css",
);

test("organization repository alias resolves its linked mirror catalog group", async ({
  page,
}) => {
  const requestedPaths = [];
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
  await page.route("**/forkmesh/forkmesh", (route) =>
    route.fulfill({
      path: REPO_PAGE_PATH,
      contentType: "text/html; charset=utf-8",
    }),
  );
  await page.route("**/dashboard.js*", (route) =>
    route.fulfill({
      path: DASHBOARD_JS_PATH,
      contentType: "text/javascript; charset=utf-8",
    }),
  );
  await page.route("**/dashboard/tailwind.css", (route) =>
    route.fulfill({
      path: DASHBOARD_CSS_PATH,
      contentType: "text/css; charset=utf-8",
    }),
  );
  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    requestedPaths.push(url.pathname);
    let body = { ok: true };
    if (url.pathname === "/api/repositories") {
      body = {
        ok: true,
        repositories: [
          {
            owner: "mirror3",
            name: "forkmesh",
            visibility: "public",
            source: "remote-clone",
            cloneUrl: "https://forkmesh.test/mirror3/forkmesh",
            description: "ForkMesh keeps source code available across independent hosts.",
            rootCommit: "root",
            stateHash: "state",
            updatedAt: "2",
          },
          {
            owner: "mirror2",
            name: "forkmesh",
            visibility: "public",
            source: "remote-clone",
            cloneUrl: "https://forkmesh.test/mirror2/forkmesh",
            description: "ForkMesh keeps source code available across independent hosts.",
            rootCommit: "root",
            stateHash: "state",
            updatedAt: "1",
          },
        ],
      };
    } else if (url.pathname === "/api/orgs/forkmesh/repos") {
      body = {
        ok: true,
        repos: [{ node: "mirror2", repo: "forkmesh" }],
      };
    } else if (url.pathname === "/api/repo/forkmesh/forkmesh/tree") {
      body = {
        ok: true,
        commit: "0123456789abcdef",
        entries: [
          {
            name: "README.md",
            path: "README.md",
            type: "blob",
            size: 42,
            commit: "0123456789abcdef",
            message: "Document the mesh",
          },
        ],
        counts: { commits: 21418, mirrors: 1 },
      };
    } else if (url.pathname === "/api/repo/forkmesh/forkmesh/mirrors") {
      body = {
        ok: true,
        mirrors: [
          {
            node: "mirror2",
            status: "online",
            cloneAvailable: true,
            version: "0.7.13",
            commit: "0123456789abcdef",
            branch: "main",
            activity: "serving",
            endpoint: "https://mirror2.forkmesh.test",
            endpointIntegrity: "ok",
            checkedAt: 1796158800000,
            lastSync: 1796158800000,
            latencyMs: 24,
            region: "us-east",
            operations: ["clone", "browse", "website"],
            sizeBytes: 507510784,
            issueCount: 92,
            commitCount: 21418,
            branchCount: 69,
            pullCount: 32,
            discussionCount: 2,
            worktreeCount: 3,
            clonesServed: 1126,
            websiteServed: 29829,
            artifactCount: 6,
          },
          {
            node: "mirror3",
            status: "offline",
            cloneAvailable: false,
            version: "0.7.13",
            commit: "0123456789abcdef",
            branch: "main",
            lastSync: 1796072400000,
            issueCount: 92,
            commitCount: 21418,
            branchCount: 69,
            pullCount: 32,
            discussionCount: 2,
            artifactCount: 6,
          },
          {
            node: "edge-mirror-with-a-long-machine-name",
            status: "online",
            cloneAvailable: true,
            version: "0.7.8",
            commit: "fedcba9876543210",
            branch: "main",
            activity: "syncing",
            endpoint: "https://edge-mirror-with-a-long-machine-name.example.test",
            endpointIntegrity: "unknown",
            checkedAt: 1796158740000,
            lastSync: 1796158680000,
            latencyMs: 195,
            region: "us-west",
            operations: ["clone", "browse"],
            sizeBytes: 506252288,
            issueCount: 92,
            commitCount: 21417,
            branchCount: 68,
            pullCount: 31,
            discussionCount: 2,
            worktreeCount: 2,
            clonesServed: 188,
            websiteServed: 4629,
            artifactCount: 0,
          },
        ],
        summary: { mirrors: 3 },
      };
    }
    return route.fulfill({
      status: 200,
      contentType: "application/json; charset=utf-8",
      body: JSON.stringify(body),
    });
  });

  await page.goto("/forkmesh/forkmesh");

  await expect(
    page.getByRole("heading", { name: "forkmesh/forkmesh", exact: true }),
  ).toBeVisible();
  await expect(page.locator("[data-repo-header-visibility]")).toHaveText("public");
  await expect(page.locator("[data-repo-tree]")).toContainText("README.md");
  await expect(page.locator("[data-repo-availability-status]")).toHaveText(
    "served by mirror",
  );
  await expect(page.locator("[data-repo-github-header]")).not.toContainText(
    "ForkMesh keeps source code available",
  );
  await expect(page.locator("[data-repo-about-description]")).toContainText(
    "ForkMesh keeps source code available",
  );
  await expect(
    page.locator('[role="tab"][data-dashboard-repo-tab="mirrors"]'),
  ).toHaveCount(0);
  const aboutActions = page.locator("[data-repo-about-actions]");
  await expect(aboutActions).toBeVisible();
  for (const label of ["Watch", "Fork", "Star", "Mirrors"]) {
    await expect(aboutActions.getByText(label, { exact: true })).toBeVisible();
  }
  await expect(page.locator("[data-dashboard-history-button]")).toHaveAttribute(
    "href",
    "/forkmesh/forkmesh/commits",
  );
  await expect(
    page.locator('[data-dashboard-history-button] [data-dashboard-repo-count="commits"]'),
  ).toHaveText("21,418");
  await expect(
    page.locator('[role="tab"][data-dashboard-repo-tab="commits"] [data-dashboard-repo-tab-count]'),
  ).toHaveCount(0);

  const appHeaderBox = await page.locator("[data-app-header]").boundingBox();
  const repoTabsBox = await page.locator("[data-repo-github-header]").boundingBox();
  expect(appHeaderBox).not.toBeNull();
  expect(repoTabsBox).not.toBeNull();
  expect(Math.abs(repoTabsBox.y - (appHeaderBox.y + appHeaderBox.height))).toBeLessThanOrEqual(1);
  const repoHeaderClasses = await page.locator("[data-repo-github-header]").getAttribute("class");
  expect(repoHeaderClasses).not.toContain("rounded-t-lg");
  expect(repoHeaderClasses).not.toContain(" border ");
  await expect(page.locator("[data-repo-detail]")).not.toContainText(
    "was not found",
  );
  expect(requestedPaths).toContain("/api/orgs/forkmesh/repos");
  expect(requestedPaths).toContain("/api/repo/forkmesh/forkmesh/tree");
  await expect(page).toHaveURL(/\/forkmesh\/forkmesh$/);

  await page.locator('[data-dashboard-repo-tab="mirrors"]').click();
  const mirrorPanel = page.locator('[data-dashboard-repo-tab-panel="mirrors"]');
  await expect(mirrorPanel).toBeVisible();
  await expect(page.locator("[data-mirror-summary]")).toContainText("Registered nodes");
  await expect(page.locator("[data-mirror-summary]")).toContainText("Online now");
  const mirrorCards = page.locator("[data-mirror-card]");
  await expect(mirrorCards).toHaveCount(3);
  await expect(mirrorCards.first()).toContainText("Revision");
  await expect(mirrorCards.first()).toContainText("Connection");
  await expect(mirrorCards.first()).toContainText("Repository contents");
  await expect(mirrorCards.first()).toContainText("Local activity");

  const desktopCardBoxes = await mirrorCards.evaluateAll((cards) =>
    cards.map((card) => card.getBoundingClientRect().toJSON()),
  );
  expect(Math.abs(desktopCardBoxes[0].y - desktopCardBoxes[1].y)).toBeLessThanOrEqual(1);
  expect(desktopCardBoxes[2].y).toBeGreaterThan(desktopCardBoxes[0].y);
  expect(
    await mirrorPanel.evaluate((panel) => panel.scrollWidth <= panel.clientWidth + 1),
  ).toBe(true);

  await page.setViewportSize({ width: 390, height: 844 });
  const mobileCardBoxes = await mirrorCards.evaluateAll((cards) =>
    cards.map((card) => card.getBoundingClientRect().toJSON()),
  );
  expect(mobileCardBoxes[1].y).toBeGreaterThan(mobileCardBoxes[0].y);
  expect(
    await mirrorPanel.evaluate((panel) => panel.scrollWidth <= panel.clientWidth + 1),
  ).toBe(true);
});
