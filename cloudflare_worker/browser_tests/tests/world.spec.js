const { test, expect } = require("@playwright/test");
const path = require("node:path");

const THREE_MODULE_URL =
  "https://cdn.jsdelivr.net/npm/three@0.184.0/build/three.module.min.js";
const THREE_MODULE_PATH = path.resolve(
  __dirname,
  "..",
  "node_modules",
  "three",
  "build",
  "three.module.min.js",
);
const CHAT_HTML_PATH = path.resolve(__dirname, "..", "..", "public", "chat.html");

const PRIVATE_SETTINGS = {
  theme: "world",
  availability: "inactive",
  activityCategory: "viewing-repository",
  publicDoor: "closed",
  displayName: "Never expose this name",
  privacy: {
    name: false,
    country: false,
    browser: false,
    os: false,
    activity: false,
    inactivity: false,
    localTime: false,
    nodes: false,
  },
};

const FIXED_NOW = 1_785_000_000_000;

async function prepareWorldPage(
  page,
  socketId,
  {
    includeThread = false,
    session = null,
    notifications = [],
    events = [],
    chatPassphrase = "",
    worldSocketHandler = null,
    repositoryFixture = null,
    repositoryStarFixture = null,
    accountFixture = null,
    unavailablePaths = [],
    chatChannels = [],
    chatChannelStatus = 200,
    ticketAuthenticated = true,
    systemCapacityTables = [],
  } = {},
) {
  let mentionState = "review";
  let notificationsRead = false;
  await page.addInitScript(({ now, identity, accountSession }) => {
    Date.now = () => now;
    let seed = [...identity].reduce(
      (value, char) => (Math.imul(value, 31) + char.charCodeAt(0)) >>> 0,
      0x5eed1234,
    );
    Math.random = () => {
      seed = (seed * 1664525 + 1013904223) >>> 0;
      return seed / 0x1_0000_0000;
    };
    sessionStorage.setItem(
      "forkmesh.world.guestId.v1",
      `playwright-${identity}`,
    );
    if (accountSession) {
      localStorage.setItem("forkmesh.session", JSON.stringify(accountSession));
    }
  }, { now: FIXED_NOW, identity: socketId, accountSession: session });
  await page.emulateMedia({ reducedMotion: "reduce" });
  page.on("pageerror", (error) => {
    process.stderr.write(`World page error: ${error.message}\n`);
  });
  page.on("console", (message) => {
    if (message.type() === "error") {
      process.stderr.write(`World console error: ${message.text()}\n`);
    }
  });
  await page.route(THREE_MODULE_URL, (route) =>
    route.fulfill({
      path: THREE_MODULE_PATH,
      contentType: "text/javascript; charset=utf-8",
    }),
  );
  await page.route("**/chat?embed=office", (route) =>
    route.fulfill({
      path: CHAT_HTML_PATH,
      contentType: "text/html; charset=utf-8",
    }),
  );
  await page.route("**/api/**", async (route) => {
    const url = new URL(route.request().url());
    if (unavailablePaths.includes(url.pathname)) {
      return route.fulfill({
        status: 503,
        contentType: "application/json; charset=utf-8",
        body: JSON.stringify({ ok: false, error: "fixture_unavailable" }),
      });
    }
    let metadataCapacityRelease = null;
    if (
      repositoryFixture &&
      Number(repositoryFixture.singleCoreDelayMs) > 0 &&
      url.pathname.startsWith("/api/repo/forkmesh/forkmesh/") &&
      ["/branches", "/tree", "/blobs", "/sizes", "/stats"].some((suffix) =>
        url.pathname.endsWith(suffix),
      )
    ) {
      const previous =
        repositoryFixture.metadataCapacityTail || Promise.resolve();
      metadataCapacityRelease = null;
      repositoryFixture.metadataCapacityTail = new Promise((resolve) => {
        metadataCapacityRelease = resolve;
      });
      await previous;
      repositoryFixture.metadataActive =
        Number(repositoryFixture.metadataActive || 0) + 1;
      repositoryFixture.maxMetadataActive = Math.max(
        Number(repositoryFixture.maxMetadataActive || 0),
        repositoryFixture.metadataActive,
      );
      await new Promise((resolve) =>
        setTimeout(
          resolve,
          Math.max(
            1,
            Math.min(
              1000,
              Number(repositoryFixture.singleCoreDelayMs) || 0,
            ),
          ),
        ),
      );
    }
    let status = 200;
    let body =
      url.pathname === "/api/world/context"
        ? {
            now: FIXED_NOW,
            countryCode: "",
            worldConnections: 64,
            worldMessagesPerSecond: 4,
            chatConnections: 128,
          }
        : url.pathname === "/api/network/overview"
          ? {
              ok: true,
              stats: {
                repos: 1,
                hosts: 2,
                clients: 0,
                onlineNodes: ["mirror2", "mirror3"],
              },
              leaderboards: {
                nodes: [
                  { name: "mirror2", pullCount: 5, issueCount: 7 },
                  { name: "mirror3", pullCount: 5, issueCount: 7 },
                ],
              },
              history: {},
            }
        : url.pathname === "/api/repo/forkmesh/forkmesh/mirrors"
          ? {
              ok: true,
              owner: "forkmesh",
              repo: "forkmesh",
              mirrors: [
                {
                  node: "mirror2",
                  status: "online",
                  integrity: "ok",
                  cloneAvailable: true,
                  commit: "a".repeat(40),
                  branch: "main",
                  sizeBytes: 75_139_176,
                  issueCount: 47,
                  commitCount: 8661,
                  branchCount: 18,
                  pullCount: 42,
                  discussionCount: 2,
                  artifactCount: 1,
                  platform: "linux",
                  version: "0.7.0",
                  cpuPercent: 25,
                  memUsedBytes: 536_870_912,
                  memTotalBytes: 1_073_741_824,
                  diskUsedBytes: 5_368_709_120,
                  diskTotalBytes: 10_737_418_240,
                },
                {
                  node: "mirror3",
                  status: "online",
                  integrity: "ok",
                  cloneAvailable: true,
                  commit: "a".repeat(40),
                  branch: "main",
                  sizeBytes: 74_944_512,
                  issueCount: 47,
                  commitCount: 8661,
                  branchCount: 18,
                  pullCount: 42,
                  discussionCount: 2,
                  artifactCount: 1,
                  platform: "linux",
                  version: "0.7.0",
                },
              ],
            }
        : url.pathname === "/api/world/instances"
          ? {
              instances: [
                {
                  id: "0123456789abcdef01234567",
                  label: "North relay",
                  origin: "https://north.example/",
                  approved: true,
                  health: "online",
                  online: true,
                  healthEvidence: "fresh-verified-node-health",
                  federationPublicKey: "must-not-render",
                  token: "must-not-render",
                },
                {
                  id: "89abcdef0123456789abcdef",
                  label: "Garden relay",
                  origin: "https://garden.example/",
                  approved: true,
                  health: "awaiting_verified_health",
                  online: false,
                  healthEvidence: "no-fresh-verified-node-health",
                },
              ],
            }
        : url.pathname === "/api/chat/room-key"
          ? url.searchParams.get("room") === "world-general" &&
            chatPassphrase
            ? {
                ok: true,
                scope: "mainnode/forkmesh",
                room: "world-general",
                access: "public-world-general",
                passphrase: chatPassphrase,
              }
            : ((status = 401), { error: "unauthorized" })
        : url.pathname === "/api/chat/channels"
          ? chatChannelStatus === 200
            ? { ok: true, channels: chatChannels }
            : ((status = chatChannelStatus), { error: "invalid_session" })
        : url.pathname === "/api/world/events"
          ? { events }
          : url.pathname === "/api/notifications"
            ? route.request().method() === "POST"
              ? ((notificationsRead = true), { ok: true })
              : {
                  ok: true,
                  notifications: notifications.map((item) => ({
                    ...item,
                    readAt: notificationsRead
                      ? item.readAt || FIXED_NOW
                      : item.readAt || 0,
                  })),
                  unread: notificationsRead
                    ? 0
                    : notifications.filter((item) => !item.readAt).length,
                }
            : url.pathname === "/api/world/ticket" && session
              ? ticketAuthenticated
                ? {
                    authenticated: true,
                    accountStatus: "Registered",
                    name: session.nodeName,
                    isAdmin: systemCapacityTables.length > 0,
                    ticket: "playwright-world-ticket",
                    expiresAt: FIXED_NOW + 300_000,
                    ...(systemCapacityTables.length
                      ? {
                          systemCapacity: {
                            tables: systemCapacityTables,
                          },
                        }
                      : {}),
                  }
                : {
                    ok: true,
                    authenticated: false,
                    accountStatus: "Guest",
                    nodeCount: 0,
                    ticket: "",
                  }
          : url.pathname === "/api/world/community-ads/placements"
            ? {
                ok: true,
                label: "Community-reviewed placement",
                context: "town-square",
                tracking: "none",
                behavioralTargeting: false,
                sensitiveTargeting: false,
                personalDataUsed: false,
                placements: [
                  {
                    proposalId: "a".repeat(32),
                    label: "Community-reviewed placement",
                    sponsor: "Open Tools Cooperative",
                    copy: "Auditable build tools for open-source teams.",
                    destinationUrl:
                      "https://sponsor.example.org/open-builds",
                    whyShown:
                      "Enabled for the town-square page context.",
                    tracking: "none",
                  },
                ],
              }
          : url.pathname === "/api/world/fediverse-mentions" &&
              route.request().method() === "GET"
            ? {
                ok: true,
                automaticIssueCreation: false,
                items: [
                  {
                    id: "b".repeat(32),
                    kind: "mention",
                    state: mentionState,
                    repository: "acme/project",
                    author: "alice@social.example.org",
                    authorName: "Alice",
                    excerpt: "The save button crashes on large projects.",
                    remoteUrl:
                      "https://social.example.org/@alice/42",
                    issueUrl: "",
                    issueNumber: 0,
                    progress:
                      mentionState === "pending"
                        ? "Pending owner-node materialization."
                        : "Awaiting authorized manual review.",
                    verifiedPublicActivity: true,
                    automaticIssueCreation: false,
                  },
                  ...(includeThread
                    ? [
                        {
                          id: "c".repeat(32),
                          kind: "reply",
                          state: "linked",
                          repository: "acme/project",
                          author: "bob@lemmy.example.org",
                          authorName: "Bob",
                          excerpt: "Nested reply from a public Lemmy thread.",
                          remoteUrl:
                            "https://lemmy.example.org/comment/77",
                          issueUrl:
                            "https://forkmesh.example.org/acme/project/issues/17",
                          issueNumber: 17,
                          progress:
                            "Verified reply linked to a tracked public issue.",
                          verifiedPublicActivity: true,
                          automaticIssueCreation: false,
                        },
                      ]
                    : []),
                ],
              }
          : url.pathname ===
              "/api/repo/acme/project/fedi-comments"
            ? {
                ok: true,
                comments: [
                  {
                    remoteId:
                      "https://lemmy.example.org/comment/77",
                    parentRemoteId: "",
                    author: "bob",
                    authorName: "Bob",
                    body: "Nested reply from a public Lemmy thread.",
                    backlink:
                      "https://lemmy.example.org/comment/77",
                    sourceInstance: "lemmy.example.org",
                    sourceSoftware: "lemmy",
                    lifecycle: "active",
                    depth: 1,
                    nativeEvent: false,
                    provenance: {
                      source: "activitypub",
                      instance: "lemmy.example.org",
                      software: "lemmy",
                      backlink:
                        "https://lemmy.example.org/comment/77",
                      nativeEvent: false,
                    },
                  },
                ],
              }
          : url.pathname.endsWith("/preview")
            ? {
                ok: true,
                mentionId: "b".repeat(32),
                repository: "acme/project",
                author: "alice@social.example.org",
                remoteUrl: "https://social.example.org/@alice/42",
                draft: {
                  title: "Save button crashes",
                  body: "The save button crashes on large projects.",
                },
                state: "review",
                requiresExplicitConfirmation: true,
                followupDefault: false,
              }
          : url.pathname.endsWith("/create")
            ? ((mentionState = "pending"),
              {
                ok: true,
                state: "pending",
                issueNumber: 0,
                proposedIssueNumber: 17,
                created: false,
              })
          : url.pathname.endsWith("/moderate")
            ? { ok: true, state: "dismissed" }
          : url.pathname === "/api/world/fediverse"
            ? { mastodon: [], lemmy: [], x: [], reddit: [] }
          : url.pathname === "/api/world/media/spaces"
            ? { spaces: [] }
            : url.pathname === "/api/version"
              ? {
                  ok: true,
                  version: "0.7.0",
                  rev: "d".repeat(40),
                  now: FIXED_NOW,
                }
            : url.pathname === "/api/repositories"
              ? { repositories: [] }
              : {};
    if (accountFixture && url.pathname === "/api/accounts/signup") {
      body = {
        ok: true,
        nodeName: "world-user",
        email: "world-user@example.test",
        emailVerified: false,
      };
    } else if (accountFixture && url.pathname === "/api/accounts/login") {
      body = {
        ok: true,
        nodeName: "world-user",
        email: "world-user@example.test",
        status: "registered",
        emailVerified: true,
        isAdmin: false,
        sessionToken: "world-session-token",
        nodes: [],
      };
    }
    if (repositoryFixture) {
      const codeOid = (
        repositoryFixture.mergePublished ? "f" : "a"
      ).repeat(40);
      const pullOid = (
        repositoryFixture.mergePublished ? "d" : "b"
      ).repeat(40);
      const stateHash = "c".repeat(64);
      const pullMarkdown = (number, title, head) => `---
schema: forkmesh-pull-v1
number: ${number}
title: "${title}"
status: ${
  repositoryFixture.mergePublished && number === 44 ? "merged" : "open"
}
authorName: Alice
base: main
head: ${head}
derive: branch
creationBaseOid: ${"a".repeat(40)}
creationHeadOid: ${"e".repeat(40)}
---
This description came from the exact pull metadata commit.`;
      const longContext = Array.from(
        { length: 90 },
        (_, index) => ` line ${index + 1}`,
      ).join("\n");
      const patch = `diff --git a/src/alpha.js b/src/alpha.js
index 1111111..2222222 100644
--- a/src/alpha.js
+++ b/src/alpha.js
@@ -1,90 +1,91 @@
${longContext}
+const worldReviewMarker = "world-pr-diff-visible";
diff --git a/src/beta.js b/src/beta.js
index 3333333..4444444 100644
--- a/src/beta.js
+++ b/src/beta.js
@@ -1,90 +1,90 @@
${longContext}
-const oldValue = false;
+const oldValue = true;`;
      const repoBase = "/api/repo/forkmesh/forkmesh";
      const pullMergeMatch = url.pathname.match(
        /^\/api\/repo\/forkmesh\/forkmesh\/pulls\/44\/merge$/,
      );
      if (
        repositoryStarFixture &&
        url.pathname === `${repoBase}/star`
      ) {
        const method = route.request().method();
        const authorization =
          route.request().headers().authorization || "";
        let requestBody = null;
        if (route.request().postData()) {
          try {
            requestBody = route.request().postDataJSON();
          } catch (_) {
            requestBody = "invalid-json";
          }
        }
        if (!Array.isArray(repositoryStarFixture.requests)) {
          repositoryStarFixture.requests = [];
        }
        repositoryStarFixture.requests.push({
          path: url.pathname,
          method,
          authorization,
          body: requestBody,
        });
        repositoryStarFixture.count = Math.max(
          0,
          Number(repositoryStarFixture.count) || 0,
        );
        repositoryStarFixture.starred =
          repositoryStarFixture.starred === true;
        if (method === "GET") {
          body = {
            ok: true,
            count: repositoryStarFixture.count,
            starred: Boolean(session && repositoryStarFixture.starred),
          };
        } else if (!["POST", "DELETE"].includes(method)) {
          status = 405;
          body = { error: "method_not_allowed" };
        } else if (!session?.sessionToken || !authorization) {
          status = 401;
          body = { error: "invalid_session" };
        } else if (repositoryStarFixture.failMutation) {
          status = 503;
          body = { error: "fixture_unavailable" };
        } else {
          const nextStarred = method === "POST";
          if (nextStarred !== repositoryStarFixture.starred) {
            repositoryStarFixture.count = Math.max(
              0,
              repositoryStarFixture.count + (nextStarred ? 1 : -1),
            );
          }
          repositoryStarFixture.starred = nextStarred;
          body = {
            ok: true,
            count: repositoryStarFixture.count,
            starred: repositoryStarFixture.starred,
          };
        }
      } else if (pullMergeMatch && route.request().method() === "POST") {
        let request = {};
        try {
          request = route.request().postDataJSON();
        } catch (_) {}
        const expectedRequest =
          request?.schemaVersion === 1 &&
          request?.type === "forkmesh.pull-merge-v1" &&
          request?.pullNumber === 44 &&
          /^[A-Za-z0-9_-]{12,80}$/.test(String(request?.requestId || "")) &&
          request?.expectedBaseOid === "a".repeat(40) &&
          request?.expectedHeadOid === "e".repeat(40) &&
          request?.expectedPullsOid === "b".repeat(40);
        if (!expectedRequest) {
          status = 400;
          body = { error: "invalid_request" };
        } else if (repositoryFixture.mergeOutcome === "forbidden") {
          status = 403;
          body = { error: "forbidden" };
        } else if (repositoryFixture.mergeOutcome === "conflict") {
          status = 409;
          body = {
            ok: false,
            status: "failed",
            requestId: request.requestId,
            error: "merge_conflict",
          };
        } else if (repositoryFixture.mergeOutcome === "stale") {
          status = 409;
          body = {
            ok: false,
            status: "failed",
            requestId: request.requestId,
            error: "stale_base",
          };
        } else if (repositoryFixture.mergeOutcome === "retry") {
          const attempt = Number(repositoryFixture.mergeRequestCount || 0);
          repositoryFixture.mergeRequestCount = attempt + 1;
          if (attempt < 6) {
            status = 202;
            body = {
              ok: true,
              status: "processing",
              requestId: request.requestId,
            };
          } else {
            status = 409;
            body = {
              ok: false,
              status: "failed",
              requestId: request.requestId,
              error: "merge_conflict",
            };
          }
        } else {
          const attempt = Number(repositoryFixture.mergeRequestCount || 0);
          repositoryFixture.mergeRequestCount = attempt + 1;
          if (attempt === 0) {
            status = 202;
            body = {
              ok: true,
              status: "processing",
              requestId: request.requestId,
            };
          } else {
            status = 200;
            body = {
              ok: true,
              status: "merged",
              requestId: request.requestId,
              published: true,
              baseBefore: "a".repeat(40),
              head: "e".repeat(40),
              pullsBefore: "b".repeat(40),
              baseAfter: "f".repeat(40),
              pullsAfter: "d".repeat(40),
            };
            repositoryFixture.mergePublished = true;
          }
        }
      } else if (url.pathname === "/api/repositories") {
        const repositoryOwners = repositoryFixture.staleOfflineAlias
          ? ["jett", "mirror2", "mirror3"]
          : ["mirror2", "mirror3"];
        body = {
          repositories: repositoryOwners.map((owner) => {
            const stale = owner === "jett";
            const conflicting =
              repositoryFixture.conflictingHealthyAlias &&
              owner === "mirror3";
            const missingStateHash =
              repositoryFixture.missingHealthyStateHash &&
              owner === "mirror3";
            return {
              owner,
              name: "forkmesh",
              source: stale ? "local-node" : "remote-clone",
              liveHost: !stale,
              commit: stale || conflicting ? "d".repeat(40) : codeOid,
              stateHash: missingStateHash
                ? ""
                : stale || conflicting
                  ? "e".repeat(64)
                  : stateHash,
              pullCount: stale ? 1 : 2,
              updatedAt: stale ? FIXED_NOW + 1000 : FIXED_NOW,
            };
          }),
        };
      } else if (url.pathname === `${repoBase}/mirrors`) {
        const mirrorNodes = ["mirror2", "mirror3"];
        if (repositoryFixture.staleOfflineAlias) mirrorNodes.push("jett");
        body = {
          ok: true,
          owner: "forkmesh",
          repo: "forkmesh",
          mirrors: mirrorNodes.map((node) => {
            const stale = node === "jett";
            const conflicting =
              repositoryFixture.conflictingHealthyAlias &&
              node === "mirror3";
            return {
              node,
              status: stale ? "offline" : "online",
              integrity: stale ? "healing" : "ok",
              cloneAvailable: !stale,
              behind: stale,
              commit: stale || conflicting ? "d".repeat(40) : codeOid,
              branch: "main",
              pullCount: stale ? 1 : 2,
              version: "0.7.0",
              ...(node === "mirror2"
                ? {
                    lastCommitMessage: "Ship the world-edge portals",
                    lastCommitAuthorName: "Ada Lovelace",
                    syncAgeMs: 2 * 60 * 60 * 1000,
                  }
                : {}),
            };
          }),
        };
      } else if (url.pathname === `${repoBase}/branches`) {
        if (
          repositoryFixture.rejectPullMetadataAfterIssueFanout &&
          repositoryFixture.issueTreeRequestStarted
        ) {
          status = 503;
          body = { ok: false, error: "mirror_capacity_exhausted" };
        } else {
          body = {
            ok: true,
            branches: repositoryFixture.invalidPullBranch
              ? [{ name: "main", commit: codeOid }]
              : [
                  { name: "main", commit: codeOid },
                  { name: "forkmesh/pulls", commit: pullOid },
                ],
          };
        }
      } else if (url.pathname === `${repoBase}/tree`) {
        const treePath = url.searchParams.get("path") || "";
        const ref = url.searchParams.get("ref") || "";
        if (treePath === "") {
          body = {
            ok: true,
            commit: codeOid,
            entries: [
              {
                name: "src",
                path: "src",
                type: "tree",
                size: 4096,
                author: "Alice",
              },
              {
                name: "README.md",
                path: "README.md",
                type: "blob",
                size: 1200,
                author: "Alice",
              },
            ],
          };
        } else if (treePath === "src" && ref === codeOid) {
          body = {
            ok: true,
            commit: codeOid,
            entries: [
              {
                name: "[id]+C++.tsx",
                path: "src/[id]+C++.tsx",
                type: "blob",
                size: 4096,
                author: "Alice",
              },
            ],
          };
        } else if (treePath.startsWith(".forkmesh/issues")) {
          repositoryFixture.issueTreeRequestStarted = true;
          const issueDelay = Math.max(
            0,
            Math.min(
              5000,
              Number(repositoryFixture.issueTreeDelayMs) || 0,
            ),
          );
          if (issueDelay) {
            await new Promise((resolve) => setTimeout(resolve, issueDelay));
          }
          body = { ok: true, commit: codeOid, entries: [] };
        } else if (
          treePath === "pulls" &&
          !repositoryFixture.invalidPullBranch &&
          ref === pullOid
        ) {
          body = {
            ok: true,
            commit: pullOid,
            entries: [
              { name: "44", path: "pulls/44", type: "tree" },
              { name: "43", path: "pulls/43", type: "tree" },
            ],
          };
        } else {
          status = 404;
          body = { ok: false, error: "not_found" };
        }
      } else if (url.pathname === `${repoBase}/sizes`) {
        body = {
          ok: true,
          commit: codeOid,
          name: "",
          type: "directory",
          size: 5296,
          fileCount: 2,
          children: [
            {
              name: "src",
              type: "directory",
              size: 4096,
              children: [
                {
                  name: "[id]+C++.tsx",
                  path: "src/[id]+C++.tsx",
                  type: "file",
                  size: 4096,
                },
              ],
            },
            {
              name: "README.md",
              path: "README.md",
              type: "file",
              size: 1200,
            },
          ],
        };
      } else if (url.pathname === `${repoBase}/stats`) {
        body = {
          ok: true,
          commit: codeOid,
          fileCount: 2,
          contributorCount: 1,
          contributors: [{ name: "Alice", commits: 4 }],
        };
      } else if (url.pathname === `${repoBase}/blobs`) {
        const paths = url.searchParams.getAll("path");
        const ref = url.searchParams.get("ref") || "";
        if (
          repositoryFixture.invalidPullBranch ||
          ref !== pullOid ||
          paths.some((item) => !/^pulls\/(?:43|44)\/(?:pull\.md|changes\.patch)$/.test(item))
        ) {
          status = 404;
          body = { ok: false, error: "not_found" };
        } else {
          const blobs = {};
          paths.forEach((item) => {
            if (item === "pulls/44/pull.md") {
              blobs[item] = {
                ok: true,
                encoding: "utf8",
                content: pullMarkdown(44, "Review inside the World", "review-ui"),
              };
            } else if (item === "pulls/43/pull.md") {
              blobs[item] = repositoryFixture.missingPullMetadata
                ? null
                : {
                    ok: true,
                    encoding: "utf8",
                    content: pullMarkdown(
                      43,
                      "Earlier exact review",
                      "earlier",
                    ),
                  };
            } else if (item === "pulls/44/changes.patch") {
              blobs[item] = { ok: true, encoding: "utf8", content: patch };
            } else {
              blobs[item] = null;
            }
          });
          body = { ok: true, commit: pullOid, blobs };
        }
      }
    }
    if (metadataCapacityRelease) {
      repositoryFixture.metadataActive = Math.max(
        0,
        Number(repositoryFixture.metadataActive || 0) - 1,
      );
      metadataCapacityRelease();
    }
    return route.fulfill({
      status,
      contentType: "application/json; charset=utf-8",
      body: JSON.stringify(body),
    });
  });
  await page.routeWebSocket("**/api/world/ws*", (socket) => {
    if (worldSocketHandler) {
      worldSocketHandler(socket, socketId);
      return;
    }
    socket.send(
      JSON.stringify({
        type: "welcome",
        id: socketId,
        peers: [],
      }),
    );
  });
}

