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
        counts: {},
      };
    } else if (url.pathname === "/api/repo/forkmesh/forkmesh/mirrors") {
      body = {
        ok: true,
        mirrors: [
          {
            node: "mirror2",
            status: "online",
            cloneAvailable: true,
          },
        ],
        summary: { mirrors: 1 },
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
  await expect(page.locator("[data-repo-tree]")).toContainText("README.md");
  await expect(page.locator("[data-repo-availability-status]")).toHaveText(
    "served by mirror",
  );
  await expect(page.locator("[data-repo-detail]")).not.toContainText(
    "was not found",
  );
  expect(requestedPaths).toContain("/api/orgs/forkmesh/repos");
  expect(requestedPaths).toContain("/api/repo/forkmesh/forkmesh/tree");
  await expect(page).toHaveURL(/\/forkmesh\/forkmesh$/);
});