async function waitForWorld(page, url = "/world/") {
  await page.goto(url);
  await waitForWorldReady(page);
}

async function waitForWorldReady(page) {
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return (
      Boolean(shell?.world?.renderer?.domElement) &&
      Number(shell.world.renderer.info?.render?.frame || 0) > 0
    );
  });
  await page.evaluate(() => document.fonts?.ready);
}

async function openWorldPullReview(page, number = 44) {
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "ready" &&
      shell?.activeRepository?.owner === "forkmesh" &&
      shell?.activeRepository?.repo === "forkmesh";
  });
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("repositories"),
  );
  await page.getByRole("button", { name: /^2 pull requests$/i }).click();
  await page
    .locator(`[data-world-pull-open][data-world-pull-number='${number}']`)
    .click();
  await expect(
    page.getByRole("heading", { name: `Pull request #${number}` }),
  ).toBeVisible();
}

async function openWorldRepositoryExplorer(page) {
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "ready" &&
      shell?.activeRepository?.owner === "forkmesh" &&
      shell?.activeRepository?.repo === "forkmesh";
  });
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("repositories"),
  );
}

test("signed-in World receives private and global notifications", async ({
  page,
}) => {
  const notificationRequests = [];
  page.on("request", (request) => {
    if (new URL(request.url()).pathname === "/api/notifications") {
      notificationRequests.push({
        method: request.method(),
        authorization: request.headers().authorization || "",
        body: request.postData() ? request.postDataJSON() : null,
      });
    }
  });
  await prepareWorldPage(page, "world-notifications", {
    session: {
      nodeName: "jett",
      sessionToken: "playwright-jett-session",
    },
    notifications: [
      {
        id: "a".repeat(64),
        kind: "Repository",
        title: "Mirror refresh completed",
        body: "forkmesh/forkmesh is healthy on the registered mirror.",
        ts: FIXED_NOW - 1_000,
        readAt: 0,
      },
      {
        id: "c".repeat(64),
        kind: "Issue",
        title: "Issue #17 / forkmesh/forkmesh",
        body: "Literal punctuation stays readable; markup <b>does not run</b>.",
        href: "javascript:alert('must-not-run')",
        ts: FIXED_NOW - 2_000,
        readAt: FIXED_NOW - 1_500,
      },
    ],
    events: [
      {
        id: "b".repeat(32),
        type: "Infrastructure",
        title: "ForkMesh production deployment complete",
        description: "The production route and multiplayer World are ready.",
        destination: "Town Square",
        startsAt: new Date(FIXED_NOW - 60_000).toISOString(),
        endsAt: new Date(FIXED_NOW + 3_600_000).toISOString(),
      },
    ],
  });
  await waitForWorld(page);

  const badge = page.locator("[data-world-notification-count]");
  await expect(badge).toBeVisible();
  await expect(badge).toHaveText("2");
  await page.getByRole("button", { name: /Open World notifications/ }).click();
  const panel = page.locator("[data-world-detail]");
  await expect(panel).toContainText("Mirror refresh completed");
  await expect(panel).toContainText(
    "ForkMesh production deployment complete",
  );
  await expect(panel).toContainText(
    "The production route and multiplayer World are ready.",
  );
  await expect(panel).toContainText("Issue #17 / forkmesh/forkmesh");
  await expect(panel).toContainText(
    "Literal punctuation stays readable; markup <b>does not run</b>.",
  );
  await expect(
    panel.locator(`[data-world-notification-id="${"c".repeat(64)}"] a`),
  ).toHaveCount(0);
  await expect(
    panel.locator('[data-world-notification-id][data-unread="true"]'),
  ).toHaveCount(1);

  const initialGet = notificationRequests.find(
    (request) => request.method === "GET",
  );
  expect(initialGet?.authorization).toBe(
    "Bearer playwright-jett-session",
  );
  const markReadRequest = page.waitForRequest(
    (request) =>
      new URL(request.url()).pathname === "/api/notifications" &&
      request.method() === "POST",
  );
  await panel.getByRole("button", { name: "Mark all read" }).click();
  await markReadRequest;
  await expect(badge).toHaveText("1");
  await expect(
    panel.locator('[data-world-notification-id][data-unread="false"]'),
  ).toHaveCount(2);
  const markRead = notificationRequests.find(
    (request) => request.method === "POST",
  );
  expect(markRead).toEqual({
    method: "POST",
    authorization: "Bearer playwright-jett-session",
    body: { node: "jett", all: true },
  });

  await page.locator("forkmesh-world").evaluate(async (shell) => {
    localStorage.removeItem("forkmesh.session");
    await shell.refreshPersonalNotifications(false);
  });
  await expect(panel).not.toContainText("Mirror refresh completed");
  await expect(panel).not.toContainText("Issue #17 / forkmesh/forkmesh");
  await expect(panel).toContainText("Sign in to receive");
});

test("account signup and login complete inside the World without leaking into URLs", async ({
  page,
  context,
}) => {
  const testAccountPassword = ["correct-horse", "battery-staple"].join("-");
  const accountRequests = [];
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.pathname.startsWith("/api/accounts/")) {
      accountRequests.push({
        path: url.pathname,
        method: request.method(),
        body: request.postDataJSON(),
      });
    }
    expect(request.url()).not.toContain("world-user@example.test");
    expect(request.url()).not.toContain(testAccountPassword);
  });
  await prepareWorldPage(page, "world-account", {
    accountFixture: true,
  });
  await waitForWorld(page);
  const originalPages = context.pages().length;

  await page.getByRole("button", { name: "Login" }).click();
  const account = page.locator("[data-world-account]");
  await expect(account).toBeVisible();
  await expect(account).toContainText(
    "form contents never enter multiplayer presence",
  );
  await account.getByRole("tab", { name: "Create account" }).click();
  const signup = account.locator("[data-world-signup-form]");
  await signup.locator("[name='nodeName']").fill("world-user");
  await signup.locator("[name='email']").fill("world-user@example.test");
  await signup
    .locator("[name='password']")
    .fill(testAccountPassword);
  await signup.locator("[name='terms']").check();
  await signup.getByRole("button", { name: /Create account inside/ }).click();
  await expect(account.locator("[data-world-account-verification]")).toBeVisible();
  await expect(account).toContainText("world-user@example.test");
  expect(accountRequests.find((item) => item.path.endsWith("/signup"))).toEqual({
    path: "/api/accounts/signup",
    method: "POST",
    body: {
      nodeName: "world-user",
      email: "world-user@example.test",
      password: testAccountPassword,
    },
  });

  await account.getByRole("button", { name: "Close account panel" }).click();
  await page.getByRole("button", { name: "Login" }).click();
  const login = account.locator("[data-world-login-form]");
  await login.locator("[name='email']").fill("world-user@example.test");
  await login
    .locator("[name='password']")
    .fill(testAccountPassword);
  const reloaded = page.waitForNavigation({ waitUntil: "domcontentloaded" });
  await login.getByRole("button", { name: /Log in inside/ }).click();
  await reloaded;
  await page.waitForFunction(
    () =>
      document
        .querySelector("forkmesh-world")
        ?.getAttribute("data-world-ready") === "true",
  );
  await expect(
    page.getByRole("button", { name: "Account" }),
  ).toBeVisible();
  const stored = await page.evaluate(() =>
    JSON.parse(localStorage.getItem("forkmesh.session") || "null"),
  );
  expect(stored).toMatchObject({
    nodeName: "world-user",
    email: "world-user@example.test",
    sessionToken: "world-session-token",
  });
  expect(accountRequests.find((item) => item.path.endsWith("/login"))).toEqual({
    path: "/api/accounts/login",
    method: "POST",
    body: {
      email: "world-user@example.test",
      password: testAccountPassword,
      totp: "",
    },
  });
  expect(context.pages()).toHaveLength(originalPages);
  expect(new URL(page.url()).pathname).toBe("/world/");
});

test("ForkMesh Office opens encrypted chat only after explicit entry", async ({
  page,
  context,
}) => {
  const passphrase = "playwright-public-world-general-passphrase";
  const chatSocketURLs = [];
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      chatSocketURLs.push(socket.url());
    },
  );
  await prepareWorldPage(page, "world-chat", {
    chatPassphrase: passphrase,
  });
  await waitForWorld(page);

  const worldURL = page.url();
  const pageCount = context.pages().length;
  // The World's always-present embedded global chat connects independently of
  // the Office. Capture that baseline so this journey proves that focusing or
  // approaching the Office does not open an additional Office chat transport.
  const globalChatSocketCount = chatSocketURLs.length;
  expect(globalChatSocketCount).toBeGreaterThanOrEqual(1);
  await page.locator("[data-world-office-focus]").first().click();
  expect(chatSocketURLs).toHaveLength(globalChatSocketCount);

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.player.position.set(11, 0.38, -17.7);
  });
  const entry = page.locator("[data-world-office-enter]");
  await expect(entry).toBeVisible();
  expect(chatSocketURLs).toHaveLength(globalChatSocketCount);
  await page.evaluate(() => document.activeElement?.blur());
  await page.keyboard.press("e");
  await page.locator("[data-world-office-fallback]").click();

  const chat = page.locator("[data-world-office-chat]");
  await expect(chat).toBeVisible();
  const officeFrame = page.locator("[data-world-office-frame]");
  await expect(officeFrame).toHaveAttribute("src", "/chat?embed=office");
  const chatFrame = page.frameLocator("[data-world-office-frame]");
  const chatInput = chatFrame.locator("#chat-input");
  await expect(chatInput).toBeVisible();
  await expect(chatFrame.locator("#chat-status")).toHaveText(
    "Connected · public World #general",
  );
  await chat.getByRole("button", {
    name: "Close accessible chat fallback",
  }).click();
  await expect(chat).toBeHidden();
  await expect(officeFrame).not.toHaveAttribute("src", /.+/, {
    timeout: 3500,
  });
  expect(chatSocketURLs).toHaveLength(globalChatSocketCount + 1);
  expect(page.url()).toBe(worldURL);
  expect(context.pages()).toHaveLength(pageCount);
});

test("ForkMesh Office preserves registered channel authorization", async ({
  page,
}) => {
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    () => {},
  );
  await prepareWorldPage(page, "office-admin", {
    session: {
      kind: "user",
      nodeName: "admin",
      email: "admin@example.test",
      sessionToken: "admin-token",
      isAdmin: true,
    },
    chatPassphrase: "playwright-office-admin-passphrase",
    chatChannels: [
      {
        id: "a".repeat(32),
        name: "announcements",
        visibility: "public",
        canManage: true,
        keyVersion: 1,
        updatedAt: FIXED_NOW,
      },
      {
        id: "b".repeat(32),
        name: "leadership",
        visibility: "private",
        canManage: true,
        keyVersion: 1,
        updatedAt: FIXED_NOW,
      },
    ],
  });
  await waitForWorld(page);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.player.position.set(11, 0.38, -17.7);
  });
  await page.locator("[data-world-office-enter]").click();
  const nativeRooms = page.locator("[data-world-office-room-board]");
  await expect(nativeRooms).toContainText("#general");
  await expect(nativeRooms).toContainText("#announcements");
  await expect(nativeRooms).toContainText("#leadership");
  await page.locator("[data-world-office-fallback]").click();

  const office = page.frameLocator("[data-world-office-frame]");
  await expect(office.locator("#chat-rooms")).toContainText("#general");
  await expect(office.locator("#chat-rooms")).toContainText("#announcements");
  await expect(office.locator("#chat-rooms")).toContainText("#leadership");
  await expect(office.locator("#chat-office-manage")).toBeVisible();
  await expect(office.locator("#chat-channel-create")).toBeHidden();
  await expect(office.locator("#chat-channel-manage")).toBeHidden();
});

test("ForkMesh Office explains an expired authorized session", async ({
  page,
}) => {
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    () => {},
  );
  await prepareWorldPage(page, "office-expired", {
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "expired-token",
    },
    chatPassphrase: "playwright-office-expired-passphrase",
    chatChannelStatus: 401,
  });
  await waitForWorld(page);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.player.position.set(11, 0.38, -17.7);
  });
  await page.locator("[data-world-office-enter]").click();
  await expect(page.locator("[data-world-office-room-board]")).toContainText(
    "#general",
  );
  await expect(page.locator("[data-world-office-lobby-status]")).toContainText(
    "Channels are temporarily unavailable",
  );
  await page.locator("[data-world-office-fallback]").click();

  const office = page.frameLocator("[data-world-office-frame]");
  await expect(office.locator("#chat-office-alert")).toContainText(
    "Your session expired. Log in again to use authorized channels.",
  );
  await expect(office.locator("#chat-office-login")).toBeVisible();
  await expect(office.locator("#chat-rooms")).toContainText("#general");
});

test("reward-program links deep-link to the self-custodial fountain controls", async ({
  page,
}) => {
  await prepareWorldPage(page, "reward-program-deep-link");
  await waitForWorld(page, "/world/?landmark=fountain");

  const panel = page.locator("[data-world-detail]");
  await expect(panel).toBeVisible();
  await expect(
    panel.getByRole("heading", {
      name: "Join or fund the community reward program",
    }),
  ).toBeVisible();
  await expect(panel).toContainText(
    "does not purchase ownership, guaranteed rewards, investment returns, or governance control",
  );
  await expect(
    panel.getByRole("button", { name: "Prepare direct wallet transfer" }),
  ).toBeVisible();
});

test("detail panels overlay the desktop without dimming or reframing the World", async ({
  page,
}) => {
  await page.setViewportSize({ width: 1280, height: 800 });
  await prepareWorldPage(page, "non-modal-world-detail");
  await waitForWorld(page);
  await expect
    .poll(() => page.evaluate(() => window.innerWidth))
    .toBe(1280);
  await expect
    .poll(() =>
      page.evaluate(() => matchMedia("(max-width: 720px)").matches),
    )
    .toBe(false);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setPaused(true);
    shell.detailTestFocusCalls = 0;
    const focusLandmark = shell.world.focusLandmark;
    shell.world.focusLandmark = (...args) => {
      shell.detailTestFocusCalls += 1;
      return focusLandmark(...args);
    };
  });

  const before = await page.locator("forkmesh-world").evaluate((shell) => {
    const canvas = shell.world.renderer.domElement;
    const rect = canvas.getBoundingClientRect();
    return {
      canvas: {
        x: rect.x,
        y: rect.y,
        width: rect.width,
        height: rect.height,
      },
      camera: shell.world.camera.position.toArray(),
    };
  });
  const trigger = page.locator(
    '.world-map [data-world-landmark="information"]',
  );
  await trigger.click();
  const detail = page.locator("[data-world-detail]");
  await expect(detail).toBeVisible();
  await expect(detail).toHaveAttribute("role", "dialog");
  await expect(detail).toHaveAttribute("aria-modal", "false");
  await expect(
    detail.getByRole("button", { name: "Close Information booth" }),
  ).toBeFocused();

  const openState = await page.locator("forkmesh-world").evaluate((shell) => {
    const canvas = shell.world.renderer.domElement;
    const rect = canvas.getBoundingClientRect();
    const backdrop = shell.querySelector("[data-world-detail-backdrop]");
    const backdropStyle = getComputedStyle(backdrop);
    return {
      focusCalls: shell.detailTestFocusCalls,
      canvas: {
        x: rect.x,
        y: rect.y,
        width: rect.width,
        height: rect.height,
      },
      camera: shell.world.camera.position.toArray(),
      backdrop: {
        opacity: backdropStyle.opacity,
        visibility: backdropStyle.visibility,
        pointerEvents: backdropStyle.pointerEvents,
      },
    };
  });
  expect(openState.focusCalls).toBe(0);
  expect(openState.canvas).toEqual(before.canvas);
  expect(openState.camera).toEqual(before.camera);
  expect(openState.backdrop).toEqual({
    opacity: "0",
    visibility: "hidden",
    pointerEvents: "none",
  });
  expect(
    await page
      .locator("forkmesh-world")
      .evaluate((shell) => shell.detailReturnFocus?.dataset.worldLandmark),
  ).toBe("information");

  await page.locator("[data-world-detail-close]").click();
  await expect(trigger).toBeFocused();

  await page.setViewportSize({ width: 600, height: 800 });
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("information"),
  );
  await expect(detail).toBeVisible();
  const mobileBackdrop = page.locator("[data-world-detail-backdrop]");
  await expect(mobileBackdrop).toHaveCSS("visibility", "visible");
  await expect(mobileBackdrop).toHaveCSS("pointer-events", "auto");
  await mobileBackdrop.click({ position: { x: 8, y: 8 } });
  await expect(detail).toHaveAttribute("data-open", "false");
});

async function freezeWorld(page) {
  await page.locator("forkmesh-world").evaluate((shell) => {
    // The default theme is fixed full daylight. Wall-clock time never
    // participates in scene lighting.
    shell.world.setTheme("world");
    shell.world.setLightLevel(100);
    // Frame the Office for the baseline so the meeting-room work is visible in
    // every viewport's shot (adhoc: PR #47). Retakes world-*.png baselines.
    shell.world.focusLandmark("office", { move: false });
  });
  // The camera intentionally eases from its spawn position. Let that bounded
  // interpolation converge before pausing so the WebGL baseline does not
  // depend on how many startup frames a busy CI host happened to paint.
  await page.waitForTimeout(1600);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.closeLandmark();
    shell.world.setPaused(true);
    const toast = shell.querySelector("[data-world-toast]");
    if (toast) toast.dataset.open = "false";
  });
}

async function openOfficeForVisual(page) {
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setPaused(false);
    shell.world.player.position.set(11, 0.38, -17.7);
  });
  await page.locator("[data-world-office-enter]").click();
  await expect(page.locator("[data-world-office-lobby]")).toBeVisible();
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setPaused(true);
  });
}

async function dragThumbstick(
  page,
  locator,
  { x = 0, y = -1, durationMs = 300 } = {},
) {
  const box = await locator.boundingBox();
  expect(box).not.toBeNull();
  const client = await page.context().newCDPSession(page);
  const start = {
    x: Math.round(box.x + box.width / 2),
    y: Math.round(box.y + box.height / 2),
    id: 1,
  };
  const travel = Math.max(1, Math.min(box.width, box.height) * 0.3);
  const point = {
    x: Math.round(start.x + x * travel),
    y: Math.round(start.y + y * travel),
    id: 1,
  };
  await client.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [start],
  });
  await client.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: [point],
  });
  await page.waitForTimeout(durationMs);
  await client.send("Input.dispatchTouchEvent", {
    type: "touchEnd",
    touchPoints: [],
  });
  await client.detach();
}

async function installFocusMusicAudioProbe(page) {
  await page.addInitScript(() => {
    const probe = {
      created: [],
      intervalRegistrations: 0,
    };
    const nativeSetInterval = window.setInterval.bind(window);
    window.setInterval = (...args) => {
      probe.intervalRegistrations += 1;
      return nativeSetInterval(...args);
    };
    class FocusMusicAudio {
      constructor(src) {
        this.src = String(src || "");
        this.currentTime = 0;
        this.loop = false;
        this.muted = false;
        this.paused = true;
        this.preload = "";
        this.readyState = 4;
        this.volume = 1;
        this.playCalls = 0;
        this.pauseCalls = 0;
        this.loadCalls = 0;
        this.listeners = new Map();
        probe.created.push(this);
      }
      addEventListener(type, listener) {
        const listeners = this.listeners.get(type) || [];
        listeners.push(listener);
        this.listeners.set(type, listeners);
      }
      removeEventListener(type, listener) {
        this.listeners.set(
          type,
          (this.listeners.get(type) || []).filter(
            (candidate) => candidate !== listener,
          ),
        );
      }
      load() {
        this.loadCalls += 1;
      }
      pause() {
        this.paused = true;
        this.pauseCalls += 1;
      }
      async play() {
        this.paused = false;
        this.playCalls += 1;
      }
    }
    Object.defineProperty(window, "Audio", {
      configurable: true,
      writable: true,
      value: FocusMusicAudio,
    });
    window.__forkmeshFocusMusicProbe = probe;
  });
}

async function focusMusicProbeSnapshot(page) {
  return page.evaluate(() => {
    const probe = window.__forkmeshFocusMusicProbe;
    return {
      intervalRegistrations: probe.intervalRegistrations,
      created: probe.created.map((audio) => ({
        src: audio.src,
        currentTime: audio.currentTime,
        loop: audio.loop,
        muted: audio.muted,
        paused: audio.paused,
        preload: audio.preload,
        volume: audio.volume,
        playCalls: audio.playCalls,
        pauseCalls: audio.pauseCalls,
        loadCalls: audio.loadCalls,
      })),
    };
  });
}

async function chooseFocusMusicTrack(page, trackId) {
  await page
    .locator(`[data-world-focus-track='${trackId}']`)
    .evaluate((control) => {
      const input = control.matches("input[type='radio']")
        ? control
        : control.querySelector("input[type='radio']");
      if (!input) throw new Error("focus music card has no radio control");
      if (!input.checked) input.click();
    });
}

async function focusMusicTrackIsChecked(page, trackId) {
  return page
    .locator(`[data-world-focus-track='${trackId}']`)
    .evaluate((control) => {
      const input = control.matches("input[type='radio']")
        ? control
        : control.querySelector("input[type='radio']");
      return input?.checked === true;
    });
}

test("enhanced Town Square starts in WebGL and keeps keyboard navigation", async ({
  page,
}) => {
  await prepareWorldPage(page, "desktop-a");
  await waitForWorld(page);

  await expect(page.locator("[data-world-root]")).toBeVisible();
  await expect(page.locator("[data-world-canvas-wrap] canvas")).toHaveCount(1);
  await expect(page.locator(".world-webgl-fallback")).toHaveCount(0);
  await expect(page.locator("#world-information")).toHaveCount(1);
  await expect(page.locator("[data-world-loading]")).toHaveCount(0);
  await expect(page.locator(".world-arrival-card")).toHaveCount(0);

  await page.locator(".world-skip-link").focus();
  await page.keyboard.press("Enter");
  await expect(page).toHaveURL(/#world-information$/);
  await expect(page.locator("#world-information")).toBeFocused();

  await page.keyboard.press("Escape");
  const before = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  await page.keyboard.down("w");
  await page.waitForTimeout(220);
  await page.keyboard.up("w");
  const after = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  expect(Math.hypot(after.x - before.x, after.z - before.z)).toBeGreaterThan(
    0.05,
  );
});

test("landmark tree clusters stay local while arrival faces inward", async ({
  page,
}) => {
  await prepareWorldPage(page, "world-tree-field");
  await waitForWorld(page);

  const state = await page.locator("forkmesh-world").evaluate((shell) => {
    const scene = shell.world.scene;
    const treeField = scene.getObjectByName("world-tree-field");
    const trees = treeField?.children.map((tree) => ({
      x: tree.position.x,
      z: tree.position.z,
      radius: Math.hypot(tree.position.x, tree.position.z),
    })) || [];
    let palePlaza = false;
    let fountainPool = false;
    scene.traverse((object) => {
      const geometry = object.geometry;
      if (geometry?.type !== "CylinderGeometry") return;
      const { radiusTop, radiusBottom, height } = geometry.parameters || {};
      if (
        Math.abs(Number(radiusTop) - 16.5) < 0.001 &&
        Math.abs(Number(radiusBottom) - 17.4) < 0.001
      ) {
        palePlaza = true;
      }
      if (
        Math.abs(Number(radiusTop) - 4.4) < 0.001 &&
        Math.abs(Number(radiusBottom) - 4.4) < 0.001 &&
        Math.abs(Number(height) - 0.16) < 0.001
      ) {
        fountainPool = true;
      }
    });

    shell.world.setSpawn({
      x: 0,
      y: 0.38,
      z: 30,
      heading: 0,
      space: "town-square",
    });
    shell.world.setRemotePlayers([
      {
        id: "arrival-facing-peer",
        name: "Arrival peer",
        x: 2,
        y: 0.38,
        z: 30,
        heading: 0,
        accountStatus: "Guest",
      },
    ]);
    const remote = scene.getObjectByName("avatar:arrival-facing-peer");
    return {
      palePlaza,
      fountainPool,
      signHeading: scene.getObjectByName("world-arrival-plaque")?.rotation.y,
      playerHeading: shell.world.player.rotation.y,
      remoteHeading: remote?.userData?.targetHeading,
      treeCount: trees.length,
      radii: trees.map((tree) => tree.radius),
      quadrants: [0, 1, 2, 3].map(
        (quadrant) =>
          trees.filter((tree) => {
            const actual =
              tree.x >= 0
                ? tree.z >= 0 ? 0 : 3
                : tree.z >= 0 ? 1 : 2;
            return actual === quadrant;
          }).length,
      ),
      arrivalTrees: trees.filter(
        (tree) =>
          tree.x >= -10.8 &&
          tree.x <= 10.8 &&
          tree.z >= 14 &&
          tree.z <= 32.4,
      ).length,
      cabinetTrees: trees.filter(
        (tree) =>
          tree.x >= 24 &&
          tree.x <= 52 &&
          tree.z >= -14 &&
          tree.z <= 14,
      ).length,
    };
  });

  expect(state.palePlaza).toBe(false);
  expect(state.fountainPool).toBe(true);
  expect(state.signHeading).toBeCloseTo(Math.PI, 5);
  expect(state.playerHeading).toBeCloseTo(Math.PI, 5);
  expect(state.remoteHeading).toBeCloseTo(Math.PI, 5);
  expect(state.treeCount).toBeGreaterThan(0);
  expect(state.treeCount).toBeLessThanOrEqual(33);
  expect(Math.max(...state.radii)).toBeLessThan(80);
  expect(state.arrivalTrees).toBe(0);
  expect(state.cabinetTrees).toBe(0);
});

test("an expired persisted session stays guest-only without private request fanout or Three.js material warnings", async ({
  page,
}) => {
  const privateRequests = [];
  const materialWarnings = [];
  page.on("request", (request) => {
    const path = new URL(request.url()).pathname;
    if (
      path === "/api/world/media/spaces" ||
      path === "/api/rewards/pending" ||
      path === "/api/notifications" ||
      path.startsWith("/api/orgs/")
    ) {
      privateRequests.push(path);
    }
  });
  page.on("console", (message) => {
    if (
      message.type() === "warning" &&
      message.text().includes("THREE.Material")
    ) {
      materialWarnings.push(message.text());
    }
  });
  await prepareWorldPage(page, "expired-session", {
    session: {
      nodeName: "expired-user",
      sessionToken: "expired-session-token",
    },
    ticketAuthenticated: false,
  });
  await waitForWorld(page);

  await expect(page.locator("[data-world-canvas-wrap] canvas")).toHaveCount(1);
  await expect(page.locator(".world-webgl-fallback")).toHaveCount(0);
  expect(privateRequests).toEqual([]);
  expect(materialWarnings).toEqual([]);
});

test("double-clicking the ground dashes the avatar to that spot", async ({
  page,
}) => {
  await prepareWorldPage(page, "desktop-dblclick-dash");
  await waitForWorld(page);

  const canvas = page.locator("[data-world-canvas-wrap] canvas");
  const box = await canvas.boundingBox();
  expect(box).not.toBeNull();

  const before = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  // Well away from the avatar, still on the plaza floor.
  await page.mouse.dblclick(
    Math.round(box.x + box.width * 0.3),
    Math.round(box.y + box.height * 0.4),
  );
  await page.waitForTimeout(400);
  const after = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  // A dash covers far more ground in 400ms than the walking cap would.
  expect(Math.hypot(after.x - before.x, after.z - before.z)).toBeGreaterThan(3);
});

test("desktop camera uses visible-cursor drag look, capped movement acceleration, and wheel zoom", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await prepareWorldPage(page, "desktop-drag-controls");
  await waitForWorld(page);

  const canvas = page.locator("[data-world-canvas-wrap] canvas");
  const box = await canvas.boundingBox();
  expect(box).not.toBeNull();
  const centre = {
    x: Math.round(box.x + box.width / 2),
    y: Math.round(box.y + box.height / 2),
  };

  const beforeGroundClick = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getPosition(),
  );
  await page.mouse.click(centre.x, centre.y);
  await page.waitForTimeout(160);
  const afterGroundClick = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getPosition(),
  );
  expect(
    Math.hypot(
      afterGroundClick.x - beforeGroundClick.x,
      afterGroundClick.z - beforeGroundClick.z,
    ),
  ).toBeLessThan(0.01);
  await expect(canvas).toHaveCSS("cursor", "grab");
  expect(await page.evaluate(() => document.pointerLockElement)).toBeNull();

  const beforeLook = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getCameraState(),
  );
  await page.mouse.move(centre.x, centre.y);
  await page.mouse.down({ button: "left" });
  await expect(canvas).toHaveCSS("cursor", "grabbing");
  await page.mouse.move(centre.x + 120, centre.y + 35, { steps: 4 });
  await page.mouse.up({ button: "left" });
  const afterLook = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getCameraState(),
  );
  expect(afterLook.yaw).not.toBeCloseTo(beforeLook.yaw, 4);
  expect(afterLook.pitch).not.toBeCloseTo(beforeLook.pitch, 4);
  expect(afterLook.yaw).toBeLessThan(beforeLook.yaw);
  expect(afterLook.pitch).toBeGreaterThan(beforeLook.pitch);
  expect(afterLook.dragging).toBe(false);
  expect(afterLook.pointerLocked).toBe(false);
  expect(await page.evaluate(() => document.pointerLockElement)).toBeNull();
  await expect(canvas).toHaveCSS("cursor", "grab");

  const beforeMove = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getPosition(),
  );
  await page.keyboard.down("ArrowUp");
  await page.waitForTimeout(120);
  const earlyMovement = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getMovementState(),
  );
  await page.waitForTimeout(760);
  const acceleratedMovement = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getMovementState(),
  );
  await page.keyboard.up("ArrowUp");
  const afterMove = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getPosition(),
  );
  expect(acceleratedMovement.speed).toBeGreaterThan(earlyMovement.speed);
  expect(acceleratedMovement.speed).toBeLessThanOrEqual(
    acceleratedMovement.maxSpeed,
  );
  const releasedMovement = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getMovementState(),
  );
  expect(releasedMovement.speed).toBe(releasedMovement.baseSpeed);
  expect(releasedMovement.keyboardActive).toBe(false);
  const displacement = {
    x: afterMove.x - beforeMove.x,
    z: afterMove.z - beforeMove.z,
  };
  const cameraForward = {
    x: -Math.sin(afterLook.yaw),
    z: -Math.cos(afterLook.yaw),
  };
  expect(
    displacement.x * cameraForward.x + displacement.z * cameraForward.z,
  ).toBeGreaterThan(0.05);

  await page.keyboard.down("w");
  await page.waitForTimeout(180);
  await page.evaluate(() => window.dispatchEvent(new Event("blur")));
  const blurredMovement = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getMovementState(),
  );
  expect(blurredMovement.speed).toBe(blurredMovement.baseSpeed);
  expect(blurredMovement.keyboardActive).toBe(false);
  await page.keyboard.up("w");

  await page.mouse.move(centre.x, centre.y);
  const initialZoom = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getCameraState().zoom,
  );
  await page.mouse.wheel(0, 480);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getCameraState().zoom,
    ),
  ).toBeGreaterThan(initialZoom);
  const zoomedOut = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getCameraState().zoom,
  );
  await page.mouse.wheel(0, -960);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getCameraState().zoom,
    ),
  ).toBeLessThan(zoomedOut);
});

test("refresh restores one bounded identity-local position without private history", async ({
  page,
}) => {
  const serverArrival = {
    id: "position-restore",
    name: "visitor",
    status: "exploring",
    x: -8.1,
    y: 0.38,
    z: 30,
    yaw: 0,
    space: "town-square",
  };
  await prepareWorldPage(page, "position-restore", {
    worldSocketHandler(socket, socketId) {
      socket.send(JSON.stringify({
        type: "welcome",
        id: socketId,
        self: serverArrival,
        peers: [],
      }));
    },
  });
  await waitForWorld(page);

  await page.locator("forkmesh-world").evaluate((shell) => {
    // The space station is parked in the works-in-progress barn, so its floor
    // is the same walkable 0.38 as the Town Square.
    const position = {
      x: 16.3,
      y: 0.38,
      z: 66.5,
      heading: 1.2,
      space: "space-station",
      moving: false,
      activity: "private/repository?token=must-not-persist",
    };
    shell.currentSpace = position.space;
    shell.world.setSpawn(position);
    shell.handleMovement(position);
  });
  const storedBeforeRefresh = await page.evaluate(() => {
    const keys = Object.keys(localStorage).filter((key) =>
      key.startsWith("forkmesh.world.position.v1."),
    );
    return {
      keys,
      record: JSON.parse(localStorage.getItem(keys[0]) || "{}"),
    };
  });
  expect(storedBeforeRefresh.keys).toHaveLength(1);
  expect(Object.keys(storedBeforeRefresh.record).sort()).toEqual(
    ["heading", "space", "updatedAt", "x", "y", "z"].sort(),
  );
  expect(JSON.stringify(storedBeforeRefresh.record)).not.toContain("private");
  expect(JSON.stringify(storedBeforeRefresh.record)).not.toContain("token");

  await page.reload();
  await waitForWorld(page);
  const restored = await page.locator("forkmesh-world").evaluate((shell) => ({
    position: shell.world.getPosition(),
    currentSpace: shell.currentSpace,
    storedRecords: Object.keys(localStorage).filter((key) =>
      key.startsWith("forkmesh.world.position.v1."),
    ).length,
  }));
  expect(restored.currentSpace).toBe("space-station");
  expect(restored.position.space).toBe("space-station");
  expect(restored.position.x).toBeCloseTo(16.3, 3);
  expect(restored.position.y).toBeCloseTo(0.38, 3);
  expect(restored.position.z).toBeCloseTo(66.5, 3);
  expect(restored.position.heading).toBeCloseTo(1.2, 3);
  expect(restored.storedRecords).toBe(1);
});

test("busy walking stays connected while movement frames remain within the soft budget", async ({
  page,
}) => {
  const frames = [];
  let socketCount = 0;
  let rateDisconnects = 0;
  let rateWindowStartedAt = 0;
  let rateWindowCount = 0;
  await prepareWorldPage(page, "busy-movement", {
    worldSocketHandler(socket, socketId) {
      socketCount += 1;
      socket.onMessage((raw) => {
        const now = performance.now();
        if (!rateWindowStartedAt || now - rateWindowStartedAt >= 1000) {
          rateWindowStartedAt = now;
          rateWindowCount = 0;
        }
        rateWindowCount += 1;
        const frame = JSON.parse(String(raw));
        frames.push({ frame, at: now });
        if (rateWindowCount > 4) {
          rateDisconnects += 1;
          void socket.close({ code: 1008, reason: "soft rate budget" });
        }
      });
      socket.send(JSON.stringify({
        type: "welcome",
        id: socketId,
        self: {
          id: socketId,
          name: "visitor",
          status: "exploring",
          x: -8.1,
          y: 0.38,
          z: 30,
          yaw: 0,
          space: "town-square",
        },
        peers: [],
      }));
    },
  });
  await waitForWorld(page);

  await page.keyboard.down("ArrowUp");
  await page.waitForTimeout(3300);
  await page.keyboard.up("ArrowUp");
  await page.waitForTimeout(350);

  const connection = await page.locator("forkmesh-world").evaluate((shell) => ({
    readyState: shell.socket?.readyState,
    openState: WebSocket.OPEN,
    peerId: shell.serverPeerId,
  }));
  const movementFrames = frames.filter(({ frame }) => frame.type === "move");
  const activeFrames = movementFrames.filter(({ frame }) => frame.moving);
  expect(rateDisconnects).toBe(0);
  expect(socketCount).toBe(1);
  expect(connection.readyState).toBe(connection.openState);
  expect(connection.peerId).toBe("busy-movement");
  expect(activeFrames.length).toBeGreaterThanOrEqual(2);
  expect(activeFrames.length).toBeLessThanOrEqual(4);
  expect(movementFrames.at(-1).frame.moving).toBe(false);
  expect(movementFrames.length).toBeLessThanOrEqual(6);
});

test("local diagnostics report renderer and existing socket state without new telemetry", async ({
  page,
}) => {
  let socketCount = 0;
  await prepareWorldPage(page, "world-diagnostics", {
    worldSocketHandler(socket, socketId) {
      socketCount += 1;
      socket.send(JSON.stringify({
        type: "welcome",
        id: socketId,
        peers: [],
      }));
    },
  });
  await waitForWorld(page);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.lastMovementSentAt = performance.now();
    shell.queueMovementPresence({
      x: 1,
      y: 0.38,
      z: 1,
      heading: 0,
      moving: true,
    });
    shell.queueMovementPresence({
      x: 2,
      y: 0.38,
      z: 2,
      heading: 0,
      moving: true,
    });
    shell.sendPresence({ type: "presence" });
    shell.sendPresence({ type: "presence" });
  });
  await page.waitForTimeout(1150);

  const diagnostics = page.locator("[data-world-diagnostics]");
  await expect(diagnostics.locator("summary")).toContainText("FPS");
  await expect(diagnostics.locator("summary")).toContainText("socket online");
  await expect(diagnostics.locator("summary")).toContainText("1 peer");
  await expect(diagnostics.locator("summary")).toContainText("v0.7.0");
  await diagnostics.locator("summary").click();
  await expect(diagnostics).toHaveAttribute("open", "");
  await expect(diagnostics).toContainText("ms/frame");
  await expect(diagnostics).toContainText("triangles");
  await expect(diagnostics).toContainText("Socket frames");
  await expect(diagnostics).toContainText("coalesced");
  await expect(diagnostics).toContainText("dddddddddddd");
  await expect(diagnostics).toContainText("No diagnostics are transmitted");

  const snapshot = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.lastDiagnosticsSnapshot,
  );
  expect(snapshot.renderer.fps).toBeGreaterThan(0);
  expect(snapshot.renderer.frameTimeMs).toBeGreaterThan(0);
  expect(snapshot.renderer.calls).toBeGreaterThan(0);
  expect(snapshot.renderer.triangles).toBeGreaterThan(0);
  expect(snapshot.connection).toMatchObject({
    state: "online",
    peers: 1,
    reconnects: 0,
  });
  expect(snapshot.traffic.inboundFrames).toBeGreaterThanOrEqual(1);
  expect(snapshot.traffic.outboundFrames).toBeGreaterThanOrEqual(1);
  expect(snapshot.queues.movementCoalesced).toBeGreaterThanOrEqual(1);
  expect(snapshot.queues.profileCoalesced).toBeGreaterThanOrEqual(1);
  expect(snapshot.build).toEqual({
    version: "0.7.0",
    revision: "d".repeat(40),
  });
  expect(Object.keys(snapshot).sort()).toEqual(
    ["build", "connection", "queues", "renderer", "traffic"].sort(),
  );
  expect(JSON.stringify(snapshot)).not.toContain("127.0.0.1");
  expect(JSON.stringify(snapshot)).not.toContain("/world/");
  expect(socketCount).toBe(1);
});

test("the topbar has no clock or emote actions and local light level survives movement without becoming presence data", async ({
  page,
}) => {
  await prepareWorldPage(page, "local-light-level");
  await waitForWorld(page);

  await expect(page.locator("[data-world-clock]")).toHaveCount(0);
  await expect(page.locator("[data-world-emote]")).toHaveCount(0);
  const initial = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getEnvironmentState(),
  );
  expect(initial.theme).toBe("world");
  expect(initial.lightLevel).toBe(100);

  await page.keyboard.down("ArrowRight");
  await page.waitForTimeout(320);
  await page.keyboard.up("ArrowRight");
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.travelToRegion("east");
  });
  const afterMovement = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getEnvironmentState(),
  );
  expect(afterMovement).toEqual(initial);

  await page.locator("[data-world-settings-open]").first().click();
  const light = page.locator("[data-world-light-level]");
  await light.fill("65");
  await expect(page.locator("[data-world-light-level-output]")).toHaveText(
    "65%",
  );
  const adjusted = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getEnvironmentState(),
  );
  expect(adjusted.lightLevel).toBe(65);
  expect(adjusted.sunIntensity).toBeLessThan(initial.sunIntensity);
  expect(adjusted.sunPosition).toEqual(initial.sunPosition);
  const stored = await page.evaluate(() =>
    JSON.parse(localStorage.getItem("forkmesh.world.settings.v1") || "{}"),
  );
  expect(stored.lightLevel).toBe(65);
  const publicIdentityState = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.identity,
  );
  expect(publicIdentityState.lightLevel).toBeUndefined();
});

test("Unicode emoji status is local-persisted, coalesced, and available to every avatar label", async ({
  page,
  context,
}) => {
  const frames = [];
  await prepareWorldPage(page, "emoji-status-owner", {
    worldSocketHandler(socket, socketId) {
      socket.onMessage((raw) => frames.push(JSON.parse(String(raw))));
      socket.send(JSON.stringify({
        type: "welcome",
        id: socketId,
        self: {
          id: socketId,
          name: "owner",
          status: "available",
          x: -8.1,
          y: 0.38,
          z: 30,
          yaw: 0,
          space: "town-square",
        },
        peers: [
          {
            id: "peer-status",
            name: "Peer",
            status: "available",
            statusEmoji: "🚀",
            statusNote: "shipping",
            x: 3,
            y: 0.38,
            z: 4,
            yaw: 0,
            space: "town-square",
          },
        ],
      }));
    },
  });
  await waitForWorld(page);
  await expect(
    page.locator('[data-player-label="peer-status"]'),
  ).toHaveAttribute("aria-label", "Peer, public status 🚀 shipping");
  const peerStatus = await page.locator("forkmesh-world").evaluate((shell) => {
    const avatar = shell.world.scene.getObjectByName("avatar:peer-status");
    const sprite = avatar?.getObjectByName("forkmesh-avatar-emoji-status");
    return {
      emoji: avatar?.userData?.statusEmoji,
      note: avatar?.userData?.statusNote,
      spriteVisible: Boolean(sprite?.visible),
    };
  });
  expect(peerStatus).toEqual({
    emoji: "🚀",
    note: "shipping",
    spriteVisible: false,
  });

  const firstChangeFrameIndex = frames.length;
  const pageCount = context.pages().length;
  await page.locator("[data-world-settings-open]").first().click();
  await page.locator(".world-emoji-picker").evaluate((picker) => {
    picker.open = true;
  });
  await page.locator("[data-world-emoji-category]").selectOption("gestures");
  await page.getByRole("button", {
    name: "Use 🧑‍💻 as public status",
  }).click();
  const note = page.locator("[data-world-status-note]");
  await note.fill("coding");
  await note.dispatchEvent("change");
  await expect(page.locator("[data-world-status-preview]")).toHaveText(
    "🧑‍💻 coding",
  );
  await expect.poll(() =>
    frames.filter(
      (frame) =>
        frame.type === "presence" &&
        frame.statusEmoji === "🧑‍💻" &&
        frame.statusNote === "coding",
    ).length,
  ).toBe(1);
  const statusFrames = frames
    .slice(firstChangeFrameIndex)
    .filter(
      (frame) => frame.type === "presence" && Boolean(frame.statusEmoji),
    );
  expect(statusFrames.length).toBeLessThanOrEqual(2);
  expect(
    frames
      .filter((frame) => frame.type === "move")
      .some((frame) => "statusEmoji" in frame || "statusNote" in frame),
  ).toBe(false);

  const saved = await page.evaluate(() =>
    JSON.parse(localStorage.getItem("forkmesh.world.settings.v1") || "{}"),
  );
  expect(saved.statusEmoji).toBe("🧑‍💻");
  expect(saved.statusNote).toBe("coding");
  const ownStatus = await page.locator("forkmesh-world").evaluate((shell) => ({
    emoji: shell.world.player.userData.statusEmoji,
    note: shell.world.player.userData.statusNote,
    spriteVisible: Boolean(
      shell.world.player.getObjectByName("forkmesh-avatar-emoji-status")
        ?.visible,
    ),
  }));
  expect(ownStatus).toEqual({
    emoji: "🧑‍💻",
    note: "coding",
    spriteVisible: false,
  });

  const codingFramesBeforeDuplicate = frames.filter(
    (frame) =>
      frame.type === "presence" &&
      frame.statusEmoji === "🧑‍💻" &&
      frame.statusNote === "coding",
  ).length;
  await note.dispatchEvent("change");
  await page.waitForTimeout(450);
  expect(frames.filter(
    (frame) =>
      frame.type === "presence" &&
      frame.statusEmoji === "🧑‍💻" &&
      frame.statusNote === "coding",
  )).toHaveLength(codingFramesBeforeDuplicate);

  await note.fill("two words");
  await note.dispatchEvent("change");
  await expect(note).toHaveValue("coding");
  await expect(page.locator("[data-world-toast]")).toContainText(
    "must be one word",
  );
  expect(context.pages()).toHaveLength(pageCount);
  expect(new URL(page.url()).pathname).toBe("/world/");
});

test("two live clients synchronize movement without leaking disabled badge fields", async ({
  context,
}) => {
  const observer = await context.newPage();
  await prepareWorldPage(observer, "observer");
  await waitForWorld(observer);

  await observer.evaluate((settings) => {
    localStorage.setItem(
      "forkmesh.world.settings.v1",
      JSON.stringify(settings),
    );
  }, PRIVATE_SETTINGS);

  const privateClient = await context.newPage();
  await prepareWorldPage(privateClient, "private-client");
  await waitForWorld(privateClient);

  await observer.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.localPeers?.size > 0;
  });
  let peer = await observer.locator("forkmesh-world").evaluate((shell) =>
    [...shell.localPeers.values()][0],
  );
  expect(peer.name).toBe("Private visitor");
  expect(peer.flag).toBe("◌");
  expect(peer.browser).toBe("Hidden");
  expect(peer.os).toBe("Hidden");
  expect(peer.localTime).toBe("");
  expect(peer.nodes).toEqual([]);
  expect(peer.status).toBe("hidden");

  await privateClient.locator("forkmesh-world").evaluate((shell) => {
    shell.handleMovement({
      x: 11.75,
      y: 0.38,
      z: -4.25,
      heading: 0.6,
      activity: "private/repository?token=must-not-cross",
    });
  });
  await observer.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    const peer = [...(shell?.localPeers?.values?.() || [])][0];
    return peer?.x === 11.75 && peer?.z === -4.25;
  });
  peer = await observer.locator("forkmesh-world").evaluate((shell) =>
    [...shell.localPeers.values()][0],
  );
  expect(peer.activity).toBe("online");
  expect(JSON.stringify(peer)).not.toContain("private/repository");
  expect(JSON.stringify(peer)).not.toContain("must-not-cross");
});

test("landscape thumbstick remains visible and moves the avatar", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 844, height: 390 },
    hasTouch: true,
    isMobile: true,
  });
  const page = await context.newPage();
  await prepareWorldPage(page, "landscape-touch");
  await waitForWorld(page);

  const control = page.locator("[data-world-thumbstick]");
  await expect(control).toBeVisible();
  const canvasBox = await page.locator("[data-world-canvas-wrap]").boundingBox();
  expect(canvasBox).not.toBeNull();
  expect(canvasBox.height).toBeLessThanOrEqual(390);

  const before = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  await dragThumbstick(page, control);
  const after = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  expect(Math.hypot(after.x - before.x, after.z - before.z)).toBeGreaterThan(
    0.05,
  );
  await context.close();
});

test("thumbstick motion is continuous, proportional, and recenters on release", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
  });
  const page = await context.newPage();
  await prepareWorldPage(page, "analog-thumbstick");
  await waitForWorld(page);

  await expect(page.locator(".world-controls")).toHaveCount(0);
  await expect(page.locator("[data-move]")).toHaveCount(0);
  const thumbstick = page.locator("[data-world-thumbstick]");
  const handle = page.locator("[data-world-thumbstick-handle]");
  await expect(thumbstick).toBeVisible();
  const box = await thumbstick.boundingBox();
  const handleBox = await handle.boundingBox();
  expect(box).not.toBeNull();
  expect(handleBox).not.toBeNull();

  const centre = {
    x: Math.round(box.x + box.width / 2),
    y: Math.round(box.y + box.height / 2),
  };
  const travel =
    Math.min(box.width, box.height) / 2 -
    Math.max(handleBox.width, handleBox.height) / 2 -
    5;
  const point = (strength) => ({
    x: centre.x,
    y: Math.round(centre.y - travel * strength),
    id: 1,
  });
  const client = await page.context().newCDPSession(page);
  const before = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );

  await client.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [{ ...centre, id: 1 }],
  });
  await client.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: [point(0.5)],
  });
  await page.waitForTimeout(300);
  const partialState = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getMovementState(),
  );
  const partialPosition = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getPosition(),
  );

  await client.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: [point(1)],
  });
  await page.waitForTimeout(300);
  const fullState = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getMovementState(),
  );
  const fullPosition = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  await client.send("Input.dispatchTouchEvent", {
    type: "touchEnd",
    touchPoints: [],
  });

  expect(partialState.touchStrength).toBeGreaterThan(0.3);
  expect(partialState.touchStrength).toBeLessThan(0.7);
  expect(fullState.touchStrength).toBeGreaterThan(0.9);
  const partialDistance = Math.hypot(
    partialPosition.x - before.x,
    partialPosition.z - before.z,
  );
  const fullDistance = Math.hypot(
    fullPosition.x - partialPosition.x,
    fullPosition.z - partialPosition.z,
  );
  expect(fullDistance).toBeGreaterThan(partialDistance * 1.35);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getMovementState().touchActive,
    ),
  ).toBe(false);
  await expect(thumbstick).toHaveAttribute("data-active", "false");
  expect(
    await handle.evaluate((element) =>
      getComputedStyle(element).getPropertyValue("--thumb-y").trim(),
    ),
  ).toBe("0px");
  await client.detach();
  await context.close();
});

test("one-finger look and two-finger pinch use distinct bounded gestures", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
  });
  const page = await context.newPage();
  await prepareWorldPage(page, "mobile-full-range-pinch");
  await waitForWorld(page);

  const canvas = page.locator("[data-world-canvas-wrap] canvas");
  const box = await canvas.boundingBox();
  expect(box).not.toBeNull();
  const centreX = Math.round(box.x + box.width / 2);
  const centreY = Math.round(box.y + box.height * 0.62);
  const client = await page.context().newCDPSession(page);
  const beforeLook = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getCameraState(),
  );
  await client.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [{ x: centreX, y: centreY, id: 7 }],
  });
  await client.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: [{ x: centreX + 80, y: centreY + 30, id: 7 }],
  });
  await client.send("Input.dispatchTouchEvent", {
    type: "touchEnd",
    touchPoints: [],
  });
  const afterLook = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getCameraState(),
  );
  expect(afterLook.yaw).toBeLessThan(beforeLook.yaw);
  expect(afterLook.pitch).toBeGreaterThan(beforeLook.pitch);
  expect(afterLook.zoom).toBeCloseTo(beforeLook.zoom, 6);

  const points = (spread) => [
    { x: centreX - spread, y: centreY, id: 1 },
    { x: centreX + spread, y: centreY, id: 2 },
  ];

  await client.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: points(10),
  });
  await client.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: points(185),
  });
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getCameraState(),
    ),
  ).toMatchObject({ zoom: 0.12, minZoom: 0.12 });

  await client.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: points(1),
  });
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getCameraState(),
    ),
  ).toMatchObject({ zoom: 3.2, maxZoom: 3.2 });

  await client.send("Input.dispatchTouchEvent", {
    type: "touchEnd",
    touchPoints: [],
  });
  await client.detach();
  await context.close();
});

test("construction markers distinguish verified live landmarks from unavailable ones", async ({
  page,
}) => {
  await prepareWorldPage(page, "construction-truth");
  await waitForWorld(page);

  const mapMarker = (id) =>
    page.locator(
      `.world-map [data-world-construction-marker="${id}"]`,
    );
  await expect(mapMarker("information")).toBeHidden();
  await expect(mapMarker("routing")).toBeHidden();
  await expect(mapMarker("workshops")).toBeHidden();
  await expect(mapMarker("repositories")).toBeHidden();
  await expect(mapMarker("fediverse")).toBeHidden();
  await expect(mapMarker("events")).toBeHidden();
  await expect(mapMarker("organizations")).toBeVisible();
  await expect(mapMarker("security")).toBeVisible();

  await expect(mapMarker("organizations")).toHaveAttribute(
    "aria-label",
    /Under construction:.*organization directory integration/i,
  );

  await page
    .locator('.world-map [data-world-landmark="organizations"]')
    .click();
  const detail = page.locator("[data-world-detail]");
  await expect(detail).toBeVisible();
  await expect(detail.getByRole("heading", { name: "Organization quarter" }))
    .toBeVisible();
  await expect(
    detail.locator(
      '[data-world-construction-marker="organizations"]',
    ),
  ).toBeVisible();
});

test("failed live checks fail closed to construction without blocking navigation", async ({
  page,
}) => {
  await prepareWorldPage(page, "construction-unavailable", {
    unavailablePaths: [
      "/api/repo/forkmesh/forkmesh/mirrors",
      "/api/repositories",
      "/api/world/fediverse",
      "/api/world/events",
    ],
  });
  await waitForWorld(page);

  for (const id of ["repositories", "fediverse", "events"]) {
    const marker = page.locator(
      `.world-map [data-world-construction-marker="${id}"]`,
    );
    await expect(marker).toBeVisible();
    await expect(marker).toHaveAttribute("aria-label", /^Under construction:/);
  }
  await page.locator('.world-map [data-world-landmark="repositories"]').click();
  await expect(
    page.getByRole("heading", { name: "Repository portals" }),
  ).toBeVisible();
  await expect(
    page.locator(
      '[data-world-detail] [data-world-construction-marker="repositories"]',
    ),
  ).toBeVisible();
});

test("approved instances and local setup stay truthful", async ({
  page,
}) => {
  await prepareWorldPage(page, "truthful-panels");
  await waitForWorld(page);

  const instanceLayer = await page.locator("forkmesh-world").evaluate((shell) => {
    return (
      shell.world.scene.getObjectByName("approved-federated-instances")
        ?.children?.length ?? -1
    );
  });
  // Each approved relay contributes one tower and one label sprite.
  expect(instanceLayer).toBe(4);

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("information"),
  );
  const localLink = page.locator("[data-world-local-qt-link]");
  await expect(localLink).toHaveAttribute(
    "href",
    "forkmesh://control/cloudflare",
  );
  await expect(
    page.getByText("The hosted World never accepts, proxies, or stores"),
  ).toBeVisible();
  await expect(page.locator("[data-world-detail] input")).toHaveCount(0);
});

test("System Capacity fits one height-scaled bar per populated D1 table", async ({
  page,
}) => {
  await prepareWorldPage(page, "system-capacity-bars", {
    session: {
      sessionToken: "playwright-admin-session",
      nodeName: "root",
    },
    systemCapacityTables: [
      { name: "users", rowCount: 2 },
      { name: "repositories", rowCount: 27 },
      { name: "world_events", rowCount: 4096 },
    ],
  });
  await waitForWorld(page);

  const capacity = await page.locator("forkmesh-world").evaluate((shell) => {
    const platform = shell.world.scene.getObjectByName(
      "system-capacity-infrastructure",
    );
    const tableLayer = shell.world.scene.getObjectByName(
      "system-capacity-database-tables",
    );
    const bars = [];
    tableLayer?.traverse((object) => {
      if (String(object.name || "").startsWith("system-capacity-table:")) {
        bars.push({
          name: object.userData.tableName,
          rowCount: object.userData.rowCount,
          height: object.geometry.parameters.height,
          x: object.position.x,
          z: object.position.z,
        });
      }
    });
    return {
      platformName: platform?.name || "",
      visibleTableCount: platform?.userData.visibleTableCount,
      legend: Boolean(
        shell.world.scene.getObjectByName("system-capacity-table-legend"),
      ),
      bars,
    };
  });

  expect(capacity.platformName).toBe("system-capacity-infrastructure");
  expect(capacity.visibleTableCount).toBe(3);
  expect(capacity.legend).toBe(true);
  expect(capacity.bars.map((bar) => bar.name).sort()).toEqual([
    "repositories",
    "users",
    "world_events",
  ]);
  const byRows = [...capacity.bars].sort(
    (left, right) => left.rowCount - right.rowCount,
  );
  expect(byRows[0].height).toBeLessThan(byRows[1].height);
  expect(byRows[1].height).toBeLessThan(byRows[2].height);
  for (const bar of capacity.bars) {
    expect(Math.abs(bar.x)).toBeLessThanOrEqual(5.75);
    expect(bar.z).toBeGreaterThanOrEqual(-3.45);
    expect(bar.z).toBeLessThanOrEqual(3.35);
  }

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setPaused(true);
    shell.world.camera.position.set(8, 7.5, -14);
    shell.world.camera.lookAt(8, 1.6, -27);
    shell.world.renderer.render(shell.world.scene, shell.world.camera);
  });
  await expect(page).toHaveScreenshot("world-system-capacity.png", {
    animations: "disabled",
    maxDiffPixelRatio: 0.012,
  });
});

test("a stale offline alias cannot erase the live mirrors' flagship pin", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await prepareWorldPage(page, "world-stale-offline-alias", {
    repositoryFixture: { staleOfflineAlias: true },
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "ready";
  });

  const snapshot = await page.locator("forkmesh-world").evaluate((shell) => {
    const alias = shell.repositories.find(
      (record) =>
        record.owner === "forkmesh" &&
        record.name === "forkmesh" &&
        record.source === "organization-alias",
    );
    return {
      alias: alias
        ? {
            commit: alias.commit,
            stateHash: alias.stateHash,
            servingOwner: alias.servingOwner,
            mirrorAliases: alias.mirrorAliases,
          }
        : null,
      mapState: shell.repositoryMapState,
      activeCommit: shell.activeRepository?.commit || "",
    };
  });
  expect(snapshot).toEqual({
    alias: {
      commit: "a".repeat(40),
      stateHash: "c".repeat(64),
      servingOwner: "mirror2",
      mirrorAliases: [
        { owner: "jett", name: "forkmesh" },
        { owner: "mirror2", name: "forkmesh" },
        { owner: "mirror3", name: "forkmesh" },
      ],
    },
    mapState: "ready",
    activeCommit: "a".repeat(40),
  });
});

test("repository portals and the 3D size sunburst use the verified catalog tree", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await prepareWorldPage(page, "world-repository-size-rings", {
    repositoryFixture: { staleOfflineAlias: true },
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "ready";
  });

  const initial = await page.locator("forkmesh-world").evaluate((shell) => {
    const scene = shell.world.scene;
    const segments = [];
    const portals = [];
    const sizeLayer = scene.getObjectByName("repository-3d-size-map");
    scene.traverse((object) => {
      if (String(object.name || "").startsWith("repository-portal:")) {
        portals.push(object.name);
      }
      if (String(object.name || "").startsWith("repository-size-segment-")) {
        segments.push(object.userData.repositorySizeNode);
      }
    });
    return {
      repositories: shell.repositories.map(
        (record) => `${record.owner}/${record.name}`,
      ),
      portals,
      segments,
      portalUuid: sizeLayer?.parent?.uuid,
      sizeMount: sizeLayer?.parent?.name,
      sizeMountRadius: sizeLayer?.parent
        ? Math.hypot(sizeLayer.parent.position.x, sizeLayer.parent.position.z)
        : 0,
      legacyVisible:
        scene.getObjectByName("repository-legacy-file-graph")?.visible,
      relationshipsVisible:
        scene.getObjectByName("repository-entity-layer")?.visible,
    };
  });
  expect(initial.portals).toEqual(["repository-portal:forkmesh/forkmesh"]);
  expect(initial.portals).toHaveLength(initial.repositories.length);
  expect(initial.sizeMount).toBe("repository-portal:forkmesh/forkmesh");
  expect(initial.sizeMountRadius).toBeCloseTo(68, 5);
  expect(initial.legacyVisible).toBe(false);
  expect(initial.relationshipsVisible).toBe(false);
  expect(
    initial.segments.map(({ path, type, size }) => ({ path, type, size })),
  ).toEqual([
    { path: "src", type: "directory", size: 4096 },
    { path: "src/[id]+C++.tsx", type: "file", size: 4096 },
    { path: "README.md", type: "file", size: 1200 },
  ]);
  expect(initial.segments[0].percent).toBeCloseTo((4096 / 5296) * 100, 4);

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.selectRepositorySizeNode({
      owner: "forkmesh",
      name: "forkmesh",
      type: "directory",
      path: "src",
    });
  });
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.activeRepository?.path === "src";
  });
  const focused = await page.locator("forkmesh-world").evaluate((shell) => {
    const layer = shell.world.scene.getObjectByName("repository-3d-size-map");
    const hub = layer?.getObjectByName("repository-size-map-hub");
    const segment = layer?.getObjectByName("repository-size-segment-0");
    return {
      focus: layer?.userData?.repositorySizeFocus,
      portalUuid: layer?.parent?.uuid,
      hubTarget: hub?.userData?.repositorySizeNode?.targetPath,
      segment: segment?.userData?.repositorySizeNode,
      camera: shell.world.getCameraState(),
    };
  });
  expect(focused.focus).toBe("src");
  expect(focused.portalUuid).toBe(initial.portalUuid);
  expect(focused.hubTarget).toBe("");
  expect(focused.segment).toMatchObject({
    path: "src/[id]+C++.tsx",
    type: "file",
    size: 4096,
    percent: 100,
  });
  expect(focused.camera.yaw).toBeCloseTo(-Math.PI, 5);
  expect(focused.camera.pitch).toBeCloseTo(0.16, 5);
  expect(focused.camera.zoom).toBeLessThanOrEqual(0.55);

  const injectedPortals = await page
    .locator("forkmesh-world")
    .evaluate((shell) => {
      shell.world.updateRepositoryCatalog(
        Array.from({ length: 200 }, (_, index) => ({
          owner: "orbit",
          name: `repo-${index + 1}`,
          liveHost: index % 2 === 0,
          sizeBytes: (index + 1) * 1024,
        })),
        {},
      );
      const portals = [];
      shell.world.scene.traverse((object) => {
        if (String(object.name || "").startsWith("repository-portal:")) {
          portals.push({
            name: object.name,
            radius: Math.hypot(object.position.x, object.position.z),
            faceScale: object.userData.repositoryFace?.scale.x,
            labelVisible: object.userData.repositoryLabel?.visible,
          });
        }
      });
      return portals;
    });
  expect(injectedPortals).toHaveLength(200);
  expect(injectedPortals.map(({ name }) => name)).toContain(
    "repository-portal:orbit/repo-1",
  );
  expect(injectedPortals.map(({ name }) => name)).toContain(
    "repository-portal:orbit/repo-200",
  );
  injectedPortals.forEach(({ radius }) => expect(radius).toBeCloseTo(68, 5));
  injectedPortals.forEach(({ faceScale }) =>
    expect(faceScale).toBeLessThan(0.7),
  );
  expect(injectedPortals.filter(({ labelVisible }) => labelVisible)).toHaveLength(
    0,
  );

  await page.route("**/forkmesh/forkmesh/blob/**", (route) =>
    route.fulfill({
      status: 200,
      contentType: "text/html; charset=utf-8",
      body: "<!doctype html><title>Repository blob</title>",
    }),
  );
  await page.locator("forkmesh-world").evaluate((shell) => {
    setTimeout(() => {
      shell.selectRepositorySizeNode({
        owner: "forkmesh",
        name: "forkmesh",
        type: "file",
        path: "src/[id]+C++.tsx",
      });
    }, 0);
  });
  await page.waitForURL((url) => url.pathname.includes("/blob/"));
  const destination = new URL(page.url());
  expect(decodeURIComponent(destination.pathname)).toBe(
    "/forkmesh/forkmesh/blob/src/[id]+C++.tsx",
  );
  expect(destination.searchParams.get("ref")).toBe("a".repeat(40));
});

test("disagreeing eligible mirrors leave the automatic flagship map unpinned", async ({
  page,
}) => {
  const repositoryReads = [];
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.pathname.startsWith("/api/repo/forkmesh/forkmesh/")) {
      repositoryReads.push(url.pathname);
    }
  });
  await prepareWorldPage(page, "world-conflicting-live-alias", {
    repositoryFixture: { conflictingHealthyAlias: true },
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "unavailable";
  });

  const snapshot = await page.locator("forkmesh-world").evaluate((shell) => {
    const alias = shell.repositories.find(
      (record) =>
        record.owner === "forkmesh" &&
        record.name === "forkmesh" &&
        record.source === "organization-alias",
    );
    return {
      commit: alias?.commit || "",
      stateHash: alias?.stateHash || "",
      activeRepository: shell.activeRepository,
    };
  });
  expect(snapshot).toEqual({
    commit: "",
    stateHash: "",
    activeRepository: null,
  });
  expect(
    repositoryReads.filter((path) => path.endsWith("/tree")),
  ).toHaveLength(0);
});

test("an incomplete healthy-mirror state attestation cannot auto-load the flagship map", async ({
  page,
}) => {
  const repositoryReads = [];
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.pathname.startsWith("/api/repo/forkmesh/forkmesh/")) {
      repositoryReads.push(url.pathname);
    }
  });
  await prepareWorldPage(page, "world-incomplete-live-alias", {
    repositoryFixture: { missingHealthyStateHash: true },
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "unavailable";
  });

  const snapshot = await page.locator("forkmesh-world").evaluate((shell) => {
    const alias = shell.repositories.find(
      (record) =>
        record.owner === "forkmesh" &&
        record.name === "forkmesh" &&
        record.source === "organization-alias",
    );
    return {
      commit: alias?.commit || "",
      stateHash: alias?.stateHash || "",
      activeRepository: shell.activeRepository,
    };
  });
  expect(snapshot).toEqual({
    commit: "a".repeat(40),
    stateHash: "",
    activeRepository: null,
  });
  expect(
    repositoryReads.filter((path) => path.endsWith("/tree")),
  ).toHaveLength(0);
});

test("repository stars toggle in-place through the exact organization alias", async ({
  page,
  context,
}) => {
  const starFixture = { count: 7, starred: false, requests: [] };
  await prepareWorldPage(page, "world-repository-star", {
    session: {
      nodeName: "star-tester",
      sessionToken: "star-session-token",
    },
    repositoryFixture: {},
    repositoryStarFixture: starFixture,
  });
  await openWorldRepositoryExplorer(page);

  const originalURL = page.url();
  const originalPages = context.pages().length;
  const star = page.locator(
    '[data-world-repo-star][data-world-repo-key="forkmesh/forkmesh"]',
  );
  await expect(star).toBeVisible();
  await expect(star).toHaveAttribute("aria-pressed", "false");
  await expect(star.locator("[data-world-repo-star-count]")).toHaveText("7");

  await star.click();
  await expect(star).toHaveAttribute("aria-pressed", "true");
  await expect(star.locator("[data-world-repo-star-count]")).toHaveText("8");

  await star.click();
  await expect(star).toHaveAttribute("aria-pressed", "false");
  await expect(star.locator("[data-world-repo-star-count]")).toHaveText("7");

  expect(page.url()).toBe(originalURL);
  expect(context.pages()).toHaveLength(originalPages);
  expect(
    starFixture.requests.filter((request) => request.method === "GET").length,
  ).toBeGreaterThanOrEqual(1);
  expect(
    starFixture.requests
      .filter((request) => request.method !== "GET")
      .map((request) => ({
        path: request.path,
        method: request.method,
        authorization: request.authorization,
        body: request.body,
      })),
  ).toEqual([
    {
      path: "/api/repo/forkmesh/forkmesh/star",
      method: "POST",
      authorization: "Bearer star-session-token",
      body: {},
    },
    {
      path: "/api/repo/forkmesh/forkmesh/star",
      method: "DELETE",
      authorization: "Bearer star-session-token",
      body: {},
    },
  ]);
  expect(
    starFixture.requests.every(
      (request) =>
        request.path === "/api/repo/forkmesh/forkmesh/star",
    ),
  ).toBe(true);
});

test("signed-out repository star opens the in-world account panel without mutation", async ({
  page,
  context,
}) => {
  const starFixture = { count: 11, starred: false, requests: [] };
  await prepareWorldPage(page, "world-repository-star-guest", {
    repositoryFixture: {},
    repositoryStarFixture: starFixture,
  });
  await openWorldRepositoryExplorer(page);

  const originalURL = page.url();
  const originalPages = context.pages().length;
  const star = page.locator(
    '[data-world-repo-star][data-world-repo-key="forkmesh/forkmesh"]',
  );
  await expect(star).toHaveAttribute("aria-pressed", "false");
  await expect(star.locator("[data-world-repo-star-count]")).toHaveText("11");
  await star.click();

  const account = page.locator("[data-world-account-panel]");
  await expect(account).toHaveAttribute("data-open", "true");
  await expect(account).toHaveAttribute("aria-hidden", "false");
  expect(
    starFixture.requests.filter((request) =>
      ["POST", "DELETE"].includes(request.method),
    ),
  ).toHaveLength(0);
  expect(
    starFixture.requests.filter((request) => request.method === "GET").length,
  ).toBeGreaterThanOrEqual(1);
  expect(page.url()).toBe(originalURL);
  expect(context.pages()).toHaveLength(originalPages);
});

test("camera toggle enters first-person and restores the local player", async ({
  page,
}) => {
  await prepareWorldPage(page, "world-first-person-toggle");
  await waitForWorld(page);

  const toggle = page.locator("[data-world-camera-toggle]");
  await expect(toggle).toBeVisible();
  await expect(toggle).toHaveAttribute("aria-pressed", "false");
  await expect(toggle).toHaveAttribute(
    "aria-label",
    "Enter first-person view",
  );
  await expect(toggle.locator("[data-world-camera-label]")).toHaveText(
    "First person",
  );

  await toggle.click();
  await expect(toggle).toHaveAttribute("aria-pressed", "true");
  await expect(toggle).toHaveAttribute(
    "aria-label",
    "Exit first-person view",
  );
  await expect(toggle.locator("[data-world-camera-label]")).toHaveText(
    "Third person",
  );
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => ({
      camera: shell.world.getCameraState(),
      playerVisible: shell.world.player.visible,
      canvasMode: shell.world.renderer.domElement.dataset.cameraMode,
    })),
  ).toMatchObject({
    camera: { mode: "first-person", firstPerson: true },
    playerVisible: false,
    canvasMode: "first-person",
  });

  await toggle.click();
  await expect(toggle).toHaveAttribute("aria-pressed", "false");
  await expect(toggle).toHaveAttribute(
    "aria-label",
    "Enter first-person view",
  );
  await expect(toggle.locator("[data-world-camera-label]")).toHaveText(
    "First person",
  );
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => ({
      camera: shell.world.getCameraState(),
      playerVisible: shell.world.player.visible,
      canvasMode: shell.world.renderer.domElement.dataset.cameraMode,
    })),
  ).toMatchObject({
    camera: { mode: "third-person", firstPerson: false },
    playerVisible: true,
    canvasMode: "third-person",
  });
});

test("visiting the repository sunburst enters first-person and can exit", async ({
  page,
}) => {
  await prepareWorldPage(page, "world-repository-first-person", {
    repositoryFixture: {},
  });
  await openWorldRepositoryExplorer(page);

  await page.locator("[data-world-repo-scene]").click();
  const toggle = page.locator("[data-world-camera-toggle]");
  await expect(toggle).toHaveAttribute("aria-pressed", "true");
  await expect(toggle).toHaveAttribute(
    "aria-label",
    "Exit first-person view",
  );
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => ({
      camera: shell.world.getCameraState(),
      playerVisible: shell.world.player.visible,
      canvasMode: shell.world.renderer.domElement.dataset.cameraMode,
    })),
  ).toMatchObject({
    camera: { mode: "first-person", firstPerson: true },
    playerVisible: false,
    canvasMode: "first-person",
  });

  await toggle.click();
  await expect(toggle).toHaveAttribute("aria-pressed", "false");
  await expect(toggle).toHaveAttribute(
    "aria-label",
    "Enter first-person view",
  );
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => ({
      mode: shell.world.getCameraState().mode,
      playerVisible: shell.world.player.visible,
    })),
  ).toEqual({ mode: "third-person", playerVisible: true });
});

test("pull requests open and become viewed entirely inside the repository World", async ({
  page,
  context,
}) => {
  const repositoryRequests = [];
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.pathname.startsWith("/api/repo/forkmesh/forkmesh/")) {
      repositoryRequests.push(url);
    }
  });
  await prepareWorldPage(page, "world-pull-review", {
    repositoryFixture: {},
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "ready" &&
      shell?.activeRepository?.owner === "forkmesh" &&
      shell?.activeRepository?.repo === "forkmesh";
  });

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("repositories"),
  );
  await expect(
    page.getByRole("button", { name: /^2 pull requests$/i }),
  ).toBeVisible();
  const originalURL = page.url();
  const originalPages = context.pages().length;
  await page.getByRole("button", { name: /^2 pull requests$/i }).click();
  await expect(
    page.getByRole("heading", { name: "forkmesh/forkmesh pull requests" }),
  ).toBeVisible();
  await expect(page.locator("[data-world-pull-open]")).toHaveCount(2);
  await expect(
    page.locator("[data-world-pull-open][data-world-pull-number='44']"),
  ).toContainText("Review inside the World");

  await page
    .locator("[data-world-pull-open][data-world-pull-number='44']")
    .click();
  await expect(
    page.getByRole("heading", { name: "Pull request #44" }),
  ).toBeVisible();
  await expect(page.locator(".world-pull-file-tree button")).toHaveCount(2);
  await expect(page.locator("[data-world-pull-diff-file]")).toHaveCount(2);
  await expect(page.locator("[data-world-pull-diff]")).toContainText(
    "world-pr-diff-visible",
  );
  await expect(page.locator("[data-world-pull-viewed-summary]")).toHaveText(
    "0 of 2 files viewed",
  );
  expect(page.url()).toBe(originalURL);
  expect(context.pages()).toHaveLength(originalPages);
  await expect(
    page.getByRole("button", { name: /merge pull request/i }),
  ).toHaveCount(0);

  await page.locator("[data-world-pull-diff]").evaluate((element) => {
    element.scrollTop = element.scrollHeight;
    element.dispatchEvent(new Event("scroll"));
  });
  await expect(page.locator("[data-world-pull-viewed-summary]")).toHaveText(
    "1 of 2 files viewed",
  );
  await page
    .locator("[data-world-pull-file-path='src/alpha.js']")
    .click();
  await expect(page.locator("[data-world-pull-viewed-summary]")).toHaveText(
    "2 of 2 files viewed",
  );
  await expect(
    page.locator("[data-world-pull-file-path][data-viewed='true']"),
  ).toHaveCount(2);

  const persisted = await page.evaluate(() => JSON.stringify(localStorage));
  expect(persisted).not.toContain("world-pr-diff-visible");
  expect(persisted).not.toContain("src/alpha.js");
  const pullReads = repositoryRequests.filter(
    (url) =>
      (url.pathname.endsWith("/tree") &&
        url.searchParams.get("path") === "pulls") ||
      (url.pathname.endsWith("/blobs") &&
        url.searchParams.getAll("path").some((item) => item.startsWith("pulls/"))),
  );
  expect(pullReads.length).toBeGreaterThanOrEqual(3);
  for (const request of pullReads) {
    expect(request.searchParams.get("ref")).toBe("b".repeat(40));
    expect(request.searchParams.get("ref")).not.toBe("main");
    expect(request.searchParams.get("ref")).not.toBe("a".repeat(40));
  }
  expect(
    repositoryRequests.filter((url) => url.pathname.endsWith("/branches")),
  ).toHaveLength(1);
});

test("a fresh map resolves exact pull metadata before slow issue scans", async ({
  page,
}) => {
  const repositoryRequests = [];
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.pathname.startsWith("/api/repo/forkmesh/forkmesh/")) {
      repositoryRequests.push(url);
    }
  });
  await prepareWorldPage(page, "world-pull-priority", {
    repositoryFixture: {
      issueTreeDelayMs: 900,
      // Model a small mirror whose pull-metadata read would be rejected once
      // lower-priority issue fanout has occupied its request capacity.
      rejectPullMetadataAfterIssueFanout: true,
    },
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "ready" &&
      shell?.activeRepository?.pullCountSource === "metadata-tree";
  });

  const snapshot = await page.locator("forkmesh-world").evaluate((shell) => ({
    pullCount: shell.activeRepository?.pullCount,
    pullCountSource: shell.activeRepository?.pullCountSource,
    pullMetadataCommit:
      shell.activeRepository?.entityRecords?.pullMetadataCommit,
    pullCountExact: shell.activeRepository?.entityRecords?.pullCountExact,
    pullsAvailable: shell.activeRepository?.entityRecords?.pullsAvailable,
  }));
  expect(snapshot).toEqual({
    pullCount: 2,
    pullCountSource: "metadata-tree",
    pullMetadataCommit: "b".repeat(40),
    pullCountExact: true,
    pullsAvailable: true,
  });

  const branchesIndex = repositoryRequests.findIndex((url) =>
    url.pathname.endsWith("/branches"),
  );
  const pullTreeIndex = repositoryRequests.findIndex(
    (url) =>
      url.pathname.endsWith("/tree") &&
      url.searchParams.get("path") === "pulls" &&
      url.searchParams.get("ref") === "b".repeat(40),
  );
  const pullBlobsIndex = repositoryRequests.findIndex(
    (url) =>
      url.pathname.endsWith("/blobs") &&
      url.searchParams.get("ref") === "b".repeat(40) &&
      url.searchParams
        .getAll("path")
        .some((path) => path.startsWith("pulls/")),
  );
  const firstIssueIndex = repositoryRequests.findIndex(
    (url) =>
      url.pathname.endsWith("/tree") &&
      String(url.searchParams.get("path") || "").startsWith(
        ".forkmesh/issues",
      ),
  );
  expect(branchesIndex).toBeGreaterThanOrEqual(0);
  expect(pullTreeIndex).toBeGreaterThan(branchesIndex);
  expect(pullBlobsIndex).toBeGreaterThan(pullTreeIndex);
  expect(firstIssueIndex).toBeGreaterThan(branchesIndex);
  expect(firstIssueIndex).toBeGreaterThan(pullTreeIndex);
  expect(firstIssueIndex).toBeGreaterThan(pullBlobsIndex);

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("repositories"),
  );
  await page.getByRole("button", { name: /^2 pull requests$/i }).click();
  await page
    .locator("[data-world-pull-open][data-world-pull-number='44']")
    .click();
  await expect(
    page.getByRole("heading", { name: "Pull request #44" }),
  ).toBeVisible();
  await expect(page.locator("[data-world-pull-diff]")).toContainText(
    "world-pr-diff-visible",
  );
});

test("four fresh single-core contexts keep exact pull metadata ahead of other reads", async ({
  browser,
}) => {
  for (let iteration = 0; iteration < 4; iteration += 1) {
    const context = await browser.newContext();
    const page = await context.newPage();
    const repositoryFixture = { singleCoreDelayMs: 60 };
    const repositoryRequests = [];
    page.on("request", (request) => {
      const url = new URL(request.url());
      if (url.pathname.startsWith("/api/repo/forkmesh/forkmesh/")) {
        repositoryRequests.push(url);
      }
    });
    await prepareWorldPage(page, `single-core-${iteration}`, {
      repositoryFixture,
    });
    await waitForWorld(page);
    await page.waitForFunction(() => {
      const shell = document.querySelector("forkmesh-world");
      return shell?.repositoryMapState === "ready" &&
        shell?.activeRepository?.pullCountSource === "metadata-tree";
    });

    const snapshot = await page.locator("forkmesh-world").evaluate((shell) => ({
      commit: shell.activeRepository?.commit,
      pullCount: shell.activeRepository?.pullCount,
      pullCountSource: shell.activeRepository?.pullCountSource,
      pullMetadataCommit:
        shell.activeRepository?.entityRecords?.pullMetadataCommit,
      pullCountExact: shell.activeRepository?.entityRecords?.pullCountExact,
    }));
    expect(snapshot).toEqual({
      commit: "a".repeat(40),
      pullCount: 2,
      pullCountSource: "metadata-tree",
      pullMetadataCommit: "b".repeat(40),
      pullCountExact: true,
    });
    expect(repositoryFixture.maxMetadataActive).toBe(1);

    const branchesIndex = repositoryRequests.findIndex((url) =>
      url.pathname.endsWith("/branches"),
    );
    const pullTreeIndex = repositoryRequests.findIndex(
      (url) =>
        url.pathname.endsWith("/tree") &&
        url.searchParams.get("path") === "pulls" &&
        url.searchParams.get("ref") === "b".repeat(40),
    );
    const pullBlobsIndex = repositoryRequests.findIndex(
      (url) =>
        url.pathname.endsWith("/blobs") &&
        url.searchParams.get("ref") === "b".repeat(40) &&
        url.searchParams
          .getAll("path")
          .every((path) => /^pulls\/(?:43|44)\/pull\.md$/.test(path)),
    );
    const firstCompetingIndex = repositoryRequests.findIndex(
      (url) =>
        url.pathname.endsWith("/sizes") ||
        url.pathname.endsWith("/stats") ||
        (
          url.pathname.endsWith("/tree") &&
          String(url.searchParams.get("path") || "").startsWith(
            ".forkmesh/issues",
          )
        ),
    );
    expect(branchesIndex).toBeGreaterThanOrEqual(0);
    expect(pullTreeIndex).toBeGreaterThan(branchesIndex);
    expect(pullBlobsIndex).toBeGreaterThan(pullTreeIndex);
    expect(firstCompetingIndex).toBeGreaterThan(pullBlobsIndex);
    expect(
      repositoryRequests.filter((url) => url.pathname.endsWith("/branches")),
    ).toHaveLength(1);
    expect(
      repositoryRequests.filter(
        (url) =>
          url.pathname.endsWith("/tree") &&
          url.searchParams.get("path") === "pulls",
      ),
    ).toHaveLength(1);

    await context.close();
  }
});

test("an authenticated organization writer merges exact reviewed OIDs in-World", async ({
  page,
  context,
}) => {
  const fixture = { mergeOutcome: "success" };
  const mergeRequests = [];
  const rootTreeRequests = [];
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (
      url.pathname === "/api/repo/forkmesh/forkmesh/tree" &&
      (url.searchParams.get("path") || "") === ""
    ) {
      rootTreeRequests.push(url);
    }
    if (url.pathname.endsWith("/pulls/44/merge")) {
      mergeRequests.push({
        method: request.method(),
        authorization: request.headers().authorization || "",
        body: request.postDataJSON(),
      });
    }
  });
  await prepareWorldPage(page, "world-org-writer-merge", {
    session: {
      nodeName: "release-writer",
      sessionToken: "org-writer-session-token",
    },
    repositoryFixture: fixture,
  });
  await openWorldPullReview(page);
  const originalPages = context.pages().length;
  const originalRootReads = rootTreeRequests.length;
  const mergeButton = page.getByRole("button", {
    name: "Merge pull request #44",
  });
  await expect(mergeButton).toBeVisible();
  await mergeButton.evaluate((button) => {
    button.click();
    document.querySelector("forkmesh-world")?.mergeRepositoryPull();
  });
  await expect(
    page.locator("[data-world-pull-merge-state='merged']"),
  ).toContainText("Merged and published");
  await page.waitForFunction(
    (commit) =>
      document.querySelector("forkmesh-world")?.activeRepository?.commit ===
      commit,
    "f".repeat(40),
  );

  expect(mergeRequests).toHaveLength(2);
  expect(mergeRequests.every((request) => request.method === "POST")).toBe(true);
  expect(
    mergeRequests.every(
      (request) =>
        request.authorization === "Bearer org-writer-session-token",
    ),
  ).toBe(true);
  expect(mergeRequests[0].body).toEqual(mergeRequests[1].body);
  expect(mergeRequests[0].body).toMatchObject({
    schemaVersion: 1,
    type: "forkmesh.pull-merge-v1",
    pullNumber: 44,
    expectedBaseOid: "a".repeat(40),
    expectedHeadOid: "e".repeat(40),
    expectedPullsOid: "b".repeat(40),
  });
  expect(mergeRequests[0].body.requestId).toMatch(
    /^[A-Za-z0-9_-]{12,80}$/,
  );
  expect(rootTreeRequests.length).toBeGreaterThan(originalRootReads);
  expect(context.pages()).toHaveLength(originalPages);
  expect(page.url()).toContain("/world/");
});

test("an unauthenticated reviewer never receives a merge control", async ({
  page,
}) => {
  const mergeRequests = [];
  page.on("request", (request) => {
    if (new URL(request.url()).pathname.endsWith("/pulls/44/merge")) {
      mergeRequests.push(request);
    }
  });
  await prepareWorldPage(page, "world-unauthenticated-review", {
    repositoryFixture: {},
  });
  await openWorldPullReview(page);
  await expect(page.locator("[data-world-pull-merge]")).toHaveCount(0);
  await expect(
    page.getByText(
      "Sign in to request a protected in-World merge. Review remains available without opening another tab.",
    ),
  ).toBeVisible();
  expect(mergeRequests).toHaveLength(0);
});

test("a forbidden merge is rendered safely and never reloads repository data", async ({
  page,
}) => {
  const fixture = { mergeOutcome: "forbidden" };
  const mergeRequests = [];
  const rootTreeRequests = [];
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (
      url.pathname === "/api/repo/forkmesh/forkmesh/tree" &&
      (url.searchParams.get("path") || "") === ""
    ) {
      rootTreeRequests.push(url);
    }
    if (url.pathname.endsWith("/pulls/44/merge")) mergeRequests.push(request);
  });
  await prepareWorldPage(page, "world-forbidden-merge", {
    session: {
      nodeName: "registered-reader",
      sessionToken: "reader-session-token",
    },
    repositoryFixture: fixture,
  });
  await openWorldPullReview(page);
  const rootReadsBeforeMerge = rootTreeRequests.length;
  await page
    .getByRole("button", { name: "Merge pull request #44" })
    .click();
  const denied = page.locator(
    "[data-world-pull-merge-state='forbidden']",
  );
  await expect(denied).toHaveAttribute("role", "alert");
  await expect(denied).toContainText("Merge not authorized");
  await expect(denied).toContainText("organization writer");
  await expect(page.locator("[data-world-pull-merge]")).toHaveCount(0);
  expect(mergeRequests).toHaveLength(1);
  expect(rootTreeRequests).toHaveLength(rootReadsBeforeMerge);
});

for (const scenario of [
  {
    outcome: "conflict",
    state: "conflict",
    title: "Merge conflict",
  },
  {
    outcome: "stale",
    state: "stale",
    title: "Review is stale",
  },
]) {
  test(`${scenario.outcome} merge result is explicit and does not reload`, async ({
    page,
  }) => {
    const fixture = { mergeOutcome: scenario.outcome };
    const rootTreeRequests = [];
    page.on("request", (request) => {
      const url = new URL(request.url());
      if (
        url.pathname === "/api/repo/forkmesh/forkmesh/tree" &&
        (url.searchParams.get("path") || "") === ""
      ) {
        rootTreeRequests.push(url);
      }
    });
    await prepareWorldPage(page, `world-${scenario.outcome}-merge`, {
      session: {
        nodeName: "organization-writer",
        sessionToken: `${scenario.outcome}-session-token`,
      },
      repositoryFixture: fixture,
    });
    await openWorldPullReview(page);
    const rootReadsBeforeMerge = rootTreeRequests.length;
    await page
      .getByRole("button", { name: "Merge pull request #44" })
      .click();
    const result = page.locator(
      `[data-world-pull-merge-state='${scenario.state}']`,
    );
    await expect(result).toHaveAttribute("role", "alert");
    await expect(result).toContainText(scenario.title);
    expect(rootTreeRequests).toHaveLength(rootReadsBeforeMerge);
  });
}

test("bounded polling and a manual retry reuse one idempotency request", async ({
  page,
}) => {
  const fixture = { mergeOutcome: "retry" };
  const mergeBodies = [];
  page.on("request", (request) => {
    if (new URL(request.url()).pathname.endsWith("/pulls/44/merge")) {
      mergeBodies.push(request.postDataJSON());
    }
  });
  await prepareWorldPage(page, "world-bounded-merge-poll", {
    session: {
      nodeName: "organization-writer",
      sessionToken: "bounded-poll-session-token",
    },
    repositoryFixture: fixture,
  });
  await openWorldPullReview(page);
  await page
    .getByRole("button", { name: "Merge pull request #44" })
    .click();
  await expect(
    page.locator("[data-world-pull-merge-state='pending']"),
  ).toContainText("Bounded automatic polling ended");
  expect(mergeBodies).toHaveLength(6);
  await page.getByRole("button", { name: "Check merge status" }).click();
  await expect(
    page.locator("[data-world-pull-merge-state='conflict']"),
  ).toContainText("Merge conflict");
  expect(mergeBodies).toHaveLength(7);
  expect(new Set(mergeBodies.map((body) => body.requestId)).size).toBe(1);
  expect(mergeBodies.every((body) => body.type === "forkmesh.pull-merge-v1")).toBe(
    true,
  );
});

test("the protected merge control is touch-sized and announces state on mobile", async ({
  page,
}) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await prepareWorldPage(page, "world-mobile-merge", {
    session: {
      nodeName: "mobile-writer",
      sessionToken: "mobile-writer-session-token",
    },
    repositoryFixture: {},
  });
  await openWorldPullReview(page);
  const panel = page.locator("[data-world-pull-merge-state='ready']");
  const button = panel.getByRole("button", {
    name: "Merge pull request #44",
  });
  await expect(panel).toHaveAttribute("role", "status");
  await expect(panel).toHaveAttribute("aria-live", "polite");
  await expect(panel).toHaveAttribute("aria-busy", "false");
  await expect(button).toBeVisible();
  const box = await button.boundingBox();
  expect(box).not.toBeNull();
  expect(box.height).toBeGreaterThanOrEqual(44);
  expect(box.width).toBeLessThanOrEqual(390);
  await button.focus();
  await expect(button).toBeFocused();
});

test("missing pull metadata branch fails closed without probing main", async ({
  page,
}) => {
  const repositoryRequests = [];
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.pathname.startsWith("/api/repo/forkmesh/forkmesh/")) {
      repositoryRequests.push(url);
    }
  });
  await prepareWorldPage(page, "world-pull-unavailable", {
    repositoryFixture: { invalidPullBranch: true },
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "ready";
  });
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("repositories"),
  );
  await page.getByRole("button", { name: /^2 pull requests$/i }).click();
  await expect(
    page.locator("[data-world-pull-state='unavailable']"),
  ).toContainText("exact forkmesh/pulls commit could not be verified");
  await expect(
    page.locator("[data-world-pull-state='unavailable']"),
  ).toContainText("No main-branch, guessed-ref");
  expect(
    repositoryRequests.filter(
      (url) =>
        url.pathname.endsWith("/tree") &&
        url.searchParams.get("path") === "pulls",
    ),
  ).toHaveLength(0);
  expect(
    repositoryRequests.filter(
      (url) =>
        url.pathname.endsWith("/blobs") &&
        url.searchParams.getAll("path").some((item) => item.startsWith("pulls/")),
    ),
  ).toHaveLength(0);
  await page.locator("[data-world-pull-back='map']").click();
  await expect(page.locator("[data-world-code-map]")).toBeVisible();
});

test("a listed pull with unreadable metadata is unknown rather than open", async ({
  page,
}) => {
  await prepareWorldPage(page, "world-pull-record-unreadable", {
    repositoryFixture: { missingPullMetadata: true },
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "ready";
  });
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("repositories"),
  );
  await page.getByRole("button", { name: /^2 pull requests$/i }).click();
  const unavailable = page.locator(".world-pull-record-unavailable");
  await expect(unavailable).toHaveCount(1);
  await expect(unavailable).toContainText(
    "#43 · Metadata unavailable",
  );
  await expect(unavailable).toContainText("unknown");
  await expect(page.locator("[data-world-pull-open]")).toHaveCount(1);
  await expect(page.locator("[data-world-pull-open]")).toHaveAttribute(
    "data-world-pull-number",
    "44",
  );
  await expect(
    page.getByText(
      "1 pull-request record is listed by the tree but its metadata is unavailable.",
    ),
  ).toBeVisible();
});

test("live mirror cabinets expose a readable truthful technical panel", async ({
  page,
}) => {
  await prepareWorldPage(page, "mirror-cabinets");
  await waitForWorld(page);

  const cabinets = await page.locator("forkmesh-world").evaluate((shell) => {
    const items = [];
    shell.world.scene.traverse((object) => {
      if (String(object.name || "").startsWith("mirror-server-cabinet:")) {
        items.push({
          name: object.name,
          x: object.position.x,
          z: object.position.z,
        });
      }
    });
    return items;
  });
  expect(cabinets).toHaveLength(2);
  expect(cabinets[0].name).not.toBe(cabinets[1].name);
  expect(
    Math.hypot(
      cabinets[0].x - cabinets[1].x,
      cabinets[0].z - cabinets[1].z,
    ),
  ).toBeGreaterThan(2);

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.focusNetworkNode("mirror2");
    shell.world.setCameraZoom(0.32);
  });
  await page.waitForTimeout(450);
  await expect(page).toHaveScreenshot("world-mirror-cabinets.png", {
    animations: "disabled",
    maxDiffPixelRatio: 0.015,
  });

  await page.locator("forkmesh-world").evaluate((shell) => {
    let mirror = null;
    shell.world.scene.traverse((object) => {
      if (object.userData?.nodeRecord?.name === "mirror2") {
        mirror = object.userData.nodeRecord;
      }
    });
    shell.openMirrorNodeDetail(mirror);
  });
  const detail = page.locator("[data-world-mirror-node-detail]");
  await expect(detail).toBeVisible();
  await expect(detail).toContainText("mirror2");
  await expect(detail).toContainText("25.0%");
  await expect(detail).toContainText("42");
  await expect(detail).toContainText("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  await expect(detail).toContainText("forkmesh/forkmesh");
  await expect(detail).toContainText(
    "operator-reported, bounded values signed into the public catalog",
  );
});

test("the member lounge plaque carries the count and one account button", async ({
  page,
}) => {
  await prepareWorldPage(page, "lounge-plaque");
  await waitForWorld(page);
  const logoutRequests = [];
  await page.route("**/api/accounts/logout", async (route) => {
    logoutRequests.push(route.request().method());
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ ok: true }),
    });
  });

  // No floating count card hovers over the lounge any more: the total and the
  // account button live on the one ground plaque.
  const lounge = await page.locator("forkmesh-world").evaluate((shell) => {
    const group = shell.world.scene.getObjectByName("registered-user-lounge");
    shell.world.updateMemberLounge([{ name: "ada", nodes: [] }], 9);
    return {
      sprites: group.children.filter((child) => child.isSprite).length,
      plaque: Boolean(
        group.getObjectByName("forkmesh-member-lounge-plaque"),
      ),
      countOnPlaque:
        group.userData.memberCountSign ===
        group.getObjectByName("forkmesh-member-lounge-plaque").userData.face,
      action: group.userData.authButton.userData.worldAuthAction,
    };
  });
  expect(lounge).toEqual({
    sprites: 0,
    plaque: true,
    countOnPlaque: true,
    action: "login",
  });

  // Park the camera on the plaque and pause so the button projects to a stable
  // point, then tap it exactly like a visitor walking up to the lounge.
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.closeLandmark();
    shell.world.player.position.set(-36.5, 0.38, 23.5);
    shell.world.setCameraZoom(0.42);
  });
  await page.waitForTimeout(1800);
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.setPaused(true),
  );
  const buttonPoint = async () =>
    page.locator("forkmesh-world").evaluate((shell) => {
      const button = shell.world.scene
        .getObjectByName("registered-user-lounge")
        .userData.authButton;
      const target = button.getWorldPosition(button.position.clone());
      target.project(shell.world.camera);
      const rect = shell.world.renderer.domElement.getBoundingClientRect();
      return {
        x: rect.left + (target.x * 0.5 + 0.5) * rect.width,
        y: rect.top + (-target.y * 0.5 + 0.5) * rect.height,
      };
    });
  const guestPoint = await buttonPoint();
  await page.mouse.click(guestPoint.x, guestPoint.y);
  await expect(page.locator("[data-world-account]")).toHaveAttribute(
    "data-open",
    "true",
  );
  expect(logoutRequests).toEqual([]);

  // Signed in, the same tiny button becomes the log-out control.
  await page
    .locator("[data-world-account] [data-world-account-close]")
    .click();
  const signedIn = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.updateIdentity({ accountStatus: "Registered", name: "ada" });
    return shell.world.scene.getObjectByName("registered-user-lounge").userData
      .authButton.userData.worldAuthAction;
  });
  expect(signedIn).toBe("logout");
  // The dismissed backdrop stays hit-testable until its visibility transition
  // finishes, so wait for it to stop covering the plaque.
  await page.waitForFunction(() => {
    const backdrop = document
      .querySelector("forkmesh-world")
      .querySelector(".world-account-backdrop");
    return getComputedStyle(backdrop).visibility === "hidden";
  });
  const memberPoint = await buttonPoint();
  await page.mouse.click(memberPoint.x, memberPoint.y);
  await expect.poll(() => logoutRequests).toEqual(["POST"]);
});

test("Town Square placement is contextual, tracking-free, and collapses safely", async ({
  page,
}) => {
  const placementRequests = [];
  page.on("request", (request) => {
    if (request.url().includes("/api/world/community-ads/placements")) {
      placementRequests.push(request.url());
    }
  });
  await prepareWorldPage(page, "community-placement");
  await waitForWorld(page);

  const rail = page.locator(".world-right-rail");
  const map = page.locator(".world-map");
  const placement = page.locator("[data-world-community-placement]");
  await expect(placement).toBeVisible();
  await expect(placement).toContainText("Community-reviewed placement");
  await expect(placement).toContainText("No behavioral tracking");
  await expect(placement.getByRole("link", { name: /Visit sponsor/ }))
    .toHaveAttribute("rel", "sponsored noopener noreferrer");
  await expect(placement.getByRole("link", { name: /Visit sponsor/ }))
    .toHaveAttribute("referrerpolicy", "no-referrer");
  expect(placementRequests.map((value) => {
    const url = new URL(value);
    return `${url.pathname}${url.search}`;
  })).toEqual([
    "/api/world/community-ads/placements?context=town-square",
  ]);
  expect(
    await rail.evaluate((element, nodes) => {
      const mapNode = document.querySelector(nodes.map);
      const placementNode = document.querySelector(nodes.placement);
      return (
        element.contains(mapNode) &&
        element.contains(placementNode) &&
        Boolean(
          mapNode.compareDocumentPosition(placementNode) &
            Node.DOCUMENT_POSITION_FOLLOWING,
        )
      );
    }, { map: ".world-map", placement: "[data-world-community-placement]" }),
  ).toBe(true);

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("fountain"),
  );
  await expect(placement).toBeHidden();
  await page.locator("[data-world-detail-close]").click();
  await page.setViewportSize({ width: 900, height: 900 });
  await expect(map).toBeHidden();
  await expect(placement).toBeHidden();
});

test("verified public feedback requires manual preview and remains pending", async ({
  page,
}) => {
  const createBodies = [];
  await page.addInitScript(() => {
    localStorage.setItem(
      "forkmesh.session",
      JSON.stringify({
        sessionToken: "playwright-owner-session",
        nodeName: "owner",
      }),
    );
  });
  page.on("request", (request) => {
    if (request.url().endsWith("/create")) {
      createBodies.push(request.postDataJSON());
    }
  });
  await prepareWorldPage(page, "fediverse-manual-review");
  await waitForWorld(page);

  const feed = page.locator("[data-world-fediverse-activity]");
  await expect(feed).toBeVisible();
  await expect(feed).toContainText("Verified public feedback");
  await expect(feed).toContainText("Manual review");
  await feed.getByRole("button", { name: "Preview" }).click();
  await expect(page.getByRole("heading", { name: "Review public feedback" }))
    .toBeVisible();
  await expect(page.locator("[data-world-fediverse-followup]")).not.toBeChecked();

  await page.locator("[data-world-fediverse-create]").click();
  expect(createBodies).toHaveLength(0);
  await expect(page.locator("[data-world-fediverse-result]")).toContainText(
    "explicit authorization",
  );

  await page.locator("[data-world-fediverse-confirm]").check();
  await page.locator("[data-world-fediverse-followup]").check();
  await page.locator("[data-world-fediverse-create]").click();
  await expect(page.locator("[data-world-fediverse-result]")).toContainText(
    "Pending owner-node materialization",
  );
  expect(createBodies).toEqual([
    {
      confirm: true,
      title: "Save button crashes",
      body: "The save button crashes on large projects.",
      publishFollowup: true,
    },
  ]);
  await expect(page.getByText("created", { exact: true })).toHaveCount(0);
});

test("tracked public replies open a separate read-only fediverse thread", async ({
  page,
}) => {
  await prepareWorldPage(page, "fediverse-thread", { includeThread: true });
  await waitForWorld(page);

  const feed = page.locator("[data-world-fediverse-activity]");
  await expect(feed).toContainText("Tracked reply");
  await feed.getByRole("button", { name: "Thread" }).click();

  await expect(
    page.getByRole("heading", { name: "Issue #17 fediverse thread" }),
  ).toBeVisible();
  const reply = page.locator("[data-world-federated-reply]");
  await expect(reply).toContainText("Nested reply from a public Lemmy thread.");
  await expect(reply).toContainText("not a signed ForkMesh native event");
  await expect(reply).toHaveAttribute("data-native-event", "false");
  await expect(reply.getByRole("link", { name: /Open original/ }))
    .toHaveAttribute("href", "https://lemmy.example.org/comment/77");
  await expect(reply.getByRole("link", { name: /Open original/ }))
    .toHaveAttribute("referrerpolicy", "no-referrer");
});

test("four-hour procedural soundtrack starts only after consent and stops locally", async ({
  page,
}) => {
  const mediaRequests = [];
  page.on("request", (request) => {
    if (
      request.resourceType() === "media" ||
      /\.(?:mp3|m4a|ogg|wav|m3u8)(?:[?#]|$)/i.test(request.url())
    ) {
      mediaRequests.push(request.url());
    }
  });
  await page.addInitScript(() => {
    class AudioParam {
      setValueAtTime() {}
      exponentialRampToValueAtTime() {}
      cancelScheduledValues() {}
      setTargetAtTime() {}
    }
    class Node {
      constructor() {
        this.gain = new AudioParam();
        this.frequency = new AudioParam();
        this.detune = new AudioParam();
      }
      connect() {}
      start() {}
      stop() {}
    }
    class FakeAudioContext {
      constructor() {
        this.currentTime = 0;
        this.destination = {};
      }
      createGain() {
        return new Node();
      }
      createOscillator() {
        return new Node();
      }
      async resume() {}
      async close() {}
    }
    Object.defineProperty(window, "AudioContext", {
      configurable: true,
      value: FakeAudioContext,
    });
  });
  await prepareWorldPage(page, "soundtrack");
  await waitForWorld(page);
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => shell.activeAudio),
  ).toBeNull();

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("broadcast"),
  );
  await page.locator("[data-world-radio='forkmesh-focus']").click();
  await expect(page.locator("[data-world-media-now]")).toContainText(
    "Use the Sound button",
  );
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => shell.activeAudio),
  ).toBeNull();
  await page.locator("[data-world-sound-toggle]").click();
  await expect(page.locator("[data-world-sound-toggle]")).toHaveAttribute(
    "aria-pressed",
    "true",
  );
  await page.locator("[data-world-radio='forkmesh-focus']").click();
  const playback = await page.locator("forkmesh-world").evaluate((shell) => ({
    durationMs: shell.activeAudio?.durationMs,
    scoreOffsetMs: shell.activeAudio?.scoreOffsetMs,
    license: shell.activeAudio?.license,
  }));
  expect(playback.durationMs).toBe(4 * 60 * 60 * 1000);
  expect(playback.scoreOffsetMs).toBeGreaterThanOrEqual(0);
  expect(playback.scoreOffsetMs).toBeLessThan(playback.durationMs);
  expect(playback.license).toContain("CC0-1.0");
  await expect(page.locator("[data-world-track]")).toContainText(
    "loops independently of the UTC display",
  );
  expect(mediaRequests).toEqual([]);

  await page.locator("[data-world-radio-stop]").click();
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => shell.activeAudio),
  ).toBeNull();
});

test("the ForkMesh song button plays the first-party track only on request", async ({
  page,
}) => {
  await page.addInitScript(() => {
    window.__forkmeshSongPlays = [];
    HTMLMediaElement.prototype.play = function play() {
      window.__forkmeshSongPlays.push(this.getAttribute("src") || this.src);
      return Promise.resolve();
    };
    HTMLMediaElement.prototype.pause = function pause() {};
  });
  await prepareWorldPage(page, "forkmesh-song");
  await waitForWorld(page);

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("broadcast"),
  );
  const button = page.locator("[data-world-radio='forkmesh-song']");
  await expect(button).toHaveText("Listen to the ForkMesh song");
  expect(await page.evaluate(() => window.__forkmeshSongPlays)).toEqual([]);

  await button.click();
  expect(await page.evaluate(() => window.__forkmeshSongPlays)).toEqual([
    "/assets/songs/ForkMeshForever(IndiePop).mp3",
  ]);
  await expect(page.locator("[data-world-media-now]")).toContainText(
    "ForkMesh Forever (Indie Pop)",
  );

  await page.locator("[data-world-radio-stop]").click();
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => shell.activeAudio),
  ).toBeNull();
  await expect(page.locator("[data-world-media-now]")).toContainText(
    "Nothing is playing",
  );
});

test("focus music defaults to Heavenly and loops only after explicit playback", async ({
  page,
}) => {
  const mediaRequests = [];
  page.on("request", (request) => {
    if (new URL(request.url()).pathname.startsWith("/assets/music/")) {
      mediaRequests.push(request.url());
    }
  });
  await installFocusMusicAudioProbe(page);
  await prepareWorldPage(page, "focus-music-consent");
  await waitForWorld(page);
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("broadcast"),
  );

  const tracks = page.locator("[data-world-focus-track]");
  await expect(tracks).toHaveCount(3);
  const heavenly = page.locator(
    "[data-world-focus-track='heavenly-loop']",
  );
  await expect(heavenly).toBeVisible();
  expect(await focusMusicTrackIsChecked(page, "heavenly-loop")).toBe(true);
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Heavenly Loop is selected. Press Play",
  );

  const idle = await focusMusicProbeSnapshot(page);
  expect(idle.created).toEqual([]);
  expect(mediaRequests).toEqual([]);
  const intervalRegistrationsBeforePlay = idle.intervalRegistrations;

  await page.locator("[data-world-focus-play]").click();
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Heavenly Loop is playing",
  );
  let playback = await focusMusicProbeSnapshot(page);
  expect(playback.created).toHaveLength(1);
  expect(playback.created[0]).toMatchObject({
    src: "/assets/music/heavenly-loop.ogg",
    loop: true,
    muted: false,
    paused: false,
    volume: 0.35,
    playCalls: 1,
  });
  expect(playback.intervalRegistrations).toBe(
    intervalRegistrationsBeforePlay,
  );
  expect(mediaRequests).toEqual([]);

  await chooseFocusMusicTrack(page, "forgotten-victory");
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Forgotten Victory is playing",
  );
  playback = await focusMusicProbeSnapshot(page);
  expect(playback.created).toHaveLength(2);
  expect(playback.created[0]).toMatchObject({
    currentTime: 0,
    paused: true,
    pauseCalls: 1,
  });
  expect(playback.created[1]).toMatchObject({
    src: "/assets/music/forgotten-victory.ogg",
    loop: true,
    paused: false,
    playCalls: 1,
  });

  await page.locator("[data-world-focus-pause]").click();
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Forgotten Victory is paused",
  );
  playback = await focusMusicProbeSnapshot(page);
  expect(playback.created[1]).toMatchObject({
    paused: true,
    pauseCalls: 1,
  });
  await expect(page.locator("[data-world-focus-pause]")).toHaveText("Resume");

  await page.locator("[data-world-focus-pause]").click();
  playback = await focusMusicProbeSnapshot(page);
  expect(playback.created[1]).toMatchObject({
    paused: false,
    playCalls: 2,
  });

  await page.locator("[data-world-focus-volume]").fill("62");
  await expect(page.locator("[data-world-focus-volume]")).toHaveValue("62");
  playback = await focusMusicProbeSnapshot(page);
  expect(playback.created[1].volume).toBeCloseTo(0.62, 5);

  await page.locator("[data-world-focus-mute]").click();
  await expect(page.locator("[data-world-focus-mute]")).toHaveAttribute(
    "aria-pressed",
    "true",
  );
  playback = await focusMusicProbeSnapshot(page);
  expect(playback.created[1].muted).toBe(true);

  const stored = await page.evaluate(() =>
    JSON.parse(localStorage.getItem("forkmesh.world.settings.v1")),
  );
  expect(stored).toMatchObject({
    focusMusicTrackId: "forgotten-victory",
    focusMusicVolume: 62,
    focusMusicMuted: true,
  });

  await page.locator("[data-world-focus-stop]").click();
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Press Play",
  );
  playback = await focusMusicProbeSnapshot(page);
  expect(playback.created[1]).toMatchObject({
    currentTime: 0,
    paused: true,
  });
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => shell.activeAudio),
  ).toBeNull();
  expect(playback.intervalRegistrations).toBe(
    intervalRegistrationsBeforePlay,
  );
});

test("focus music selection and controls persist without autoplaying on reload", async ({
  page,
}) => {
  await installFocusMusicAudioProbe(page);
  await prepareWorldPage(page, "focus-music-persistence");
  await waitForWorld(page);
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("broadcast"),
  );

  await chooseFocusMusicTrack(page, "tarlite-slumber");
  await page.locator("[data-world-focus-volume]").fill("48");
  await page.locator("[data-world-focus-mute]").click();
  expect((await focusMusicProbeSnapshot(page)).created).toEqual([]);

  await page.reload();
  await waitForWorldReady(page);
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("broadcast"),
  );
  expect(await focusMusicTrackIsChecked(page, "tarlite-slumber")).toBe(true);
  await expect(page.locator("[data-world-focus-volume]")).toHaveValue("48");
  await expect(page.locator("[data-world-focus-mute]")).toHaveAttribute(
    "aria-pressed",
    "true",
  );
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Press Play",
  );
  expect((await focusMusicProbeSnapshot(page)).created).toEqual([]);

  await page.locator("[data-world-focus-play]").click();
  const restoredPlayback = await focusMusicProbeSnapshot(page);
  expect(restoredPlayback.created).toHaveLength(1);
  expect(restoredPlayback.created[0]).toMatchObject({
    src: "/assets/music/tarlite-trycor-slumber-area.ogg",
    loop: true,
    muted: true,
    paused: false,
    volume: 0.48,
    playCalls: 1,
  });

  await page.evaluate(() => {
    const key = "forkmesh.world.settings.v1";
    const settings = JSON.parse(localStorage.getItem(key));
    settings.focusMusicTrackId = "not-a-bundled-track";
    localStorage.setItem(key, JSON.stringify(settings));
  });
  await page.reload();
  await waitForWorldReady(page);
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("broadcast"),
  );
  expect(await focusMusicTrackIsChecked(page, "heavenly-loop")).toBe(true);
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Heavenly Loop is selected. Press Play",
  );
  expect((await focusMusicProbeSnapshot(page)).created).toEqual([]);
});

test("portrait coarse-pointer thumbstick and visual viewport remain usable", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
  });
  const page = await context.newPage();
  await prepareWorldPage(page, "portrait-touch");
  await waitForWorld(page);

  await expect(page.locator("[data-world-thumbstick]")).toBeVisible();
  await page.locator("[data-world-settings-open]").first().click();
  await expect(page.locator("[data-world-settings]")).toHaveAttribute(
    "data-open",
    "true",
  );
  const nameInput = page.locator("[data-world-display-name]");
  await nameInput.focus();
  const before = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  await page.keyboard.press("w");
  const afterInput = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  expect(afterInput).toEqual(before);

  await page.setViewportSize({ width: 390, height: 520 });
  await page.waitForFunction(
    () =>
      document.querySelector("forkmesh-world")?.style
        .getPropertyValue("--world-viewport-height") === "520px",
  );
  const bounds = await page.locator("[data-world-root]").boundingBox();
  expect(bounds.height).toBeLessThanOrEqual(520);
  await page.locator("[data-world-settings-close]").click();
  await expect(page.locator("[data-world-settings]")).toHaveAttribute(
    "data-open",
    "false",
  );

  const control = page.locator("[data-world-thumbstick]");
  // Leave enough time for more than one animation frame even when the release
  // gate is sharing a loaded CI host; the assertion still requires real
  // position movement from an actual CDP touch sequence.
  await dragThumbstick(page, control, { durationMs: 600 });
  const afterTouch = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  expect(
    Math.hypot(afterTouch.x - before.x, afterTouch.z - before.z),
  ).toBeGreaterThan(0.05);
  await context.close();
});

for (const viewport of [
  {
    name: "desktop",
    options: { viewport: { width: 1440, height: 900 } },
  },
  {
    name: "portrait",
    options: {
      viewport: { width: 390, height: 844 },
      hasTouch: true,
      isMobile: true,
    },
  },
  {
    name: "landscape",
    options: {
      viewport: { width: 844, height: 390 },
      hasTouch: true,
      isMobile: true,
    },
  },
]) {
  test(`visual acceptance ${viewport.name}`, async ({ browser }, testInfo) => {
    testInfo.snapshotSuffix = "linux";
    const context = await browser.newContext(viewport.options);
    const page = await context.newPage();
    await page.routeWebSocket(
      "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
      () => {},
    );
    await prepareWorldPage(page, `visual-${viewport.name}`, {
      chatPassphrase: `visual-${viewport.name}-office-passphrase`,
    });
    await waitForWorld(page);
    await freezeWorld(page);
    await expect(page).toHaveScreenshot(`world-${viewport.name}.png`, {
      animations: "disabled",
      caret: "hide",
      maxDiffPixelRatio: 0.01,
    });
    await openOfficeForVisual(page);
    const dock = page.locator("[data-world-office-lobby]");
    const dockBox = await dock.boundingBox();
    expect(dockBox).not.toBeNull();
    if (viewport.options.viewport.width <= 720) {
      expect(dockBox.height).toBeLessThanOrEqual(viewport.options.viewport.height);
    } else {
      expect(dockBox.width).toBeLessThanOrEqual(621);
    }
    await expect(page.locator("canvas.world-canvas")).toBeVisible();
    expect(await page.evaluate(
      () => document.documentElement.scrollWidth <= window.innerWidth,
    )).toBe(true);
    await expect(page).toHaveScreenshot(`world-office-${viewport.name}.png`, {
      animations: "disabled",
      caret: "hide",
      maxDiffPixelRatio: 0.01,
    });
    await context.close();
  });
}

test("ForkMesh Office lobby stays bounded at 320 CSS pixels", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 320, height: 640 },
    hasTouch: true,
    isMobile: true,
  });
  const page = await context.newPage();
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    () => {},
  );
  await prepareWorldPage(page, "office-320", {
    chatPassphrase: "playwright-office-320-passphrase",
  });
  await waitForWorld(page);
  await openOfficeForVisual(page);
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(320);
  const dockBox = await page.locator("[data-world-office-lobby]").boundingBox();
  expect(dockBox).not.toBeNull();
  expect(dockBox.width).toBeLessThanOrEqual(320);
  expect(dockBox.height).toBeLessThanOrEqual(640);
  await context.close();
});
