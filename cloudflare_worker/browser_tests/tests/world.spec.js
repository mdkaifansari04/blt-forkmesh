const { test, expect } = require("@playwright/test");
const path = require("node:path");
const { createCipheriv, createHash, pbkdf2Sync } = require("node:crypto");

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
const OFFICE_ENTRANCE_POSITION = [0, 0.38, -168.8];
const OFFICE_ENTRY_TICKET = "playwright-office-entry-ticket";

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

function worldChatKey(passphrase) {
  const salt = createHash("sha256")
    .update("ForkMesh room:world-general", "utf8")
    .digest()
    .subarray(0, 16);
  return pbkdf2Sync(passphrase, salt, 210000, 32, "sha256");
}

function encryptWorldChatEnvelope(message, key, nonceByte) {
  const nonce = Buffer.alloc(12, nonceByte);
  const cipher = createCipheriv("aes-256-gcm", key, nonce);
  const body = Buffer.concat([
    cipher.update(JSON.stringify(message), "utf8"),
    cipher.final(),
  ]);
  return {
    kind: "cipher",
    v: 1,
    nonce: nonce.toString("base64"),
    tag: cipher.getAuthTag().toString("base64"),
    body: body.toString("base64"),
    historyReplay: true,
  };
}

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
    repositoryFollowerFixture = null,
    accountFixture = null,
    unavailablePaths = [],
    chatChannels = [],
    chatChannelStatus = 200,
    ticketAuthenticated = true,
    ticketActivity = null,
    directoryUsers = [],
    systemCapacityTables = [],
    systemCapacityD1Storage = null,
    officeEntryRequests = [],
    officeFloorRequests = [],
    officeAttendanceRequests = [],
    officeAttendanceFixture = null,
    officeFloorAccess = null,
    officeFloorDelayMs = 0,
    officeTaskFixture = null,
    accountSessionFixture = null,
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
                    isAdmin:
                      systemCapacityTables.length > 0 ||
                      Boolean(systemCapacityD1Storage),
                    ticket: "playwright-world-ticket",
                    expiresAt: FIXED_NOW + 300_000,
                    totalActiveMs: Math.max(
                      0,
                      Number(ticketActivity?.totalActiveMs) || 0,
                    ),
                    activityObservedAt:
                      Number(ticketActivity?.activityObservedAt) || FIXED_NOW,
                    ...(systemCapacityTables.length || systemCapacityD1Storage
                      ? {
                          systemCapacity: {
                            tables: systemCapacityTables,
                            ...(systemCapacityD1Storage
                              ? { d1Storage: systemCapacityD1Storage }
                              : {}),
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
          : url.pathname === "/api/accounts/users"
            ? { ok: true, users: directoryUsers }
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
    if (
      officeTaskFixture &&
      url.pathname.startsWith("/api/world/office/marketing-tasks")
    ) {
      const method = route.request().method();
      const suffix = url.pathname
        .slice("/api/world/office/marketing-tasks".length)
        .split("/")
        .filter(Boolean);
      let requestBody = {};
      try {
        requestBody = route.request().postDataJSON() || {};
      } catch (_) {}
      officeTaskFixture.requests ||= [];
      officeTaskFixture.tasks ||= [];
      officeTaskFixture.requests.push({
        method,
        path: url.pathname,
        body: requestBody,
      });
      if (!session?.sessionToken) {
        status = 401;
        body = { error: "invalid_session" };
      } else if (method === "GET" && suffix.length === 0) {
        body = {
          ok: true,
          actor: String(session.nodeName || "").toLowerCase(),
          canManage: officeTaskFixture.canManage === true,
          serverNow: FIXED_NOW,
          tasks: officeTaskFixture.tasks,
          ...(officeTaskFixture.canManage
            ? { members: officeTaskFixture.members || [session.nodeName] }
            : {}),
        };
      } else if (method === "POST" && suffix.length === 0) {
        const task = {
          id: String(officeTaskFixture.nextId || "e".repeat(32)),
          title: requestBody.title,
          assignee: String(requestBody.assignee || "").toLowerCase(),
          status: "idle",
          elapsedMs: 0,
          startedAt: 0,
          nextCheckinAt: 0,
          createdAt: FIXED_NOW,
          updatedAt: FIXED_NOW,
          lastCheckin: null,
        };
        officeTaskFixture.tasks.unshift(task);
        status = 201;
        body = { ok: true, task };
      } else if (
        method === "POST" &&
        suffix.length === 1 &&
        suffix[0] === "stop-active"
      ) {
        const actor = String(session.nodeName || "").toLowerCase();
        const task = officeTaskFixture.tasks.find(
          (item) => item.assignee === actor && item.status === "active",
        );
        if (task) {
          Object.assign(task, {
            status: "idle",
            elapsedMs: Number(task.elapsedMs || 0) + 1500,
            startedAt: 0,
            nextCheckinAt: 0,
          });
        }
        body = { ok: true, stopped: Boolean(task) };
      } else if (method === "POST" && suffix.length === 2) {
        const [taskId, action] = suffix;
        const task = officeTaskFixture.tasks.find((item) => item.id === taskId);
        if (!task) {
          status = 404;
          body = { error: "task_not_found" };
        } else if (action === "start") {
          Object.assign(task, {
            status: "active",
            startedAt: FIXED_NOW,
            nextCheckinAt: FIXED_NOW + 4 * 60 * 1000,
          });
          body = { ok: true, task };
        } else if (action === "stop") {
          Object.assign(task, {
            status: "idle",
            elapsedMs: Number(task.elapsedMs || 0) + 1500,
            startedAt: 0,
            nextCheckinAt: 0,
          });
          body = { ok: true, task };
        } else if (action === "checkin") {
          task.lastCheckin = {
            state: requestBody.state,
            at: FIXED_NOW,
          };
          task.nextCheckinAt = FIXED_NOW + 6 * 60 * 1000;
          body = { ok: true, task };
        } else {
          status = 404;
          body = { error: "not_found" };
        }
      } else {
        status = 405;
        body = { error: "method_not_allowed" };
      }
    } else if (
      accountSessionFixture &&
      url.pathname.startsWith("/api/accounts/sessions")
    ) {
      const method = route.request().method();
      const target =
        url.pathname
          .slice("/api/accounts/sessions".length)
          .split("/")
          .filter(Boolean)[0] || "";
      accountSessionFixture.requests ||= [];
      accountSessionFixture.sessions ||= [];
      accountSessionFixture.requests.push({ method, target });
      if (!session?.sessionToken) {
        status = 401;
        body = { error: "invalid_session" };
      } else if (method === "GET") {
        body = {
          ok: true,
          sessions: accountSessionFixture.sessions,
          account: {
            lastSeenAt: FIXED_NOW,
            lastEmailAt: 0,
            lastEmailStatus: "",
            lastEmailKind: "",
          },
          privacyNotice: "Only you can see this list.",
        };
      } else if (method === "DELETE" && target === "others") {
        accountSessionFixture.sessions =
          accountSessionFixture.sessions.filter((item) => item.current);
        body = { ok: true, currentRevoked: false };
      } else if (method === "DELETE" && target === "all") {
        accountSessionFixture.sessions = [];
        body = { ok: true, currentRevoked: true };
      } else if (method === "DELETE") {
        const revoked = accountSessionFixture.sessions.find(
          (item) => item.id === target,
        );
        if (!revoked) {
          status = 404;
          body = { error: "session_not_found" };
        } else {
          accountSessionFixture.sessions =
            accountSessionFixture.sessions.filter(
              (item) => item.id !== target,
            );
          body = { ok: true, currentRevoked: revoked.current === true };
        }
      } else {
        status = 405;
        body = { error: "method_not_allowed" };
      }
    } else if (url.pathname === "/api/world/office/attendance") {
      let requestBody = {};
      try {
        requestBody = route.request().postDataJSON() || {};
      } catch (_) {}
      const method = route.request().method();
      officeAttendanceRequests.push({
        method,
        headers: route.request().headers(),
        body: requestBody,
      });
      if (method === "GET") {
        body = {
          ok: true,
          visits: officeAttendanceFixture?.getVisits || [],
        };
      } else if (method === "POST" && session?.sessionToken) {
        const visits =
          requestBody.action === "out"
            ? officeAttendanceFixture?.outVisits
            : officeAttendanceFixture?.inVisits;
        body = { ok: true, visits: visits || [] };
      } else if (method === "POST") {
        status = 401;
        body = { error: "login_required" };
      } else {
        status = 405;
        body = { error: "method_not_allowed" };
      }
    } else if (url.pathname === "/api/world/office/floors") {
      officeFloorRequests.push({
        method: route.request().method(),
        url: route.request().url(),
        headers: route.request().headers(),
      });
      if (route.request().method() !== "GET") {
        status = 405;
        body = { error: "method_not_allowed" };
      } else if (!session?.sessionToken) {
        status = 401;
        body = { error: "login_required" };
      } else {
        body = {
          ok: true,
          authenticated: true,
          account: String(session.nodeName || "").toLowerCase(),
          allowedFloorIds: ["lobby", "marketing", "rooftop"],
          teams: [],
          ...(officeFloorAccess || {}),
        };
      }
      if (Number(officeFloorDelayMs) > 0) {
        await new Promise((resolve) =>
          setTimeout(resolve, Number(officeFloorDelayMs)),
        );
      }
    } else if (url.pathname === "/api/world/office/general/entry") {
      let requestBody = {};
      try {
        requestBody = route.request().postDataJSON();
      } catch (_) {}
      officeEntryRequests.push({
        method: route.request().method(),
        url: route.request().url(),
        headers: route.request().headers(),
        body: requestBody,
      });
      if (route.request().method() !== "POST") {
        status = 405;
        body = {
          ok: false,
          error: "method_not_allowed",
        };
      } else if (!session?.sessionToken) {
        status = 401;
        body = { ok: false, error: "login_required" };
      } else {
        body = {
          ok: true,
          entryTicket: OFFICE_ENTRY_TICKET,
          expiresAt: FIXED_NOW + 60_000,
        };
      }
    } else if (accountFixture && url.pathname === "/api/accounts/signup") {
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
        repositoryFollowerFixture &&
        url.pathname === `${repoBase}/about`
      ) {
        if (!Array.isArray(repositoryFollowerFixture.requests)) {
          repositoryFollowerFixture.requests = [];
        }
        repositoryFollowerFixture.requests.push({
          path: url.pathname,
          method: route.request().method(),
        });
        body = repositoryFollowerFixture.unavailable
          ? { ok: false, error: "fixture_unavailable" }
          : {
              ok: true,
              description: "Peer-to-peer code hosting mesh",
              fediverse: {
                enabled: true,
                handle: "@forkmesh.forkmesh@forkmesh.com",
                followers: Number(repositoryFollowerFixture.followers ?? 0),
                followersList: repositoryFollowerFixture.followersList || [],
              },
            };
      } else if (
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
        const repositoryOwners =
          repositoryFixture.staleOfflineAlias ||
          repositoryFixture.sourceUserOwner
          ? ["jett", "mirror2", "mirror3"]
          : ["mirror2", "mirror3"];
        body = {
          repositories: repositoryOwners.map((owner) => {
            const stale =
              repositoryFixture.staleOfflineAlias && owner === "jett";
            const source = owner === "jett";
            const conflicting =
              repositoryFixture.conflictingHealthyAlias &&
              owner === "mirror3";
            const missingStateHash =
              repositoryFixture.missingHealthyStateHash &&
              owner === "mirror3";
            return {
              owner,
              name: "forkmesh",
              source: source ? "local-node" : "remote-clone",
              nodeId:
                repositoryFixture.sourceUserOwner && source
                  ? "source-node-id"
                  : owner,
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
        if (repositoryFixture.sourceUserOwner) mirrorNodes.unshift("forkmesh");
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
              id: node === "forkmesh" ? "source-node-id" : node,
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

async function moveToOfficeEntrance(page, { unpause = false } = {}) {
  await page.locator("forkmesh-world").evaluate(
    (shell, { position, shouldUnpause }) => {
      if (shouldUnpause) shell.world.setPaused(false);
      shell.world.player.position.set(...position);
    },
    {
      position: OFFICE_ENTRANCE_POSITION,
      shouldUnpause: unpause,
    },
  );
}

async function walkIntoOffice(page) {
  await moveToOfficeEntrance(page, { unpause: true });
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setControl("forward", true);
  });
  try {
    await waitForOfficeEntry(page);
  } finally {
    await page.locator("forkmesh-world").evaluate((shell) => {
      shell.world.setControl("forward", false);
    });
  }
}

async function officeSceneState(page) {
  return page.locator("forkmesh-world").evaluate((shell) => {
    const slidingDoorCount = [
      "forkmesh-office-sliding-door-left",
      "forkmesh-office-sliding-door-right",
    ].filter((name) => shell.world.scene.getObjectByName(name)).length;
    const island = shell.world.scene.getObjectByName(
      "forkmesh-office-island",
    );
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior",
    );
    return {
      slidingDoorCount,
      hasHingedDoor: Boolean(
        shell.world.scene.getObjectByName("forkmesh-office-door-pivot"),
      ),
      hasKeypad: Boolean(
        shell.world.scene.getObjectByName("forkmesh-office-keypad"),
      ),
      islandVisible: island?.visible === true,
      interiorVisible: interior?.visible === true,
      space: shell.world.getPosition().space,
    };
  });
}

async function waitForOfficeEntry(page) {
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => ({
      active: shell.officeController.active,
      space: shell.world.getPosition().space,
    }))
  ).toEqual({
    active: true,
    space: "office-lobby",
  });
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

test("World prompt button copies owner deploy and member PR workflows", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await page.addInitScript(() => {
    window.__worldCopiedPrompts = [];
    Object.defineProperty(navigator, "clipboard", {
      configurable: true,
      value: {
        writeText: async (text) => {
          window.__worldCopiedPrompts.push(text);
        },
      },
    });
  });
  await prepareWorldPage(page, "world-mcp-prompt", {
    session: {
      nodeName: "jett",
      sessionToken: "playwright-jett-session",
    },
  });
  await waitForWorld(page);

  const promptButton = page.getByRole("button", {
    name: "Copy MCP task prompt",
  });
  await expect(promptButton).toContainText("Prompt");
  await page.locator("forkmesh-world").evaluate(async (shell) => {
    shell.sessionAuthenticated = true;
    shell.organizations = [{ name: "forkmesh", role: "owner" }];
    shell.postJSON = async (path, body) => {
      window.__worldPromptRequest = { path, body };
      return { token: "fmbot_owner_test" };
    };
    await shell.copyMcpTaskPrompt(
      shell.querySelector("[data-world-mcp-prompt]"),
    );
  });
  await expect.poll(() =>
    page.evaluate(() => window.__worldCopiedPrompts.length)
  ).toBe(1);
  await expect(promptButton).toBeEnabled();

  const owner = await page.evaluate(() => ({
    prompt: window.__worldCopiedPrompts[0],
    request: window.__worldPromptRequest,
  }));
  expect(owner.request.path).toBe("/api/orgs/forkmesh/bot-tokens");
  expect(owner.request.body.scopes).toEqual([
    "organization.tasks.read",
    "organization.tasks.write",
  ]);
  expect(owner.request.body.expiresDays).toBe(1);
  expect(owner.prompt).toContain("merge only when the merge");
  expect(owner.prompt).toContain("then deploy");
  expect(owner.prompt).not.toContain("Do not merge or deploy");

  await page.locator("forkmesh-world").evaluate(async (shell) => {
    shell.organizations = [{ name: "forkmesh", role: "member" }];
    shell.postJSON = async () => ({ token: "fmbot_member_test" });
    await shell.copyMcpTaskPrompt(
      shell.querySelector("[data-world-mcp-prompt]"),
    );
  });
  await expect.poll(() =>
    page.evaluate(() => window.__worldCopiedPrompts.length)
  ).toBe(2);
  const memberPrompt = await page.evaluate(
    () => window.__worldCopiedPrompts[1],
  );
  expect(memberPrompt).toContain("open a focused pull request");
  expect(memberPrompt).toContain("Do not merge or deploy");
  expect(memberPrompt).not.toContain("then deploy");
});

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

test("collapsed CHAT bar counts unread remote lines but never your own", async ({
  page,
}) => {
  await prepareWorldPage(page, "world-chat-unread", {
    chatPassphrase: "playwright-public-world-general-passphrase",
  });
  await waitForWorld(page);

  const badge = page.locator("[data-world-chat-terminal-unread]");
  await expect(badge).toBeHidden();

  // The embedded /dashboard/chat iframe mirrors every line to the World with
  // postMessage; drive that same path directly.
  const mirror = (line) =>
    page.evaluate((message) => {
      window.postMessage(
        { type: "forkmesh:world-chat", ...message },
        location.origin,
      );
    }, line);

  // Replayed history only refreshes the newest-line label.
  await mirror({ sender: "peer", text: "old news", history: true });
  await expect(badge).toBeHidden();

  await mirror({ sender: "peer", text: "hello there" });
  await expect(badge).toHaveText("1");
  await mirror({ sender: "peer", text: "anyone around?" });
  await expect(badge).toHaveText("2");

  // Your own lines — from this browser (self) or the same account elsewhere
  // (own) — are already read.
  await mirror({ sender: "me", text: "on my way", self: true });
  await mirror({ sender: "me", text: "from my phone", own: true });
  await expect(badge).toHaveText("2");

  // Opening the bar clears it, and an open bar never accumulates.
  await page.locator("[data-world-chat-terminal]").evaluate((element) => {
    element.open = true;
  });
  await expect(badge).toBeHidden();
  await mirror({ sender: "peer", text: "still talking" });
  await expect(badge).toBeHidden();
});

test("chat launcher opens on hover with messages and left-aligned channels", async ({
  page,
}) => {
  test.slow();
  const passphrase = "playwright-world-hover-chat-passphrase";
  const key = worldChatKey(passphrase);
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      socket.send(
        JSON.stringify(
          encryptWorldChatEnvelope(
            {
              type: "chat",
              id: "hover-history-1",
              senderId: "hover-guest",
              sender: "Hover Guest",
              accountKind: "guest",
              channel: "#general",
              text: "The latest chats open with the launcher.",
              ts: FIXED_NOW - 1_000,
            },
            key,
            1,
          ),
        ),
      );
      socket.send(JSON.stringify({
        kind: "forkmesh-history-end",
        v: 1,
      }));
    },
  );
  await prepareWorldPage(page, "world-quick-composer", {
    chatPassphrase: passphrase,
  });
  await waitForWorld(page);

  const terminal = page.locator("[data-world-chat-terminal]");
  const summary = terminal.locator("summary");
  const collapsed = await summary.evaluate((element) => {
    const rect = element.getBoundingClientRect();
    const marker = getComputedStyle(
      element.querySelector(".world-chat-terminal-avatar"),
      "::after",
    );
    return {
      width: Math.round(rect.width),
      height: Math.round(rect.height),
      marker: marker.display,
    };
  });
  expect(collapsed).toEqual({ width: 56, height: 56, marker: "none" });

  const summaryBox = await summary.boundingBox();
  expect(summaryBox).not.toBeNull();
  await page.mouse.move(
    summaryBox.x + summaryBox.width / 2,
    summaryBox.y + summaryBox.height / 2,
  );
  await expect(terminal).toHaveAttribute("open", "");
  await expect(page.locator("[data-world-quick-composer-avatar]")).toBeVisible();
  await expect(terminal).toHaveAttribute("data-show-feed", "true");
  await expect(page.locator("[data-world-quick-chat-feed]")).toBeVisible();
  await expect(
    page.locator("#fullChatMessages .chat-message-row"),
  ).toContainText("The latest chats open with the launcher.");

  const channelLayout = await page
    .locator("[data-world-quick-channels]")
    .evaluate((element) => {
      const style = getComputedStyle(element);
      const rect = element.getBoundingClientRect();
      const first = element.querySelector("button")?.getBoundingClientRect();
      const second = element
        .querySelector("button:nth-of-type(2)")
        ?.getBoundingClientRect();
      const feed = element
        .parentElement
        ?.querySelector("[data-world-quick-chat-feed]")
        ?.getBoundingClientRect();
      return {
        display: style.display,
        flexDirection: style.flexDirection,
        channelLeft: Math.round(rect.left),
        firstLeft: Math.round(first?.left || 0),
        firstTop: Math.round(first?.top || 0),
        secondLeft: Math.round(second?.left || 0),
        secondTop: Math.round(second?.top || 0),
        feedLeft: Math.round(feed?.left || 0),
        feedTop: Math.round(feed?.top || 0),
      };
    });
  expect(channelLayout).toMatchObject({
    display: "flex",
    flexDirection: "row",
  });
  expect(channelLayout.firstLeft).toBeLessThanOrEqual(
    channelLayout.channelLeft + 8,
  );
  expect(channelLayout.firstLeft).toBeLessThanOrEqual(
    channelLayout.feedLeft + 8,
  );
  expect(channelLayout.secondLeft).toBeGreaterThan(channelLayout.firstLeft);
  expect(channelLayout.secondTop).toBe(channelLayout.firstTop);
  expect(channelLayout.feedTop).toBeGreaterThan(channelLayout.firstTop);

  await page.mouse.move(0, 0);
  await terminal.evaluate((element) => {
    element.removeAttribute("open");
    element.classList.add("world-chat-terminal--idle");
  });
  await expect(page.locator(".world-chat-terminal-prompt-icon")).toBeVisible();
});

test("mobile World chat keeps its composer above the terminal bars", async ({
  page,
}) => {
  await page.setViewportSize({ width: 390, height: 667 });
  await prepareWorldPage(page, "mobile-world-chat", {
    chatPassphrase: "playwright-public-world-general-passphrase",
  });
  await waitForWorld(page);

  // Reproduce the worst case: DEBUG was expanded before the visitor opened the
  // native full-height chat.
  await page.locator("[data-world-diagnostics]").evaluate((element) => {
    element.open = true;
  });
  const terminal = page.locator("[data-world-chat-terminal]");
  await terminal.evaluate((element) => {
    element.open = true;
  });
  const input = page.locator("#fullChatInput");
  await expect(input).toBeVisible();
  await input.focus();

  // Approximate the visual viewport after a mobile keyboard opens.
  await page.setViewportSize({ width: 390, height: 320 });
  await page.waitForTimeout(100);

  const metrics = await input.evaluate((element) => {
    const composer = element.closest("[data-dashboard-chat-composer]");
    const terminal = element.closest("[data-world-chat-terminal]");
    const send = terminal?.querySelector("#fullChatSend");
    const inputRect = element.getBoundingClientRect();
    const composerRect = composer?.getBoundingClientRect();
    const terminalRect = terminal?.getBoundingClientRect();
    const sendRect = send?.getBoundingClientRect();
    return {
      innerHeight: window.innerHeight,
      inputBottom: inputRect.bottom,
      sendBottom: sendRect?.bottom || 0,
      composerBottom: composerRect?.bottom || 0,
      terminalBottom: terminalRect?.bottom || 0,
      fits:
        Boolean(terminalRect && composerRect && sendRect) &&
        composerRect.bottom <= terminalRect.bottom + 1 &&
        inputRect.bottom <= terminalRect.bottom + 1 &&
        sendRect.bottom <= terminalRect.bottom + 1,
    };
  });
  expect(metrics).toMatchObject({ innerHeight: 320, fits: true });
  expect(metrics.terminalBottom).toBeLessThanOrEqual(metrics.innerHeight + 1);
});

test("World chat keeps five replayed messages lazy and its prompt in view", async ({
  page,
}) => {
  test.slow();
  await page.setViewportSize({ width: 640, height: 900 });
  const passphrase = "playwright-world-history-window-passphrase";
  const key = worldChatKey(passphrase);
  const oversizedImage = Buffer.from(
    '<svg xmlns="http://www.w3.org/2000/svg" width="4096" height="2048">' +
      '<rect width="4096" height="2048" fill="#58a6ff"/></svg>',
    "utf8",
  ).toString("base64");
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      // Same-millisecond D1 rows are hash-tied, so mutations may be replayed
      // before their targets. Exercise that ordering explicitly.
      socket.send(JSON.stringify(encryptWorldChatEnvelope({
        type: "edit",
        id: "edit-history-12",
        target: "history-12",
        senderId: "guest-12",
        sender: "Guest 12",
        accountKind: "guest",
        text: "Edited retained message 12.",
        editedAt: FIXED_NOW - 500,
        ts: FIXED_NOW - 500,
      }, key, 40)));
      socket.send(JSON.stringify(encryptWorldChatEnvelope({
        type: "delete",
        id: "delete-history-13",
        target: "history-13",
        senderId: "guest-13",
        sender: "Guest 13",
        accountKind: "guest",
        ts: FIXED_NOW - 250,
      }, key, 41)));
      for (let index = 1; index <= 13; index += 1) {
        socket.send(JSON.stringify(encryptWorldChatEnvelope({
          type: "chat",
          id: `history-${index}`,
          senderId: `guest-${index}`,
          sender: `Guest ${index}`,
          accountKind: "guest",
          channel: "#general",
          text: `Retained message ${index}. ${"Lazy history stays smooth. ".repeat(12)}`,
          ts: FIXED_NOW - (13 - index) * 1_000,
          ...(index === 12
            ? {
                fileName: "oversized-history-image.svg",
                fileMime: "image/svg+xml",
                file: oversizedImage,
              }
            : {}),
        }, key, index)));
      }
      socket.send(JSON.stringify({
        kind: "forkmesh-history-end",
        v: 1,
      }));
    },
  );
  await prepareWorldPage(page, "world-chat-history-window", {
    chatPassphrase: passphrase,
  });
  await waitForWorld(page);

  const terminal = page.locator("[data-world-chat-terminal]");
  await terminal.evaluate((element) => {
    element.open = true;
  });
  await page.locator('[data-world-quick-channel="general"]').click();
  const messages = page.locator("#fullChatMessages .chat-message-row");
  await expect(messages).toHaveCount(5);
  await expect(page.locator(".chat-history-indicator")).toContainText(
    "7 earlier messages",
  );
  await expect(page.locator("#fullChatInput")).toBeInViewport();

  const geometry = await terminal.evaluate((element) => {
    const body = element.querySelector("[data-world-native-chat]");
    const composer = element.querySelector("[data-dashboard-chat-composer]");
    const input = element.querySelector("#fullChatInput");
    const terminalRect = element.getBoundingClientRect();
    const bodyRect = body.getBoundingClientRect();
    const composerRect = composer.getBoundingClientRect();
    const inputRect = input.getBoundingClientRect();
    return {
      viewportHeight: window.innerHeight,
      terminalTop: terminalRect.top,
      terminalHeight: terminalRect.height,
      terminalBottom: terminalRect.bottom,
      bodyBottom: bodyRect.bottom,
      composerBottom: composerRect.bottom,
      inputBottom: inputRect.bottom,
      terminalScrollHeight: element.scrollHeight,
      terminalClientHeight: element.clientHeight,
    };
  });
  expect(geometry.terminalBottom).toBeLessThanOrEqual(
    geometry.viewportHeight + 1,
  );
  expect(geometry.terminalTop).toBeGreaterThanOrEqual(
    geometry.viewportHeight * 0.35,
  );
  expect(geometry.terminalHeight).toBeLessThanOrEqual(
    geometry.viewportHeight * 0.6,
  );
  expect(geometry.bodyBottom).toBeLessThanOrEqual(
    geometry.terminalBottom + 1,
  );
  expect(geometry.composerBottom).toBeLessThanOrEqual(
    geometry.terminalBottom + 1,
  );
  expect(geometry.inputBottom).toBeLessThanOrEqual(
    geometry.terminalBottom + 1,
  );
  expect(geometry.terminalScrollHeight).toBeLessThanOrEqual(
    geometry.terminalClientHeight + 1,
  );
  const attachment = page.locator(
    "#fullChatMessages .chat-message-row",
    { hasText: "Edited retained message 12." },
  );
  const attachmentImage = attachment.locator(".chat-attachment-image");
  await expect(attachmentImage).toBeVisible();
  await attachmentImage.evaluate((image) => {
    if (image.complete) return;
    return new Promise((resolve) => {
      image.addEventListener("load", resolve, { once: true });
      image.addEventListener("error", resolve, { once: true });
    });
  });
  const attachmentBounds = await attachment.evaluate((row) => {
    const bubble = row.querySelector(".chat-message-bubble");
    const wrapper = row.querySelector(".chat-attachment-wrapper");
    const image = row.querySelector(".chat-attachment-image");
    const bubbleRect = bubble.getBoundingClientRect();
    const wrapperRect = wrapper.getBoundingClientRect();
    const imageRect = image.getBoundingClientRect();
    return {
      bubbleLeft: bubbleRect.left,
      bubbleRight: bubbleRect.right,
      wrapperLeft: wrapperRect.left,
      wrapperRight: wrapperRect.right,
      imageLeft: imageRect.left,
      imageRight: imageRect.right,
      bubbleClientWidth: bubble.clientWidth,
      bubbleScrollWidth: bubble.scrollWidth,
    };
  });
  expect(attachmentBounds.wrapperLeft).toBeGreaterThanOrEqual(
    attachmentBounds.bubbleLeft - 1,
  );
  expect(attachmentBounds.wrapperRight).toBeLessThanOrEqual(
    attachmentBounds.bubbleRight + 1,
  );
  expect(attachmentBounds.imageLeft).toBeGreaterThanOrEqual(
    attachmentBounds.bubbleLeft - 1,
  );
  expect(attachmentBounds.imageRight).toBeLessThanOrEqual(
    attachmentBounds.bubbleRight + 1,
  );
  expect(attachmentBounds.bubbleScrollWidth).toBeLessThanOrEqual(
    attachmentBounds.bubbleClientWidth + 1,
  );

  await page.locator("#fullChatMessages").evaluate((element) => {
    element.scrollTop = 0;
    element.dispatchEvent(new WheelEvent("wheel", { deltaY: -120 }));
    element.dispatchEvent(new WheelEvent("wheel", { deltaY: -120 }));
  });
  await expect(messages).toHaveCount(10);
  await expect(page.locator(".chat-history-indicator")).toContainText(
    "2 earlier messages",
  );
});

test("ForkMesh Office walk-in opens chat only through the explicit fallback", async ({
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
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-office-token",
    },
  });
  await waitForWorld(page);

  const worldURL = page.url();
  const pageCount = context.pages().length;
  // The World's always-present embedded global chat connects independently of
  // the Office. Capture that baseline so this journey proves that focusing or
  // approaching the Office does not open an additional Office chat transport.
  const globalChatSocketCount = chatSocketURLs.length;
  expect(globalChatSocketCount).toBeGreaterThanOrEqual(1);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.focusLandmark("office");
  });
  expect(chatSocketURLs).toHaveLength(globalChatSocketCount);

  await moveToOfficeEntrance(page);
  await expect(page.locator("[data-world-office-prompt]")).toHaveCount(0);
  expect(chatSocketURLs).toHaveLength(globalChatSocketCount);
  await walkIntoOffice(page);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.officeController.openFallback();
  });

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

test("Office runtime stays off the initial World graph and loads on demand", async ({
  page,
}) => {
  const officeModules = new Set([
    "/world/world-office.js",
    "/world/world-office-meeting.js",
    "/world/world-office-tasks.js",
  ]);
  const requestedOfficeModules = [];
  page.on("request", (request) => {
    const path = new URL(request.url()).pathname;
    if (officeModules.has(path)) requestedOfficeModules.push(path);
  });
  await prepareWorldPage(page, "office-lazy-runtime");
  await waitForWorld(page);
  expect(requestedOfficeModules).toEqual([]);

  const runtime = await page.locator("forkmesh-world").evaluate(async (shell) => {
    const controller = await shell.ensureOfficeRuntime();
    return {
      controller: Boolean(controller),
      meeting: Boolean(shell.officeMeeting),
      tasks: Boolean(shell.officeTasks),
    };
  });
  expect(runtime).toEqual({
    controller: true,
    meeting: true,
    tasks: true,
  });
  expect(new Set(requestedOfficeModules)).toEqual(officeModules);
});

test("a signed-in member walks into the continuous ten-story Office without a gate", async ({
  page,
}) => {
  test.slow();
  const officeEntryRequests = [];
  const officeFloorRequests = [];
  const officeAttendanceRequests = [];
  const signedVisit = {
    id: "signed-in",
    account: "alice",
    inAt: FIXED_NOW,
    outAt: 0,
  };
  await prepareWorldPage(page, "office-member-entry", {
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-office-token",
    },
    officeEntryRequests,
    officeFloorRequests,
    officeAttendanceRequests,
    officeAttendanceFixture: {
      getVisits: [{
        id: "stale-get",
        account: "stale",
        inAt: FIXED_NOW - 60_000,
        outAt: FIXED_NOW - 30_000,
      }],
      inVisits: [signedVisit],
    },
  });
  await waitForWorld(page);
  await moveToOfficeEntrance(page);
  await page.locator("forkmesh-world").evaluate((shell) => {
    const setAttendance = shell.world.setOfficeAttendance.bind(shell.world);
    shell.__officeAttendanceSnapshots = [];
    shell.world.setOfficeAttendance = (snapshot) => {
      shell.__officeAttendanceSnapshots.push(
        structuredClone(snapshot),
      );
      return setAttendance(snapshot);
    };
  });

  await expect(page.locator("[data-world-office-prompt]")).toHaveCount(0);
  await expect(page.locator("[data-world-office-enter]")).toHaveCount(0);
  await expect.poll(() => officeSceneState(page)).toEqual({
    slidingDoorCount: 2,
    hasHingedDoor: false,
    hasKeypad: false,
    islandVisible: true,
    interiorVisible: true,
    space: "town-square",
  });

  await walkIntoOffice(page);
  await expect(page.locator("[data-world-office-lobby]")).toBeHidden();
  expect(officeEntryRequests).toHaveLength(0);
  await expect.poll(() => officeFloorRequests.length).toBe(1);
  expect(officeFloorRequests).toEqual([
    expect.objectContaining({
      method: "GET",
      headers: expect.objectContaining({
        authorization: "Bearer alice-office-token",
      }),
    }),
  ]);
  await expect.poll(() => officeAttendanceRequests.length).toBe(1);
  expect(officeAttendanceRequests).toEqual([
    expect.objectContaining({
      method: "POST",
      body: { action: "in" },
      headers: expect.objectContaining({
        authorization: "Bearer alice-office-token",
      }),
    }),
  ]);
  expect(officeAttendanceRequests.some(({ method }) => method === "GET"))
    .toBe(false);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.__officeAttendanceSnapshots,
    )
  ).toEqual([{ visits: [signedVisit] }]);
  await expect.poll(() => officeSceneState(page)).toEqual({
    slidingDoorCount: 2,
    hasHingedDoor: false,
    hasKeypad: false,
    islandVisible: true,
    interiorVisible: true,
    space: "office-lobby",
  });
});

test("guests walk directly into the public Office lobby without network admission", async ({
  page,
}) => {
  test.slow();
  const officeEntryRequests = [];
  const officeFloorRequests = [];
  const officeAttendanceRequests = [];
  const publicVisit = {
    id: "public-last-visit",
    account: "contributor",
    inAt: FIXED_NOW - 120_000,
    outAt: FIXED_NOW - 60_000,
  };
  await prepareWorldPage(page, "office-guest-entry", {
    officeEntryRequests,
    officeFloorRequests,
    officeAttendanceRequests,
    officeAttendanceFixture: {
      getVisits: [publicVisit],
    },
  });
  await waitForWorld(page);
  await page.locator("forkmesh-world").evaluate((shell) => {
    const setAttendance = shell.world.setOfficeAttendance.bind(shell.world);
    shell.__officeAttendanceSnapshots = [];
    shell.world.setOfficeAttendance = (snapshot) => {
      shell.__officeAttendanceSnapshots.push(
        structuredClone(snapshot),
      );
      return setAttendance(snapshot);
    };
  });
  await walkIntoOffice(page);
  await expect(page.locator("[data-world-office-lobby]")).toBeHidden();
  await expect(page.locator("[data-world-office-prompt]")).toHaveCount(0);
  await expect(page.locator("[data-world-login-form]")).toBeHidden();
  expect(officeEntryRequests).toHaveLength(0);
  expect(officeFloorRequests).toHaveLength(0);
  await expect.poll(() => officeAttendanceRequests.length).toBe(1);
  expect(officeAttendanceRequests).toEqual([
    expect.objectContaining({
      method: "GET",
    }),
  ]);
  expect(officeAttendanceRequests.some(({ method }) => method === "POST"))
    .toBe(false);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.__officeAttendanceSnapshots,
    )
  ).toEqual([{ visits: [publicVisit] }]);
  expect(await officeSceneState(page)).toMatchObject({
    slidingDoorCount: 2,
    hasHingedDoor: false,
    hasKeypad: false,
    space: "office-lobby",
  });
});

test("Office entrance doors slide apart on approach and close after departure", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-sliding-doors");
  await waitForWorld(page);
  const positions = () =>
    page.locator("forkmesh-world").evaluate((shell) => {
      const left = shell.world.scene.getObjectByName(
        "forkmesh-office-sliding-door-left"
      );
      const right = shell.world.scene.getObjectByName(
        "forkmesh-office-sliding-door-right"
      );
      return {
        left: Number(left?.position.x || 0),
        right: Number(right?.position.x || 0),
      };
    });
  const closed = await positions();
  expect(closed.left).toBeLessThan(0);
  expect(closed.right).toBeGreaterThan(0);

  await moveToOfficeEntrance(page, { unpause: true });
  await expect.poll(async () => Math.abs((await positions()).right))
    .toBeGreaterThan(Math.abs(closed.right) + 1);

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.player.position.set(0, 0.38, 0);
  });
  await expect.poll(async () => {
    const next = await positions();
    return Math.abs(next.right - closed.right);
  }).toBeLessThan(0.08);
});

test("Office bridge, approach, and lobby meet on one continuous plane", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-level-threshold");
  await waitForWorld(page);
  const surfaces = await page.locator("forkmesh-world").evaluate((shell) => {
    const bounds = (name) => {
      const object = shell.world.scene.getObjectByName(name);
      object.geometry.computeBoundingBox();
      const box = object.geometry.boundingBox;
      const corners = [];
      for (const x of [box.min.x, box.max.x]) {
        for (const y of [box.min.y, box.max.y]) {
          for (const z of [box.min.z, box.max.z]) {
            corners.push(
              object.localToWorld(object.position.clone().set(x, y, z)),
            );
          }
        }
      }
      return {
        top: Math.max(...corners.map((point) => point.y)),
        minZ: Math.min(...corners.map((point) => point.z)),
        maxZ: Math.max(...corners.map((point) => point.z)),
      };
    };
    return {
      bridge: bounds("forkmesh-office-bridge-deck"),
      approach: bounds("forkmesh-office-island-approach-deck"),
      lobby: bounds("forkmesh-office-floor-slab-lobby-2"),
    };
  });

  expect(surfaces.bridge.top).toBeCloseTo(0.38, 6);
  expect(surfaces.approach.top).toBeCloseTo(0.38, 6);
  expect(surfaces.lobby.top).toBeCloseTo(0.38, 6);
  expect(surfaces.approach.minZ).toBeCloseTo(surfaces.lobby.maxZ, 6);
  expect(surfaces.bridge.minZ).toBeLessThan(surfaces.approach.maxZ);
});

test("Office lobby marine aquarium is visible, ambient, and animated", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-marine-aquarium");
  await page.emulateMedia({ reducedMotion: "no-preference" });
  await waitForWorld(page);
  const first = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.enterOfficeLobby({ floorId: "lobby" });
    shell.world.setPaused(false);
    const aquarium = shell.world.scene.getObjectByName(
      "forkmesh-office-marine-aquarium",
    );
    const fish = aquarium.getObjectByName(
      "forkmesh-office-aquarium-fish-clownfish-0",
    );
    const tail = aquarium.getObjectByName(
      "forkmesh-office-aquarium-fish-tail-clownfish",
    );
    const bubble = aquarium.getObjectByName(
      "forkmesh-office-aquarium-bubble-0",
    );
    const softCoral = aquarium.getObjectByName(
      "forkmesh-office-aquarium-soft-coral-0",
    );
    const surface = aquarium.getObjectByName(
      "forkmesh-office-aquarium-water-surface",
    );
    const caustics = aquarium.getObjectByName(
      "forkmesh-office-aquarium-caustics",
    );
    const light = aquarium.getObjectByName("forkmesh-office-aquarium-light");
    const interactive = [];
    aquarium.traverse((child) => {
      if (child.userData?.interactive) interactive.push(child.name);
    });
    return {
      visible: aquarium.visible,
      fish: fish.position.toArray(),
      tailRotation: tail.rotation.y,
      bubbleY: bubble.position.y,
      coralRotation: softCoral.rotation.z,
      surfaceHeight: surface.geometry.getAttribute("position").getY(0),
      causticOpacity: caustics.children[0].material.opacity,
      lightIntensity: light.intensity,
      interactive,
    };
  });
  await page.waitForTimeout(220);
  const second = await page.locator("forkmesh-world").evaluate((shell) => {
    const aquarium = shell.world.scene.getObjectByName(
      "forkmesh-office-marine-aquarium",
    );
    const fish = aquarium.getObjectByName(
      "forkmesh-office-aquarium-fish-clownfish-0",
    );
    const tail = aquarium.getObjectByName(
      "forkmesh-office-aquarium-fish-tail-clownfish",
    );
    const bubble = aquarium.getObjectByName(
      "forkmesh-office-aquarium-bubble-0",
    );
    const softCoral = aquarium.getObjectByName(
      "forkmesh-office-aquarium-soft-coral-0",
    );
    const surface = aquarium.getObjectByName(
      "forkmesh-office-aquarium-water-surface",
    );
    const caustics = aquarium.getObjectByName(
      "forkmesh-office-aquarium-caustics",
    );
    const light = aquarium.getObjectByName("forkmesh-office-aquarium-light");
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior",
    );
    const cameraPosition = interior.localToWorld(
      shell.world.camera.position.clone().set(-58, 8.2, 13),
    );
    const target = interior.localToWorld(
      shell.world.camera.position.clone().set(-82.4, 6.1, -10.5),
    );
    shell.world.setPaused(true);
    shell.world.camera.fov = 52;
    shell.world.camera.updateProjectionMatrix();
    shell.world.camera.position.copy(cameraPosition);
    shell.world.camera.lookAt(target);
    shell.world.renderer.render(shell.world.scene, shell.world.camera);
    return {
      fish: fish.position.toArray(),
      tailRotation: tail.rotation.y,
      bubbleY: bubble.position.y,
      coralRotation: softCoral.rotation.z,
      surfaceHeight: surface.geometry.getAttribute("position").getY(0),
      causticOpacity: caustics.children[0].material.opacity,
      lightIntensity: light.intensity,
    };
  });
  await page.locator("canvas.world-canvas").screenshot({
    path: "/tmp/forkmesh-office-cinematic-reef-close.png",
    animations: "disabled",
  });
  await page.locator("forkmesh-world").evaluate((shell) => {
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior",
    );
    const cameraPosition = interior.localToWorld(
      shell.world.camera.position.clone().set(-42, 10.5, 19),
    );
    const target = interior.localToWorld(
      shell.world.camera.position.clone().set(-78, 5.2, -7),
    );
    shell.world.camera.fov = 59;
    shell.world.camera.updateProjectionMatrix();
    shell.world.camera.position.copy(cameraPosition);
    shell.world.camera.lookAt(target);
    shell.world.renderer.render(shell.world.scene, shell.world.camera);
  });
  await page.locator("canvas.world-canvas").screenshot({
    path: "/tmp/forkmesh-office-cinematic-reef-lobby.png",
    animations: "disabled",
  });
  expect(first.visible).toBe(true);
  expect(first.interactive).toEqual([]);
  expect(second.fish).not.toEqual(first.fish);
  expect(second.tailRotation).not.toBe(first.tailRotation);
  expect(second.bubbleY).not.toBe(first.bubbleY);
  expect(second.coralRotation).not.toBe(first.coralRotation);
  expect(second.surfaceHeight).not.toBe(first.surfaceHeight);
  expect(second.causticOpacity).not.toBe(first.causticOpacity);
  expect(second.lightIntensity).not.toBe(first.lightIntensity);
});

test("aquarium blocks lobby movement and double-click travel", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-aquarium-collision");
  await waitForWorld(page);
  const placeAtGlass = () =>
    page.locator("forkmesh-world").evaluate((shell) => {
      shell.world.enterOfficeLobby({ floorId: "lobby" });
      shell.world.setCameraView({
        mode: "third-person",
        yaw: Math.PI / 2,
        pitch: 0.35,
        zoom: 0.9,
      });
      const interior = shell.world.scene.getObjectByName(
        "forkmesh-office-interior",
      );
      const position = interior.localToWorld(
        shell.world.player.position.clone().set(-79.7, 0.38, -10.5),
      );
      shell.world.player.position.copy(position);
      shell.world.setPaused(false);
    });

  const readLocalPosition = () =>
    page.locator("forkmesh-world").evaluate((shell) => {
      const interior = shell.world.scene.getObjectByName(
        "forkmesh-office-interior",
      );
      return interior.worldToLocal(
        shell.world.player.getWorldPosition(shell.world.player.position.clone()),
      ).toArray();
    });

  await placeAtGlass();
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setControl("forward", true);
  });
  await page.waitForTimeout(450);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setControl("forward", false);
  });
  const walked = await readLocalPosition();

  await placeAtGlass();
  await page.locator("forkmesh-world").evaluate((shell) => {
    const canvas = shell.world.renderer.domElement;
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior",
    );
    const target = interior.localToWorld(
      shell.world.player.position.clone().set(-82.9, 0.38, -10.5),
    );
    target.project(shell.world.camera);
    const bounds = canvas.getBoundingClientRect();
    canvas.dispatchEvent(new MouseEvent("dblclick", {
      bubbles: true,
      button: 0,
      clientX: bounds.left + (target.x * 0.5 + 0.5) * bounds.width,
      clientY: bounds.top + (-target.y * 0.5 + 0.5) * bounds.height,
    }));
  });
  await page.waitForTimeout(450);
  const dashed = await readLocalPosition();
  const aquariumFrontLimit = -81.24 + 0.46;

  expect(walked[0]).toBeGreaterThanOrEqual(aquariumFrontLimit);
  expect(dashed[0]).toBeGreaterThanOrEqual(aquariumFrontLimit);
});

test("aquarium feeding appears nearby and expires after one minute", async ({
  page,
}) => {
  test.slow();
  await prepareWorldPage(page, "office-aquarium-feeding");
  await waitForWorld(page);
  const feedAction = page.locator("[data-world-aquarium-feed]");
  await expect(feedAction).toBeHidden();

  const before = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.enterOfficeLobby({ floorId: "lobby" });
    shell.world.setCameraView({
      mode: "third-person",
      yaw: Math.PI / 2,
      pitch: 0.3,
      zoom: 0.72,
    });
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior",
    );
    const position = interior.localToWorld(
      shell.world.player.position.clone().set(-78.8, 0.38, -10.5),
    );
    shell.world.player.position.copy(position);
    shell.world.setPaused(false);
    const aquarium = shell.world.scene.getObjectByName(
      "forkmesh-office-marine-aquarium",
    );
    return aquarium.getObjectByName(
      "forkmesh-office-aquarium-fish-clownfish-0",
    ).position.toArray();
  });

  await expect(feedAction).toBeVisible();
  await expect(feedAction).toHaveText("Feed the Fishes");
  await page.locator("forkmesh-world").screenshot({
    path: "/tmp/forkmesh-office-aquarium-feed-action.png",
    animations: "disabled",
  });
  await feedAction.click();
  await expect(feedAction).toBeDisabled();
  await expect(feedAction).toHaveText("Fishes are feeding");
  await page.waitForTimeout(220);
  await page.locator("forkmesh-world").screenshot({
    path: "/tmp/forkmesh-office-aquarium-feeding.png",
    animations: "disabled",
  });

  const active = await page.locator("forkmesh-world").evaluate((shell) => {
    const aquarium = shell.world.scene.getObjectByName(
      "forkmesh-office-marine-aquarium",
    );
    const food = aquarium.getObjectByName(
      "forkmesh-office-aquarium-food",
    );
    const fish = aquarium.getObjectByName(
      "forkmesh-office-aquarium-fish-clownfish-0",
    );
    const state = shell.world.getOfficeAquariumState();
    const fishPositions = aquarium.children
      .filter((child) =>
        child.name.startsWith("forkmesh-office-aquarium-fish-"),
      )
      .map((child) => child.position.toArray());
    const pelletPositions = food.children.map((child) =>
      child.position.toArray()
    );
    return {
      state,
      foodVisible: food.visible,
      fishPosition: fish.position.toArray(),
      fishPositions,
      pelletPositions,
      repeatStarted: shell.world.feedOfficeAquarium(),
    };
  });
  expect(active.state.active).toBe(true);
  expect(active.state.durationMs).toBe(60_000);
  expect(active.foodVisible).toBe(true);
  expect(active.repeatStarted).toBe(false);
  expect(active.fishPosition).not.toEqual(before);
  const fishZ = active.fishPositions.map((position) => position[2]);
  const pelletZ = active.pelletPositions.map((position) => position[2]);
  expect(Math.max(...fishZ) - Math.min(...fishZ)).toBeGreaterThan(6);
  expect(Math.max(...active.fishPositions.map((position) => position[1])))
    .toBeLessThan(9.6);
  expect(Math.max(...pelletZ) - Math.min(...pelletZ)).toBeGreaterThan(6.5);

  const expired = await page.locator("forkmesh-world").evaluate(
    (shell, endsAt) => {
      const aquarium = shell.world.scene.getObjectByName(
        "forkmesh-office-marine-aquarium",
      );
      const food = aquarium.getObjectByName(
        "forkmesh-office-aquarium-food",
      );
      const fish = aquarium.getObjectByName(
        "forkmesh-office-aquarium-fish-clownfish-0",
      );
      const feedingPosition = fish.position.toArray();
      const state = shell.world.getOfficeAquariumState(endsAt + 1);
      return {
        state,
        foodVisible: food.visible,
        feedingPosition,
      };
    },
    active.state.endsAt,
  );
  expect(expired.state.active).toBe(false);
  expect(expired.foodVisible).toBe(false);
  await expect(feedAction).toBeEnabled();
  await expect(feedAction).toHaveText("Feed the Fishes");
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => {
      const aquarium = shell.world.scene.getObjectByName(
        "forkmesh-office-marine-aquarium",
      );
      return aquarium.getObjectByName(
        "forkmesh-office-aquarium-fish-clownfish-0",
      ).position.toArray();
    })
  ).not.toEqual(expired.feedingPosition);
});

test("Office cinematic reef remains composed with reduced motion", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-cinematic-reef-reduced-motion");
  await page.emulateMedia({ reducedMotion: "reduce" });
  await waitForWorld(page);
  const state = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.enterOfficeLobby({ floorId: "lobby" });
    const aquarium = shell.world.scene.getObjectByName(
      "forkmesh-office-marine-aquarium",
    );
    const fish = aquarium.getObjectByName(
      "forkmesh-office-aquarium-fish-clownfish-0",
    );
    const bubble = aquarium.getObjectByName(
      "forkmesh-office-aquarium-bubble-0",
    );
    return {
      visible: aquarium.visible,
      fishPosition: fish.position.toArray(),
      bubblePosition: bubble.position.toArray(),
    };
  });
  await page.waitForTimeout(220);
  const settled = await page.locator("forkmesh-world").evaluate((shell) => {
    const aquarium = shell.world.scene.getObjectByName(
      "forkmesh-office-marine-aquarium",
    );
    return {
      fishPosition: aquarium.getObjectByName(
        "forkmesh-office-aquarium-fish-clownfish-0",
      ).position.toArray(),
      bubblePosition: aquarium.getObjectByName(
        "forkmesh-office-aquarium-bubble-0",
      ).position.toArray(),
    };
  });
  const feeding = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.feedOfficeAquarium();
    const aquarium = shell.world.scene.getObjectByName(
      "forkmesh-office-marine-aquarium",
    );
    return {
      state: shell.world.getOfficeAquariumState(),
      fishPosition: aquarium.getObjectByName(
        "forkmesh-office-aquarium-fish-clownfish-0",
      ).position.toArray(),
      tailRotation: aquarium.getObjectByName(
        "forkmesh-office-aquarium-fish-tail-clownfish",
      ).rotation.y,
      pelletPosition: aquarium.getObjectByName(
        "forkmesh-office-aquarium-food-pellet-0",
      ).position.toArray(),
    };
  });
  await page.waitForTimeout(220);
  const feedingSettled = await page.locator("forkmesh-world").evaluate(
    (shell) => {
      const aquarium = shell.world.scene.getObjectByName(
        "forkmesh-office-marine-aquarium",
      );
      return {
        fishPosition: aquarium.getObjectByName(
          "forkmesh-office-aquarium-fish-clownfish-0",
        ).position.toArray(),
        tailRotation: aquarium.getObjectByName(
          "forkmesh-office-aquarium-fish-tail-clownfish",
        ).rotation.y,
        pelletPosition: aquarium.getObjectByName(
          "forkmesh-office-aquarium-food-pellet-0",
        ).position.toArray(),
      };
    },
  );
  const expired = await page.locator("forkmesh-world").evaluate(
    (shell, endsAt) => {
      const state = shell.world.getOfficeAquariumState(endsAt + 1);
      const aquarium = shell.world.scene.getObjectByName(
        "forkmesh-office-marine-aquarium",
      );
      return {
        state,
        foodVisible: aquarium.getObjectByName(
          "forkmesh-office-aquarium-food",
        ).visible,
        fishPosition: aquarium.getObjectByName(
          "forkmesh-office-aquarium-fish-clownfish-0",
        ).position.toArray(),
      };
    },
    feeding.state.endsAt,
  );
  expect(state.visible).toBe(true);
  expect(state.fishPosition[1]).toBeGreaterThan(2);
  expect(state.bubblePosition[1]).toBeGreaterThan(0.8);
  expect(settled).toEqual({
    fishPosition: state.fishPosition,
    bubblePosition: state.bubblePosition,
  });
  expect(feeding.state.active).toBe(true);
  expect(feedingSettled).toEqual({
    fishPosition: feeding.fishPosition,
    tailRotation: feeding.tailRotation,
    pelletPosition: feeding.pelletPosition,
  });
  expect(expired.state.active).toBe(false);
  expect(expired.foodVisible).toBe(false);
  expect(expired.fishPosition).toEqual(state.fishPosition);
});

test("a first-frame doorway crossing enters before proximity catches up", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-first-frame-entry");
  await waitForWorld(page);

  const entry = await page.locator("forkmesh-world").evaluate(async (shell) => {
    // Freeze between the doorway collision and the later proximity ticker,
    // matching a fast dash/double-click arrival in that first frame.
    shell.world.setPaused(true);
    shell.world.player.position.set(0, 0.38, -169.53);
    const proximityBefore = shell.officeController.proximity;
    const nonDoorwayEntered = await shell.officeController.enterOffice({
      source: "door",
    });
    const entered = await shell.officeController.enterOffice({
      source: "doorway",
    });
    return {
      proximityBefore,
      nonDoorwayEntered,
      entered,
      active: shell.officeController.active,
      space: shell.world.getPosition().space,
    };
  });
  expect(entry).toEqual({
    proximityBefore: "distant",
    nonDoorwayEntered: false,
    entered: true,
    active: true,
    space: "office-lobby",
  });
});

test("walking through the Office doorway hydrates floor access without admission POST", async ({
  page,
}) => {
  const officeEntryRequests = [];
  const officeFloorRequests = [];
  await prepareWorldPage(page, "office-walk-in-entry", {
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-office-token",
    },
    officeEntryRequests,
    officeFloorRequests,
  });
  await waitForWorld(page);
  await walkIntoOffice(page);
  await expect.poll(() => officeFloorRequests.length).toBe(1);
  expect(officeEntryRequests).toHaveLength(0);
});

test("Office doorway stays outside at the jamb and never blocks on access hydration", async ({
  page,
}) => {
  test.slow();
  const officeFloorRequests = [];
  const officeAttendanceRequests = [];
  await prepareWorldPage(page, "office-doorway-background-access", {
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-office-token",
    },
    officeFloorRequests,
    officeAttendanceRequests,
    officeFloorDelayMs: 5_000,
  });
  await waitForWorld(page);

  const jambState = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setPaused(false);
    const status = shell.world.scene.getObjectByName(
      "forkmesh-office-door-status",
    );
    const doorway = status.getWorldPosition(status.position.clone());
    // The avatar's center is just through the glass, but its full collision
    // body has not cleared the inner jamb yet.
    shell.world.player.position.set(doorway.x, 0.38, doorway.z - 0.26);
    return {
      active: shell.officeController.active,
      space: shell.world.getPosition().space,
      status: status.userData.officeDoorStatus,
      visible: status.visible,
    };
  });
  expect(jambState).toEqual({
    active: false,
    space: "town-square",
    status: "open",
    visible: false,
  });
  await page.waitForTimeout(150);
  expect(officeFloorRequests).toHaveLength(0);
  expect(officeAttendanceRequests).toHaveLength(0);

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setControl("forward", true);
  });
  try {
    await waitForOfficeEntry(page);
    await expect.poll(() => officeFloorRequests.length).toBe(1);
    const whileSyncing = await page.locator("forkmesh-world").evaluate(
      (shell) => {
        const status = shell.world.scene.getObjectByName(
          "forkmesh-office-door-status",
        );
        return {
          position: shell.world.player.getWorldPosition(
            shell.world.player.position.clone(),
          ).z,
          status: status.userData.officeDoorStatus,
          visible: status.visible,
        };
      },
    );
    expect(whileSyncing.status).toBe("syncing");
    expect(whileSyncing.visible).toBe(true);

    await page.waitForTimeout(250);
    const movingPosition = await page.locator("forkmesh-world").evaluate(
      (shell) =>
        shell.world.player.getWorldPosition(
          shell.world.player.position.clone(),
        ).z,
    );
    expect(movingPosition).toBeLessThan(whileSyncing.position - 0.05);
    expect(officeAttendanceRequests).toHaveLength(0);
    await page.locator("forkmesh-world").evaluate((shell) => {
      shell.world.setControl("forward", false);
    });

    await expect.poll(() =>
      page.locator("forkmesh-world").evaluate((shell) =>
        shell.world.scene.getObjectByName(
          "forkmesh-office-door-status",
        ).userData.officeDoorStatus
      )
    ).toBe("ready");
    await expect.poll(() => officeAttendanceRequests.length).toBe(1);

    await page.locator("forkmesh-world").evaluate((shell) => {
      const status = shell.world.scene.getObjectByName(
        "forkmesh-office-door-status",
      );
      const doorway = status.getWorldPosition(status.position.clone());
      shell.world.player.position.set(doorway.x, 0.38, doorway.z - 0.8);
      shell.world.setControl("back", true);
    });
    await expect.poll(() =>
      page.locator("forkmesh-world").evaluate((shell) => ({
        active: shell.officeController.active,
        space: shell.world.getPosition().space,
        status: shell.world.scene.getObjectByName(
          "forkmesh-office-door-status",
        ).userData.officeDoorStatus,
        visible: shell.world.scene.getObjectByName(
          "forkmesh-office-door-status",
        ).visible,
      }))
    ).toEqual({
      active: false,
      space: "town-square",
      status: "open",
      visible: false,
    });
  } finally {
    await page.locator("forkmesh-world").evaluate((shell) => {
      shell.world.setControl("forward", false);
      shell.world.setControl("back", false);
    });
  }
});

test("Office doorway retries a rejected crossing without requiring backward movement", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-doorway-no-twitch");
  await waitForWorld(page);
  await moveToOfficeEntrance(page, { unpause: true });
  await page.locator("forkmesh-world").evaluate((shell) => {
    const original =
      shell.officeController.enterOffice.bind(shell.officeController);
    shell.__officeEntryAttempts = 0;
    shell.officeController.enterOffice = async (entry = {}) => {
      shell.__officeEntryAttempts += 1;
      if (shell.__officeEntryAttempts === 1) {
        // Match a transient rejected/interrupted handoff. The controller's
        // normal finally path clears pending; movement remains inward.
        shell.world.setOfficeDoorwayEntryPending(false);
        return false;
      }
      return original(entry);
    };
    shell.world.setControl("forward", true);
  });
  try {
    await waitForOfficeEntry(page);
    const result = await page.locator("forkmesh-world").evaluate((shell) => ({
      attempts: shell.__officeEntryAttempts,
      space: shell.world.getPosition().space,
    }));
    expect(result).toEqual({ attempts: 2, space: "office-lobby" });
  } finally {
    await page.locator("forkmesh-world").evaluate((shell) => {
      shell.world.setControl("forward", false);
    });
  }
});

test("Office entry preserves the live avatar and keeps zoom inside the tower", async ({
  page,
}) => {
  test.slow();
  await prepareWorldPage(page, "office-seamless-entry-camera", {
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-office-token",
    },
  });
  await waitForWorld(page);
  await moveToOfficeEntrance(page, { unpause: true });
  const before = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setCameraZoom(0.9);
    const originalEnterOffice =
      shell.officeController.enterOffice.bind(shell.officeController);
    const sample = () => {
      const interior = shell.world.scene.getObjectByName(
        "forkmesh-office-interior"
      );
      const playerWorld = shell.world.player.getWorldPosition(
        shell.world.player.position.clone()
      );
      const target = playerWorld.clone();
      target.y += 2.2;
      return {
        uuid: shell.world.player.uuid,
        local: interior.worldToLocal(playerWorld).toArray(),
        cameraDistance: shell.world.camera.position.distanceTo(target),
      };
    };
    shell.__officeThresholdHandoff = null;
    shell.officeController.enterOffice = (...args) => {
      const thresholdBefore = sample();
      const result = originalEnterOffice(...args);
      shell.__officeThresholdHandoff = {
        before: thresholdBefore,
        after: sample(),
      };
      return result;
    };
    return {
      uuid: shell.world.player.uuid,
      camera: shell.world.getCameraState(),
      position: shell.world.player.position.toArray(),
    };
  });

  await walkIntoOffice(page);
  await page.waitForTimeout(100);
  const after = await page.locator("forkmesh-world").evaluate((shell) => {
    const clone = shell.world.scene.getObjectByName(
      "forkmesh-office-lobby-player"
    );
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const target = shell.world.player.getWorldPosition(
      shell.world.player.position.clone()
    );
    target.y += 2.2;
    return {
      uuid: shell.world.player.uuid,
      camera: shell.world.getCameraState(),
      position: shell.world.player.position.toArray(),
      playerVisible: shell.world.player.visible,
      cloneVisible: clone?.visible === true,
      handoff: shell.__officeThresholdHandoff,
      cameraLocal: interior.worldToLocal(
        shell.world.camera.position.clone()
      ).toArray(),
      cameraDistance: shell.world.camera.position.distanceTo(target),
    };
  });
  expect(after.uuid).toBe(before.uuid);
  expect(after.camera.mode).toBe(before.camera.mode);
  expect(after.camera.yaw).toBeCloseTo(before.camera.yaw, 8);
  expect(after.camera.pitch).toBeCloseTo(before.camera.pitch, 8);
  expect(after.camera.zoom).toBeCloseTo(before.camera.zoom, 8);
  expect(after.playerVisible).toBe(true);
  expect(after.cloneVisible).toBe(false);
  expect(after.handoff.before.uuid).toBe(before.uuid);
  expect(after.handoff.after.uuid).toBe(before.uuid);
  expect(after.handoff.before.local[2]).toBeGreaterThan(45.46);
  expect(after.handoff.before.local[2]).toBeLessThan(46.5);
  expect(after.handoff.after.local[0])
    .toBeCloseTo(after.handoff.before.local[0], 7);
  expect(after.handoff.after.local[2])
    .toBeCloseTo(after.handoff.before.local[2], 7);
  expect(after.cameraLocal[2]).toBeGreaterThan(45);
  expect(after.cameraDistance).toBeGreaterThan(10);
  expect(after.cameraDistance)
    .toBeGreaterThan(after.handoff.before.cameraDistance * 0.45);
  expect(
    Math.hypot(
      after.position[0] - before.position[0],
      after.position[2] - before.position[2],
    ),
  ).toBeLessThan(6);

  // From just inside the same doorway, offset the target far enough that the
  // default orbit ray crosses the front plane beside the opening. It must hit
  // the facade clamp rather than inheriting the centered portal exception.
  await page.locator("forkmesh-world").evaluate((shell) => {
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const point = interior.localToWorld(
      shell.world.player.position.clone().set(4, 0.38, 43.7)
    );
    shell.world.player.parent.worldToLocal(point);
    shell.world.player.position.copy(point);
    shell.world.setCameraZoom(0.9);
  });
  await page.waitForTimeout(100);
  const facadeClamped = await page.locator("forkmesh-world").evaluate(
    (shell) => {
      const interior = shell.world.scene.getObjectByName(
        "forkmesh-office-interior"
      );
      const camera = interior.worldToLocal(
        shell.world.camera.position.clone()
      );
      const player = interior.worldToLocal(
        shell.world.player.getWorldPosition(
          shell.world.player.position.clone()
        )
      );
      player.y += 2.2;
      return {
        camera: camera.toArray(),
        distance: camera.distanceTo(player),
      };
    },
  );
  expect(facadeClamped.camera[2]).toBeLessThan(44.8);
  expect(facadeClamped.distance).toBeLessThan(2);

  await page.locator("forkmesh-world").evaluate((shell) => {
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const point = interior.localToWorld(
      shell.world.player.position.clone().set(0, 0.38, 0)
    );
    shell.world.player.parent.worldToLocal(point);
    shell.world.player.position.copy(point);
    shell.world.setCameraZoom(28);
  });
  await page.waitForTimeout(700);
  const bounded = await page.locator("forkmesh-world").evaluate((shell) => {
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const cameraLocal = interior.worldToLocal(
      shell.world.camera.position.clone()
    );
    const playerLocal = interior.worldToLocal(
      shell.world.player.getWorldPosition(shell.world.player.position.clone())
    );
    return {
      camera: cameraLocal.toArray(),
      player: playerLocal.toArray(),
      distance: cameraLocal.distanceTo(playerLocal),
      requestedZoom: shell.world.getCameraState().zoom,
    };
  });
  expect(bounded.requestedZoom).toBe(28);
  expect(bounded.camera[0]).toBeGreaterThan(-84.3);
  expect(bounded.camera[0]).toBeLessThan(84.3);
  expect(bounded.camera[1]).toBeGreaterThan(0.49);
  expect(bounded.camera[1]).toBeLessThan(15.46);
  expect(bounded.camera[2]).toBeGreaterThan(-44.3);
  expect(bounded.camera[2]).toBeLessThan(44.8);
  expect(bounded.distance).toBeGreaterThan(10);
  expect(bounded.distance).toBeLessThan(30);
});

test("Office rooftop restores the full world zoom outside the elevator", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-rooftop-full-zoom");
  await waitForWorld(page);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setOfficeAccess({
      authenticated: true,
      account: "alice",
      allowedFloorIds: ["lobby", "rooftop"],
    });
    shell.world.enterOfficeLobby();
    shell.world.enterOfficeLobby({ floorId: "rooftop" });
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const patioWorld = interior.localToWorld(
      shell.world.player.position.clone().set(0, 144.38, 0)
    );
    shell.world.player.parent.worldToLocal(patioWorld);
    shell.world.player.position.copy(patioWorld);
    shell.world.setCameraZoom(28);
    shell.world.setPaused(false);
  });

  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => {
      const target = shell.world.player
        .getWorldPosition(shell.world.player.position.clone());
      target.y += 2.2;
      return shell.world.camera.position.distanceTo(target);
    })
  ).toBeGreaterThan(700);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => ({
      space: shell.world.getPosition().space,
      zoom: shell.world.getCameraState().zoom,
    }))
  ).toEqual({
    space: "office-rooftop",
    zoom: 28,
  });

  // Pull the orbit eye below its target to look sharply upward. It may retain
  // full horizontal zoom, but it must never pass down through the roof slab.
  const canvas = page.locator("[data-world-canvas-wrap] canvas");
  const box = await canvas.boundingBox();
  expect(box).not.toBeNull();
  const x = Math.round(box.x + box.width * 0.5);
  await page.mouse.move(x, Math.round(box.y + box.height * 0.86));
  await page.mouse.down();
  await page.mouse.move(x, Math.round(box.y + box.height * 0.08), {
    steps: 4,
  });
  await page.mouse.up();
  await page.waitForTimeout(120);
  const upward = await page.locator("forkmesh-world").evaluate((shell) => {
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const camera = interior.worldToLocal(shell.world.camera.position.clone());
    const target = interior.worldToLocal(
      shell.world.player.getWorldPosition(shell.world.player.position.clone())
    );
    return {
      camera: camera.toArray(),
      horizontalDistance: Math.hypot(
        camera.x - target.x,
        camera.z - target.z,
      ),
      zoom: shell.world.getCameraState().zoom,
    };
  });
  expect(upward.camera[1]).toBeGreaterThanOrEqual(144.54);
  expect(upward.horizontalDistance).toBeGreaterThan(400);
  expect(upward.zoom).toBe(28);
});

test("Office rooftop has complete sittable patio furniture and a real source launcher", async ({
  page,
}) => {
  await page.addInitScript(() => {
    window.__officeLaptopOpened = [];
    window.open = (...args) => {
      window.__officeLaptopOpened.push(args);
      return null;
    };
  });
  await prepareWorldPage(page, "office-rooftop-patio");
  await waitForWorld(page);
  const patio = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setOfficeAccess({
      authenticated: true,
      account: "alice",
      allowedFloorIds: ["lobby", "marketing", "rooftop"],
    });
    shell.world.enterOfficeLobby();
    shell.world.enterOfficeLobby({ floorId: "rooftop" });
    const scene = shell.world.scene;
    const interior = scene.getObjectByName("forkmesh-office-interior");
    const tables = [];
    const chairs = [];
    scene.traverse((object) => {
      if (/^forkmesh-office-rooftop-table-\d+$/.test(object.name)) {
        tables.push({
          id: object.name,
          legs: object.children.filter((child) =>
            child.name.includes("-leg-")
          ).length,
        });
      }
      if (/^forkmesh-office-rooftop-chair-\d+$/.test(object.name)) {
        chairs.push({
          id: object.name,
          legs: object.children.filter((child) =>
            child.name.includes("-leg-")
          ).length,
          backs: object.children.filter((child) =>
            child.name.endsWith("-back")
          ).length,
        });
      }
    });
    const marketingRejected =
      shell.world.sitOnOfficeChair("chair-1");
    const sat = shell.world.sitOnOfficeChair("rooftop-chair-1");
    const chair = scene.getObjectByName(
      "forkmesh-office-rooftop-chair-1"
    );
    const seated = interior.worldToLocal(
      shell.world.player.getWorldPosition(shell.world.player.position.clone())
    );
    const toTable = {
      x: -40 - chair.position.x,
      z: 8 - chair.position.z,
    };
    const length = Math.hypot(toTable.x, toTable.z) || 1;
    const facingDot =
      (-Math.sin(shell.world.player.rotation.y) * (toTable.x / length)) +
      (-Math.cos(shell.world.player.rotation.y) * (toTable.z / length));
    const stood = shell.world.sitOnOfficeChair("rooftop-chair-1");
    shell.world.enterOfficeLobby({ floorId: "marketing" });
    const rooftopRejected =
      shell.world.sitOnOfficeChair("rooftop-chair-1");
    shell.world.enterOfficeLobby({ floorId: "rooftop" });
    return {
      tables,
      chairs,
      marketingRejected,
      rooftopRejected,
      sat,
      stood,
      seated: seated.toArray(),
      standingY: shell.world.player.position.y,
      facingDot,
      laptop: Boolean(scene.getObjectByName(
        "forkmesh-office-rooftop-laptop"
      )),
      laptopBase: Boolean(scene.getObjectByName(
        "forkmesh-office-rooftop-laptop-base"
      )),
      laptopScreen: Boolean(scene.getObjectByName(
        "forkmesh-office-rooftop-laptop-screen"
      )),
      strayTelescope: Boolean(scene.getObjectByName("telescopeTube")),
    };
  });
  expect(patio.tables).toHaveLength(3);
  expect(patio.tables.every((table) => table.legs === 4)).toBe(true);
  expect(patio.chairs).toHaveLength(12);
  expect(patio.chairs.every((chair) =>
    chair.legs === 4 && chair.backs === 1
  )).toBe(true);
  expect(patio).toMatchObject({
    marketingRejected: false,
    rooftopRejected: false,
    sat: true,
    stood: true,
    standingY: 144.38,
    laptop: true,
    laptopBase: true,
    laptopScreen: true,
    strayTelescope: false,
  });
  expect(patio.seated[1]).toBeGreaterThan(144);
  expect(patio.seated[1]).toBeLessThan(144.38);
  expect(patio.facingDot).toBeGreaterThan(0.99);

  const laptopPoint = await page.locator("forkmesh-world").evaluate((shell) => {
    const scene = shell.world.scene;
    const interior = scene.getObjectByName("forkmesh-office-interior");
    const screen = scene.getObjectByName(
      "forkmesh-office-rooftop-laptop-screen"
    );
    const target = screen.getWorldPosition(screen.position.clone());
    const camera = interior.localToWorld(
      screen.position.clone().set(0, 149, 17)
    );
    shell.world.setPaused(true);
    shell.world.camera.position.copy(camera);
    shell.world.camera.lookAt(target);
    shell.world.camera.updateMatrixWorld(true);
    shell.world.renderer.render(scene, shell.world.camera);
    const projected = target.clone().project(shell.world.camera);
    const rect = shell.world.renderer.domElement.getBoundingClientRect();
    return {
      x: rect.left + (projected.x * 0.5 + 0.5) * rect.width,
      y: rect.top + (-projected.y * 0.5 + 0.5) * rect.height,
    };
  });
  await page.mouse.click(laptopPoint.x, laptopPoint.y);
  await expect.poll(() =>
    page.evaluate(() => window.__officeLaptopOpened)
  ).toEqual([[
    "/forkmesh/forkmesh/blob/cloudflare_worker/public/world/world-scene.js",
    "_blank",
    "noopener,noreferrer",
  ]]);
  await expect(page.locator("[data-world-toast]")).toContainText(
    "Source edits stay in the desktop app or IDE extension"
  );
});

test("leaving an Office meeting resumes at a walkable first-person pose", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-meeting-walkable-handoff");
  await waitForWorld(page);
  const handoff = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setOfficeAccess({
      authenticated: true,
      account: "alice",
      allowedFloorIds: ["marketing"],
    });
    shell.world.enterOfficeLobby();
    shell.world.setCameraMode("first-person");
    shell.world.enterOfficeMeeting({
      roomName: "general",
      participantId: "local-me",
      participants: [{
        id: "local-me",
        name: "Alice",
        x: 0,
        z: 0,
        yaw: 0,
        pose: "standing",
      }],
    });
    let localAvatar = null;
    shell.world.scene.traverse((object) => {
      if (object.userData?.id === "local-me") localAvatar = object;
    });
    const localAvatarVisible = localAvatar?.visible === true;
    shell.world.enterOfficeLobby({ floorId: "marketing" });
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const playerLocal = interior.worldToLocal(
      shell.world.player.getWorldPosition(shell.world.player.position.clone())
    );
    return {
      localAvatarFound: Boolean(localAvatar),
      localAvatarVisible,
      playerLocal: playerLocal.toArray(),
      cameraMode: shell.world.getCameraState().mode,
      playerVisible: shell.world.player.visible,
    };
  });
  expect(handoff.localAvatarFound).toBe(true);
  expect(handoff.localAvatarVisible).toBe(false);
  expect(
    Math.abs(handoff.playerLocal[0]) > 4.36 ||
      Math.abs(handoff.playerLocal[2]) > 2.46,
  ).toBe(true);
  expect(handoff.playerLocal[1]).toBeCloseTo(16.38, 3);
  expect(handoff.cameraMode).toBe("first-person");
  expect(handoff.playerVisible).toBe(false);
});

test("Marketing studio furniture, wall features, reception, and open FM mark align", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-marketing-visual-alignment");
  await waitForWorld(page);
  const state = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setOfficeAccess({
      authenticated: true,
      account: "alice",
      allowedFloorIds: ["lobby", "marketing"],
    });
    shell.world.enterOfficeLobby();
    shell.world.enterOfficeLobby({ floorId: "marketing" });
    const scene = shell.world.scene;
    const interior = scene.getObjectByName("forkmesh-office-interior");
    const tabletop = scene.getObjectByName(
      "forkmesh-office-marketing-tabletop"
    );
    const legs = [];
    const chairs = [];
    scene.traverse((object) => {
      if (object.name === "forkmesh-office-marketing-table-leg") {
        legs.push(object);
      }
      if (/^forkmesh-office-chair-\d+$/.test(object.name)) {
        const position = object.position;
        const seatedYaw = object.rotation.y + Math.PI;
        const length = Math.hypot(position.x, position.z) || 1;
        chairs.push({
          id: object.name,
          backZ: object.getObjectByName(`${object.name}-back`)?.position.z,
          inwardDot:
            (-Math.sin(seatedYaw) * (-position.x / length)) +
            (-Math.cos(seatedYaw) * (-position.z / length)),
        });
      }
    });
    const tableBottom =
      tabletop.position.y - tabletop.geometry.parameters.height / 2;
    const highestLegTop = Math.max(...legs.map(
      (leg) => leg.position.y + leg.geometry.parameters.height / 2
    ));
    const taskBoard = scene.getObjectByName(
      "forkmesh-office-marketing-task-board"
    );
    const guideBoard = scene.getObjectByName("forkmesh-office-guide-board");
    const title = scene.getObjectByName(
      "forkmesh-office-marketing-wall-title"
    );
    const placards = [];
    scene.traverse((object) => {
      if (object.userData?.officeWallMounted === true) {
        placards.push({
          name: object.name,
          mesh: object.isMesh === true,
          sprite: object.isSprite === true,
        });
      }
    });
    const receptionDesk = scene.getObjectByName(
      "forkmesh-office-reception-desk"
    );
    const maya = scene.getObjectByName("avatar:office-greeter-maya");
    const noah = scene.getObjectByName("avatar:office-greeter-noah");
    let noahCount = 0;
    scene.traverse((object) => {
      if (object.name === "avatar:office-greeter-noah") noahCount += 1;
    });
    const chromeCube = scene.getObjectByName("forkmesh-reflective-fm-cube");
    const chromeMark = scene.getObjectByName(
      "forkmesh-reflective-fm-cube-fixed-tilt"
    );
    const support = scene.getObjectByName(
      "forkmesh-reflective-fm-cube-support"
    );
    const contact = scene.getObjectByName(
      "forkmesh-reflective-fm-cube-contact-point"
    );
    const solidBodies = [];
    let fFaces = 0;
    let mFaces = 0;
    let panels = 0;
    const throughCutouts = [];
    chromeCube.traverse((object) => {
      if (object.name.startsWith("forkmesh-reflective-f-face-")) fFaces += 1;
      if (object.name.startsWith("forkmesh-reflective-m-face-")) mFaces += 1;
      if (object.name.startsWith("forkmesh-reflective-fm-panel-")) {
        panels += 1;
        throughCutouts.push(Number(object.userData.logoThroughCutouts) || 0);
      }
      const size = object.geometry?.parameters;
      if (
        object.isMesh &&
        size?.width === 5.6 &&
        size?.height === 5.6 &&
        size?.depth === 5.6
      ) {
        solidBodies.push(object.name || "solid");
      }
    });
    const contactWorld = contact.getWorldPosition(contact.position.clone());
    const supportTop = support.localToWorld(
      support.position.clone().set(
        0,
        support.geometry.parameters.height / 2,
        0,
      )
    );
    const sat = shell.world.sitOnOfficeChair("chair-1");
    const seatedWorld = shell.world.player.getWorldPosition(
      shell.world.player.position.clone()
    );
    const seatedLocal = interior.worldToLocal(seatedWorld).toArray();
    const stood = shell.world.sitOnOfficeChair("chair-1");
    return {
      space: shell.world.getPosition().space,
      tableBottom,
      highestLegTop,
      chairCount: chairs.length,
      chairs,
      boards: {
        task: taskBoard.position.toArray(),
        guide: guideBoard.position.toArray(),
        title: title.position.toArray(),
      },
      placards,
      reception: {
        desk: receptionDesk.position.toArray(),
        hasMaya: Boolean(maya),
        noah: noah?.position.toArray(),
        noahCount,
      },
      logo: {
        verticalAxisOnly: {
          x: chromeCube.rotation.x,
          z: chromeCube.rotation.z,
        },
        fixedTiltQuaternion: chromeMark.quaternion.toArray(),
        contactSupportDistance: contactWorld.distanceTo(supportTop),
        fFaces,
        mFaces,
        panels,
        throughCutouts: throughCutouts.sort((left, right) => left - right),
        supportCount: support ? 1 : 0,
        solidBodies,
      },
      seating: {
        sat,
        stood,
        seatedLocal,
        standingY: shell.world.player.position.y,
      },
    };
  });
  await page.locator("forkmesh-world").evaluate((shell) => {
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const cameraPosition = interior.localToWorld(
      shell.world.camera.position.clone().set(0, 27, 37)
    );
    const target = interior.localToWorld(
      shell.world.camera.position.clone().set(0, 21, -12)
    );
    shell.world.setPaused(true);
    shell.world.camera.position.copy(cameraPosition);
    shell.world.camera.lookAt(target);
    shell.world.renderer.render(shell.world.scene, shell.world.camera);
  });
  await page.screenshot({
    path: "/tmp/forkmesh-office-marketing.png",
    animations: "disabled",
  });

  expect(state.space).toBe("office-marketing");
  expect(state.tableBottom).toBeGreaterThanOrEqual(state.highestLegTop);
  expect(state.chairCount).toBe(8);
  expect(state.chairs.every((chair) => chair.backZ < 0)).toBe(true);
  expect(state.chairs.every((chair) => chair.inwardDot > 0.99)).toBe(true);
  expect(state.boards.task).toEqual([-24, 22.4, -44.45]);
  expect(state.boards.guide).toEqual([24, 22.65, -44.44]);
  expect(state.boards.title[2]).toBeLessThan(-44);
  expect(state.placards.length).toBeGreaterThanOrEqual(11);
  expect(state.placards.every((placard) => placard.mesh && !placard.sprite))
    .toBe(true);
  expect(state.reception.desk).toEqual([0, 1.05, -36.5]);
  expect(state.reception.hasMaya).toBe(false);
  expect(state.reception.noah).toEqual([0, 0.38, -40]);
  expect(state.reception.noahCount).toBe(1);
  expect(state.logo.verticalAxisOnly.x).toBeCloseTo(0, 7);
  expect(state.logo.verticalAxisOnly.z).toBeCloseTo(0, 7);
  expect(state.logo.fixedTiltQuaternion.some(
    (value) => Math.abs(value) > 0.1
  )).toBe(true);
  expect(state.logo.contactSupportDistance).toBeLessThan(0.03);
  expect(state.logo).toMatchObject({
    fFaces: 2,
    mFaces: 2,
    panels: 2,
    throughCutouts: [7, 7],
    supportCount: 1,
    solidBodies: [],
  });
  expect(state.seating).toMatchObject({
    sat: true,
    stood: true,
    standingY: 16.38,
  });
  expect(state.seating.seatedLocal[0]).toBeCloseTo(-5.15, 2);
  expect(state.seating.seatedLocal[2]).toBeCloseTo(0, 2);
  // Avatar origins sit below the seat because their hips are modeled above
  // the origin; the rendered hips remain on the chair and the folded feet
  // reach the floor.
  expect(state.seating.seatedLocal[1]).toBeGreaterThan(15.5);
  expect(state.seating.seatedLocal[1]).toBeLessThan(16.38);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setPaused(false);
    shell.world.enterOfficeLobby({ floorId: "lobby" });
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const cameraPosition = interior.localToWorld(
      shell.world.camera.position.clone().set(0, 11, 36)
    );
    const target = interior.localToWorld(
      shell.world.camera.position.clone().set(0, 5, -18)
    );
    shell.world.setPaused(true);
    shell.world.camera.position.copy(cameraPosition);
    shell.world.camera.lookAt(target);
    shell.world.renderer.render(shell.world.scene, shell.world.camera);
  });
  await page.screenshot({
    path: "/tmp/forkmesh-office-lobby.png",
    animations: "disabled",
  });
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.setPaused(false);
  });
  await page.waitForTimeout(1200);
  await page.locator("forkmesh-world").evaluate((shell) => {
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const cameraPosition = interior.localToWorld(
      shell.world.camera.position.clone().set(-15, 15, 1)
    );
    const target = interior.localToWorld(
      shell.world.camera.position.clone().set(-18, 7.5, -2)
    );
    shell.world.scene.getObjectByName(
      "forkmesh-reflective-fm-cube"
    ).rotation.y = 0;
    shell.world.setPaused(true);
    shell.world.camera.fov = 65;
    shell.world.camera.updateProjectionMatrix();
    shell.world.camera.position.copy(cameraPosition);
    shell.world.camera.lookAt(target);
    shell.world.renderer.render(shell.world.scene, shell.world.camera);
  });
  await page.locator("canvas.world-canvas").screenshot({
    path: "/tmp/forkmesh-office-logo.png",
    animations: "disabled",
  });
});

test("Noah greets desk approaches locally without per-frame bubble spam", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-noah-reception");
  await waitForWorld(page);
  const placeAtDeskDistance = async (localZ) =>
    page.locator("forkmesh-world").evaluate((shell, z) => {
      const interior = shell.world.scene.getObjectByName(
        "forkmesh-office-interior"
      );
      const point = interior.localToWorld(
        shell.world.player.position.clone().set(0, 0.38, z)
      );
      shell.world.player.parent.worldToLocal(point);
      shell.world.player.position.copy(point);
      shell.world.setPaused(false);
    }, localZ);

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.enterOfficeLobby();
  });
  await placeAtDeskDistance(-32);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => {
      let count = 0;
      shell.world.scene.traverse((object) => {
        if (object.userData?.officeReceptionGreeting === true) count += 1;
      });
      return count;
    })
  ).toBe(1);

  const first = await page.locator("forkmesh-world").evaluate((shell) => {
    const scene = shell.world.scene;
    const noah = scene.getObjectByName("avatar:office-greeter-noah");
    let bubble = null;
    let noahCount = 0;
    scene.traverse((object) => {
      if (object.name === "avatar:office-greeter-noah") noahCount += 1;
      if (object.userData?.officeReceptionGreeting === true) bubble = object;
    });
    return {
      noahCount,
      hasMaya: Boolean(scene.getObjectByName("avatar:office-greeter-maya")),
      uuid: bubble?.uuid || "",
      message: bubble?.userData?.message || "",
      bubble: bubble?.position.toArray() || [],
      noah: noah?.getWorldPosition(noah.position.clone()).toArray() || [],
    };
  });
  expect(first.noahCount).toBe(1);
  expect(first.hasMaya).toBe(false);
  expect(first.message).toContain("lobby is open to everyone");
  expect(first.message).toContain("Log in");
  expect(first.message).toContain("restricted team floors");
  expect(Math.abs(first.bubble[0] - first.noah[0])).toBeLessThan(0.05);
  expect(Math.abs(first.bubble[2] - first.noah[2])).toBeLessThan(0.05);
  expect(first.bubble[1]).toBeGreaterThan(first.noah[1]);

  // Cross the hysteresis boundary and return during the cooldown. The existing
  // local bubble remains; no new sprite/event is created on animation frames.
  await placeAtDeskDistance(-27);
  await page.waitForTimeout(100);
  await placeAtDeskDistance(-32);
  await page.waitForTimeout(180);
  const second = await page.locator("forkmesh-world").evaluate((shell) => {
    const bubbles = [];
    shell.world.scene.traverse((object) => {
      if (object.userData?.officeReceptionGreeting === true) {
        bubbles.push(object.uuid);
      }
    });
    return bubbles;
  });
  expect(second).toEqual([first.uuid]);
});

test("FM sculpture uses mirrored through-cut panels at a deterministic yaw", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await prepareWorldPage(page, "office-fm-sculpture");
  await waitForWorld(page);
  const entryCapture = await page.locator("forkmesh-world").evaluate((shell) => {
    const scene = shell.world.scene;
    const renderer = shell.world.renderer;
    const reflection = shell.world.scene.getObjectByName(
      "forkmesh-office-logo-reflection-camera"
    );
    const chromeCube = scene.getObjectByName(
      "forkmesh-reflective-fm-cube"
    );
    const localPlayer = shell.world.player;
    const originalRender = renderer.render;
    const renderStates = [];
    renderer.render = function renderWithLogoReflectionAudit(
      renderedScene,
      renderedCamera,
    ) {
      if (renderedCamera?.parent === reflection) {
        let visibleAvatarMeshes = 0;
        localPlayer.traverse((object) => {
          if (!object.isMesh || !object.visible) return;
          let ancestor = object.parent;
          while (ancestor && ancestor !== localPlayer) {
            if (!ancestor.visible) return;
            ancestor = ancestor.parent;
          }
          visibleAvatarMeshes += 1;
        });
        renderStates.push({
          playerVisible: localPlayer.visible,
          visibleAvatarMeshes,
          sculptureVisible: chromeCube.visible,
        });
      }
      return originalRender.call(this, renderedScene, renderedCamera);
    };
    reflection.userData.testLogoReflectionRenderStates = renderStates;
    reflection.userData.testLogoReflectionRestore = () => {
      renderer.render = originalRender;
    };
    shell.world.enterOfficeLobby();
    return {
      count: Number(reflection?.userData?.logoCaptureCount) || 0,
      settleMs: Number(reflection?.userData?.logoCaptureSettleMs) || 900,
    };
  });
  expect(entryCapture.count).toBe(0);
  // Motion keeps the six-face cube render deferred. Once movement stops, one
  // settled capture includes the live avatar; continued idling does not loop.
  await page.keyboard.down("w");
  await page.waitForTimeout(250);
  const movingCapture = await page.locator("forkmesh-world").evaluate((shell) =>
    Number(shell.world.scene.getObjectByName(
      "forkmesh-office-logo-reflection-camera"
    )?.userData?.logoCaptureCount) || 0
  );
  await page.keyboard.up("w");
  expect(movingCapture).toBe(0);
  await page.locator("forkmesh-world").evaluate((shell) => {
    const interior = shell.world.scene.getObjectByName(
      "forkmesh-office-interior"
    );
    const reflectedAvatarPoint = interior.localToWorld(
      shell.world.player.position.clone().set(-9, 0.38, -2)
    );
    shell.world.player.parent.worldToLocal(reflectedAvatarPoint);
    shell.world.player.position.copy(reflectedAvatarPoint);
  });
  await page.waitForTimeout(entryCapture.settleMs + 350);
  const settledCapture = await page.locator("forkmesh-world").evaluate(
    (shell) => {
      const reflection = shell.world.scene.getObjectByName(
        "forkmesh-office-logo-reflection-camera"
      );
      const states = [
        ...(reflection?.userData?.testLogoReflectionRenderStates || []),
      ];
      reflection?.userData?.testLogoReflectionRestore?.();
      delete reflection?.userData?.testLogoReflectionRenderStates;
      delete reflection?.userData?.testLogoReflectionRestore;
      return {
        count: Number(reflection?.userData?.logoCaptureCount) || 0,
        states,
        playerRestored:
          shell.world.player.visible ===
          (shell.world.getCameraState().mode !== "first-person"),
      };
    }
  );
  expect(settledCapture.count).toBe(1);
  expect(settledCapture.states).toHaveLength(6);
  expect(settledCapture.states.every((state) =>
    state.playerVisible &&
    state.visibleAvatarMeshes >= 10 &&
    !state.sculptureVisible
  )).toBe(true);
  expect(settledCapture.playerRestored).toBe(true);
  await page.waitForTimeout(entryCapture.settleMs + 200);
  const logo = await page.locator("forkmesh-world").evaluate((shell) => {
    const scene = shell.world.scene;
    const cube = scene.getObjectByName("forkmesh-reflective-fm-cube");
    const mark = scene.getObjectByName(
      "forkmesh-reflective-fm-cube-fixed-tilt"
    );
    const panels = [];
    const letterPieces = [];
    const oldOverlays = [];
    mark.traverse((object) => {
      if (object.name.startsWith("forkmesh-reflective-fm-panel-")) {
        panels.push({
          name: object.name,
          throughCutouts: Number(object.userData.logoThroughCutouts) || 0,
          metalness: object.material.metalness,
          roughness: object.material.roughness,
          hasEnvironment: Boolean(object.material.envMap),
        });
      }
      if (object.isMesh && Array.isArray(object.material)) {
        letterPieces.push({
          materialCount: object.material.length,
          mirroredOuterFace:
            object.material[4]?.metalness >= 0.9 &&
            Boolean(object.material[4]?.envMap),
          matteSides:
            object.material.slice(0, 4).every((material) =>
              material === object.material[5] &&
              material.roughness >= 0.7
            ),
        });
      }
      const geometry = object.geometry?.parameters;
      if (
        geometry?.radiusTop === 0.48 ||
        (
          geometry?.width === 0.3 &&
          geometry?.height === 0.08
        )
      ) {
        oldOverlays.push(object.name || object.geometry.type);
      }
    });
    const support = scene.getObjectByName(
      "forkmesh-reflective-fm-cube-support"
    );
    const contact = scene.getObjectByName(
      "forkmesh-reflective-fm-cube-contact-point"
    );
    const reflection = scene.getObjectByName(
      "forkmesh-office-logo-reflection-camera"
    );
    const contactWorld = contact.getWorldPosition(contact.position.clone());
    const supportTop = support.localToWorld(
      support.position.clone().set(
        0,
        support.geometry.parameters.height / 2,
        0,
      )
    );
    cube.rotation.y = 0;
    const interior = scene.getObjectByName("forkmesh-office-interior");
    shell.world.setPaused(true);
    shell.world.camera.fov = 48;
    shell.world.camera.updateProjectionMatrix();
    shell.world.camera.position.copy(interior.localToWorld(
      shell.world.camera.position.clone().set(-9, 12, 6)
    ));
    shell.world.camera.lookAt(interior.localToWorld(
      shell.world.camera.position.clone().set(-18, 6.3, -2)
    ));
    shell.world.renderer.render(scene, shell.world.camera);
    return {
      panels,
      letterPieces,
      oldOverlays,
      outerTilt: [cube.rotation.x, cube.rotation.z],
      fixedTilt: mark.quaternion.toArray(),
      supportCount: support ? 1 : 0,
      contactSupportDistance: contactWorld.distanceTo(supportTop),
      reflection: {
        captureCount:
          Number(reflection?.userData?.logoCaptureCount) || 0,
        capturePolicy: reflection?.userData?.logoCapturePolicy || "",
        capturedPlayer:
          reflection?.userData?.logoCapturedPlayer === true,
      },
    };
  });
  await page.locator("canvas.world-canvas").screenshot({
    path: "/tmp/forkmesh-office-logo.png",
    animations: "disabled",
  });
  expect(logo.panels).toHaveLength(2);
  expect(logo.panels.every((panel) =>
    panel.throughCutouts === 7 &&
    panel.metalness >= 0.78 &&
    panel.roughness <= 0.08 &&
    panel.hasEnvironment
  )).toBe(true);
  expect(logo.letterPieces).toHaveLength(14);
  expect(logo.letterPieces.every((piece) =>
    piece.materialCount === 6 &&
    piece.mirroredOuterFace &&
    piece.matteSides
  )).toBe(true);
  expect(logo.oldOverlays).toEqual([]);
  expect(logo.outerTilt[0]).toBeCloseTo(0, 7);
  expect(logo.outerTilt[1]).toBeCloseTo(0, 7);
  expect(logo.fixedTilt.some((value) => Math.abs(value) > 0.1)).toBe(true);
  expect(logo.supportCount).toBe(1);
  expect(logo.contactSupportDistance).toBeLessThan(0.03);
  expect(logo.reflection).toEqual({
    captureCount: 1,
    capturePolicy: "dirty-idle-once",
    capturedPlayer: true,
  });
});

test("Office glass has one stable shell and one elevator-car layer", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-stable-glass");
  await waitForWorld(page);
  const glass = await page.locator("forkmesh-world").evaluate((shell) => {
    const scene = shell.world.scene;
    const transparentMeshes = (root) => {
      const meshes = [];
      root?.traverse((object) => {
        if (object.isMesh && object.material?.transparent) {
          meshes.push(object);
        }
      });
      return meshes;
    };
    const exterior = [];
    scene.traverse((object) => {
      if (
        object.isMesh &&
        object.userData?.landmark === "office" &&
        object.material?.transparent
      ) {
        exterior.push(object);
      }
    });
    const teamLayers = [
      "marketing",
      "engineering",
      "product-design",
      "security",
      "infrastructure",
      "community",
      "partnerships",
      "operations",
    ].flatMap((floorId) => transparentMeshes(
      scene.getObjectByName(`forkmesh-office-floor-${floorId}`)
    ));
    const rooftop = transparentMeshes(
      scene.getObjectByName("forkmesh-office-floor-rooftop")
    );
    const shaft = transparentMeshes(
      scene.getObjectByName("forkmesh-office-glass-elevator-shaft")
    );
    const car = transparentMeshes(
      scene.getObjectByName("forkmesh-office-glass-elevator-car")
    );
    const doors = [
      "forkmesh-office-sliding-door-left",
      "forkmesh-office-sliding-door-right",
    ].map((name) => scene.getObjectByName(name));
    return {
      exterior: {
        count: exterior.length,
        stable: exterior.every((mesh) =>
          mesh.material.depthWrite === false &&
          mesh.castShadow === false &&
          mesh.receiveShadow === false
        ),
      },
      teamLayerCount: teamLayers.length,
      rooftop: {
        count: rooftop.length,
        stable: rooftop.every((mesh) => mesh.material.depthWrite === false),
      },
      shaftLayerCount: shaft.length,
      car: {
        count: car.length,
        stable: car.every((mesh) =>
          mesh.material.depthWrite === false && mesh.castShadow === false
        ),
      },
      doors: {
        count: doors.filter(Boolean).length,
        stable: doors.every((mesh) =>
          mesh?.material?.transparent === true &&
          mesh.material.opacity === 0.3 &&
          mesh.material.metalness === 0 &&
          mesh.material.depthWrite === false &&
          mesh.castShadow === false &&
          mesh.receiveShadow === false
        ),
      },
    };
  });
  expect(glass.exterior.count).toBeGreaterThanOrEqual(20);
  expect(glass.exterior.stable).toBe(true);
  expect(glass.teamLayerCount).toBe(0);
  expect(glass.rooftop).toEqual({ count: 5, stable: true });
  expect(glass.shaftLayerCount).toBe(0);
  expect(glass.car).toEqual({ count: 5, stable: true });
  expect(glass.doors).toEqual({ count: 2, stable: true });
});

test("Office elevator exposes ten floors while enforcing team access", async ({
  page,
}) => {
  test.setTimeout(120_000);
  await prepareWorldPage(page, "office-elevator-access", {
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-office-token",
    },
    officeFloorAccess: {
      allowedFloorIds: ["engineering"],
      teams: ["engineering"],
    },
  });
  await waitForWorld(page);
  await walkIntoOffice(page);
  await expect(page.locator("[data-world-office-lobby]")).toBeHidden();
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => {
      let allowed = false;
      shell.world.scene.traverse((object) => {
        if (
          object.userData?.officeFloorId === "engineering" &&
          object.userData?.officeFloorAllowed === true
        ) {
          allowed = true;
        }
      });
      return allowed;
    })
  ).toBe(true);

  const access = await page.locator("forkmesh-world").evaluate((shell) => {
    const destinations = new Map();
    const scene = shell.world.scene;
    const car = scene.getObjectByName("forkmesh-office-glass-elevator-car");
    const shaft = scene.getObjectByName(
      "forkmesh-office-glass-elevator-shaft"
    );
    const panel = scene.getObjectByName(
      "forkmesh-office-elevator-cabin-panel"
    );
    const buttons = [];
    const isDescendantOf = (object, ancestor) => {
      for (let current = object?.parent; current; current = current.parent) {
        if (current === ancestor) return true;
      }
      return false;
    };
    shell.world.scene.traverse((object) => {
      if (object.userData?.interactive !== "office-elevator-floor") return;
      buttons.push(object);
      destinations.set(object.userData.officeFloorId, {
        id: object.userData.officeFloorId,
        level: object.userData.officeFloorLevel,
        number: object.userData.officeFloorNumber,
        teamLabel: object.userData.officeFloorTeamLabel,
        allowed: object.userData.officeFloorAllowed === true,
        y: Number(object.position.y.toFixed(2)),
      });
    });
    return {
      destinations: [...destinations.values()].sort((left, right) =>
        left.level - right.level
      ),
      buttonCount: buttons.length,
      buttonsMoveWithCar:
        Boolean(car) && buttons.every((button) => isDescendantOf(button, car)),
      panelParent: panel?.parent?.name || "",
      carPosition: car
        ? { x: car.position.x, y: car.position.y, z: car.position.z }
        : null,
      shaftPosition: shaft
        ? { x: shaft.position.x, y: shaft.position.y, z: shaft.position.z }
        : null,
    };
  });
  expect(access.destinations).toHaveLength(10);
  expect(access.destinations.map((floor) => floor.id)).toEqual([
    "lobby",
    "marketing",
    "engineering",
    "product-design",
    "security",
    "infrastructure",
    "community",
    "partnerships",
    "operations",
    "rooftop",
  ]);
  expect(access.buttonCount).toBe(10);
  expect(access.destinations.map((floor) => floor.number)).toEqual([
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
  ]);
  expect(access.destinations[0].teamLabel).toBe("LOBBY");
  expect(access.destinations[2].teamLabel).toBe("ENGINEERING");
  expect(access.destinations[9].teamLabel).toBe("ROOF");
  expect(access.destinations[0].y).toBe(access.destinations[1].y);
  expect(access.destinations[1].y).toBeLessThan(access.destinations[2].y);
  expect(access.destinations[2].y).toBeLessThan(access.destinations[9].y);
  expect(access.buttonsMoveWithCar).toBe(true);
  expect(access.panelParent).toBe("forkmesh-office-glass-elevator-car");
  expect(access.carPosition).toMatchObject({ x: 70, y: 0 });
  expect(access.shaftPosition).toMatchObject({ x: 70, y: 0 });
  expect(access.carPosition.z).toBeGreaterThanOrEqual(44.5);
  expect(access.shaftPosition.z).toBeCloseTo(access.carPosition.z, 5);
  expect(
    Object.fromEntries(
      access.destinations.map((floor) => [floor.id, floor.allowed])
    ),
  ).toMatchObject({
    lobby: true,
    marketing: true,
    engineering: true,
    security: false,
    rooftop: true,
  });

  // Calling the controller from elsewhere on the floor must not teleport the
  // avatar into the elevator. Only a visitor physically inside the cabin may
  // start a ride.
  const outsideCabin = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.travelToOfficeFloor("engineering")
  );
  expect(outsideCabin).toBe(false);

  const cabinPosition = await page
    .locator("forkmesh-world")
    .evaluate((shell) => {
      const scene = shell.world.scene;
      const car = scene.getObjectByName("forkmesh-office-glass-elevator-car");
      const avatar = shell.world.player;
      car.updateWorldMatrix(true, false);
      avatar.position.x = car.matrixWorld.elements[12] - 1.25;
      avatar.position.z = car.matrixWorld.elements[14] - 1.25;
      return { x: avatar.position.x, z: avatar.position.z };
    });
  const locked = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.travelToOfficeFloor("security")
  );
  expect(locked).toBe(false);
  const travelled = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.travelToOfficeFloor("engineering")
  );
  expect(travelled).toBe(true);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => ({
      space: shell.world.getPosition().space,
      avatar: (() => {
        const object = shell.world.player;
        return object
          ? { x: object.position.x, y: object.position.y, z: object.position.z }
          : null;
      })(),
      carHeight: shell.world.scene.getObjectByName(
        "forkmesh-office-glass-elevator-car"
      )?.position.y,
    }))
  ).toMatchObject({
    space: "office-engineering",
    avatar: {
      x: cabinPosition.x,
      y: 32.38,
      z: cabinPosition.z,
    },
    carHeight: 32,
  });
  // The elevator changes the active floor while the camera can remain in the
  // same near-LOD class. The story itself must make the destination visible;
  // waiting for a later zoom update once caused a successful arrival to render
  // an empty Office level.
  const arrivedFloor = await page.locator("forkmesh-world").evaluate((shell) => {
    const engineering = shell.world.scene.getObjectByName(
      "forkmesh-office-floor-engineering",
    );
    const marketing = shell.world.scene.getObjectByName(
      "forkmesh-office-floor-marketing",
    );
    const slab = engineering?.getObjectByName(
      "forkmesh-office-floor-slab-engineering-1",
    );
    return {
      engineeringVisible: engineering?.visible === true,
      engineeringSlabVisible: slab?.visible === true,
      priorFloorHidden: marketing?.visible === false,
    };
  });
  expect(arrivedFloor).toEqual({
    engineeringVisible: true,
    engineeringSlabVisible: true,
    priorFloorHidden: true,
  });
});

test("Local controls carry Work and Security tabs instead of a session card", async ({
  page,
}) => {
  const taskId = "b".repeat(32);
  const officeTaskFixture = {
    canManage: false,
    requests: [],
    tasks: [
      {
        id: taskId,
        title: "Write the launch digest",
        assignee: "alice",
        status: "idle",
        elapsedMs: 0,
        startedAt: 0,
        nextCheckinAt: 0,
        createdAt: FIXED_NOW,
        updatedAt: FIXED_NOW,
        lastCheckin: null,
      },
    ],
  };
  const accountSessionFixture = {
    requests: [],
    sessions: [
      {
        id: "c".repeat(32),
        deviceLabel: "Web browser",
        ipAddress: "203.0.113.9",
        createdAt: FIXED_NOW - 60_000,
        lastSeenAt: FIXED_NOW - 60_000,
        expiresAt: FIXED_NOW + 60_000,
        current: true,
      },
      {
        id: "d".repeat(32),
        deviceLabel: "Desktop node",
        ipAddress: "198.51.100.7",
        createdAt: FIXED_NOW - 7_200_000,
        lastSeenAt: FIXED_NOW - 3_600_000,
        expiresAt: FIXED_NOW + 60_000,
        current: false,
      },
    ],
  };
  await prepareWorldPage(page, "local-controls-tabs", {
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-token",
    },
    officeTaskFixture,
    accountSessionFixture,
  });
  await waitForWorld(page);
  // The owner-only avatar plate carries assigned work, never a session card.
  const board = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.setSelfWorkBoard({}),
  );
  expect(board.state).toBe("locked");

  await page.locator("[data-world-settings-open]").first().click();
  await expect(page.locator("[data-world-settings]")).toHaveAttribute(
    "data-open",
    "true",
  );

  // The View tab is the default, so the environment controls stay reachable.
  await expect(page.locator("[data-world-light-level]")).toBeVisible();
  await expect(page.locator("[data-world-session-list]")).toBeHidden();

  await page.locator('[data-world-settings-tab="security"]').click();
  const sessions = page.locator("[data-world-session-list] > li");
  await expect(sessions).toHaveCount(2);
  await expect(sessions.first()).toContainText("Web browser · this device");
  await expect(sessions.first()).toContainText("203.0.113.9");
  await expect(sessions.nth(1)).toContainText("198.51.100.7");
  await expect(page.locator("[data-world-light-level]")).toBeHidden();

  // Revoking another device leaves this one signed in and does not reload.
  await sessions
    .nth(1)
    .locator("[data-world-session-revoke]")
    .click();
  await expect(sessions).toHaveCount(1);
  expect(
    accountSessionFixture.requests.some(
      (request) =>
        request.method === "DELETE" && request.target === "d".repeat(32),
    ),
  ).toBe(true);

  await page.locator('[data-world-settings-tab="work"]').click();
  await expect(page.locator("[data-world-work-total]")).toHaveText("1");
  await expect(page.locator("[data-world-work-active]")).toHaveText("0");
  const workTask = page.locator("[data-world-work-list] > li").first();
  await expect(workTask).toContainText("Write the launch digest");
  await workTask.locator('[data-world-office-task-action="start"]').click();
  await expect
    .poll(() =>
      officeTaskFixture.requests.filter(
        (request) =>
          request.path ===
          `/api/world/office/marketing-tasks/${taskId}/start`,
      ).length,
    )
    .toBeGreaterThan(0);
  // The row flips to Stop once the server-timed start lands.
  await expect(
    page.locator('[data-world-work-list] [data-world-office-task-action="stop"]'),
  ).toBeVisible();
  await expect(page.locator("[data-world-work-active]")).toHaveText("1");

});


test("Office marketing tasks can be assigned, timed, and checked in privately", async ({
  page,
}) => {
  const taskId = "a".repeat(32);
  const officeTaskFixture = {
    canManage: true,
    members: ["alice", "bob"],
    requests: [],
    tasks: [
      {
        id: taskId,
        title: "Prepare launch digest",
        assignee: "alice",
        status: "idle",
        elapsedMs: 0,
        startedAt: 0,
        nextCheckinAt: 0,
        createdAt: FIXED_NOW,
        updatedAt: FIXED_NOW,
        lastCheckin: null,
      },
    ],
  };
  const worldFrames = [];
  await prepareWorldPage(page, "office-marketing-tasks", {
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-token",
    },
    officeTaskFixture,
    worldSocketHandler: (socket, id) => {
      socket.onMessage((raw) => worldFrames.push(String(raw)));
      socket.send(JSON.stringify({ type: "welcome", id, peers: [] }));
    },
  });
  await waitForWorld(page);
  await page.evaluate(() => {
    const shell = document.querySelector("forkmesh-world");
    shell.officeTasks.setActive(true);
    shell.officeTasks.open();
  });

  const panel = page.locator("[data-world-office-task-panel]");
  await expect(panel).toBeVisible();
  const originalTask = panel
    .locator("[data-world-office-task-list] > li")
    .filter({ hasText: "Prepare launch digest" });
  await expect(originalTask).toContainText("@alice");
  await originalTask.getByRole("button", { name: "Start" }).click();
  await expect(originalTask.getByRole("button", { name: "Stop" })).toBeVisible();
  await expect
    .poll(async () =>
      originalTask.locator("[data-world-office-task-elapsed]").textContent(),
    )
    .not.toBe("0:00");

  await page.evaluate((id) => {
    document.querySelector("forkmesh-world").officeTasks.showCheckin(id);
  }, taskId);
  const checkin = page.locator("[data-world-office-task-checkin]");
  await expect(checkin).toBeVisible();
  await checkin.getByRole("button", { name: "Blocked" }).click();
  await expect(originalTask).toContainText("last check-in: Blocked");

  await page.evaluate(async () => {
    const tasks = document.querySelector("forkmesh-world").officeTasks;
    tasks.stopActiveForDeparture();
    await new Promise((resolve) => setTimeout(resolve, 50));
    await tasks.refresh({ quiet: true });
  });
  await expect(originalTask.getByRole("button", { name: "Start" })).toBeVisible();
  expect(
    officeTaskFixture.requests.some(
      (request) =>
        request.method === "POST" &&
        request.path.endsWith("/stop-active"),
    ),
  ).toBe(true);

  await panel.locator("[data-world-office-task-name]").fill(
    "Schedule community demo",
  );
  await panel.locator("[data-world-office-task-assignee]").selectOption("bob");
  await panel.getByRole("button", { name: "Add task" }).click();
  await expect(panel).toContainText("Schedule community demo");
  await expect(panel).toContainText("@bob");

  expect(
    officeTaskFixture.requests.some(
      (request) =>
        request.method === "POST" &&
        request.path.endsWith(`/${taskId}/checkin`) &&
        request.body.state === "blocked",
    ),
  ).toBe(true);
  expect(JSON.stringify(worldFrames)).not.toContain("Prepare launch digest");
  expect(JSON.stringify(worldFrames)).not.toContain("Schedule community demo");
});

test("ForkMesh Office defers the channel directory until fallback is opened", async ({
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
  await walkIntoOffice(page);
  const nativeRooms = page.locator("[data-world-office-room-board]");
  await expect(nativeRooms).toContainText("#general");
  await expect(nativeRooms).not.toContainText("#announcements");
  await expect(nativeRooms).not.toContainText("#leadership");
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.officeController.openFallback();
  });

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
  await walkIntoOffice(page);
  await expect(page.locator("[data-world-office-room-board]")).toContainText(
    "#general",
  );
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.officeController.openFallback();
  });

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
    '.world-map [data-world-landmark="fountain"]',
  );
  await trigger.click();
  const detail = page.locator("[data-world-detail]");
  await expect(detail).toBeVisible();
  await expect(detail).toHaveAttribute("role", "dialog");
  await expect(detail).toHaveAttribute("aria-modal", "false");
  await expect(
    detail.getByRole("button", { name: "Close SOL reward fountain" }),
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
  ).toBe("fountain");

  await page.locator("[data-world-detail-close]").click();
  await expect(trigger).toBeFocused();

  await page.setViewportSize({ width: 600, height: 800 });
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("fountain"),
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
  await walkIntoOffice(page);
  await expect(page.locator("[data-world-office-lobby]")).toBeHidden();
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
    // A normal Town Square position survives while private activity copy does
    // not become part of the local position record.
    const position = {
      x: 16.3,
      y: 0.38,
      z: 26.5,
      heading: 1.2,
      space: "town-square",
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
  expect(storedBeforeRefresh.record.space).toBe("town-square");
  const positionBeforeRefresh = await page
    .locator("forkmesh-world")
    .evaluate((shell) => shell.world.getPosition());
  expect(positionBeforeRefresh.space).toBe("town-square");
  expect(JSON.stringify(storedBeforeRefresh.record)).not.toContain("private");
  expect(JSON.stringify(storedBeforeRefresh.record)).not.toContain("token");

  await page.reload();
  await waitForWorldReady(page);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => {
      const position = shell.world.getPosition();
      return (
        shell.spawnSelected === true &&
        position.space === "town-square" &&
        Math.abs(position.x - 16.3) < 0.001 &&
        Math.abs(position.z - 26.5) < 0.001
      );
    }),
  ).toBe(true);
  const restored = await page.locator("forkmesh-world").evaluate((shell) => ({
    position: shell.world.getPosition(),
    currentSpace: shell.currentSpace,
    restoredPosition: shell.restoredPosition,
    storedRecord: JSON.parse(
      localStorage.getItem(
        Object.keys(localStorage).find((key) =>
          key.startsWith("forkmesh.world.position.v1."),
        ) || "",
      ) || "null",
    ),
    storedRecords: Object.keys(localStorage).filter((key) =>
      key.startsWith("forkmesh.world.position.v1."),
    ).length,
  }));
  expect(restored).toMatchObject({
    currentSpace: "town-square",
    position: { space: "town-square" },
    restoredPosition: { space: "town-square" },
    storedRecord: { space: "town-square" },
  });
  expect(restored.position.x).toBeCloseTo(16.3, 3);
  expect(restored.position.y).toBeCloseTo(0.38, 3);
  expect(restored.position.z).toBeCloseTo(26.5, 3);
  expect(restored.position.heading).toBeCloseTo(1.2, 3);
  expect(restored.storedRecords).toBe(1);
});

test("refresh from the Office restores the last Town Square location", async ({
  page,
}) => {
  await prepareWorldPage(page, "office-refresh-position");
  await waitForWorld(page);

  await page.locator("forkmesh-world").evaluate((shell) => {
    const position = {
      x: -18.2,
      y: 0.38,
      z: 14.6,
      heading: -0.7,
      space: "town-square",
      moving: false,
      activity: "exploring the Town Square",
    };
    shell.currentSpace = position.space;
    shell.world.setSpawn(position);
    shell.handleMovement(position);
    shell.world.enterOfficeLobby();
  });
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getPosition().space,
    ),
  ).toBe("office-lobby");

  await page.reload();
  await waitForWorldReady(page);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate((shell) => {
      const position = shell.world.getPosition();
      return (
        shell.spawnSelected === true &&
        position.space === "town-square" &&
        Math.abs(position.x + 18.2) < 0.001 &&
        Math.abs(position.z - 14.6) < 0.001
      );
    }),
  ).toBe(true);
  await expect(page.locator("[data-world-office-lobby]")).toBeHidden();
});

test("mobile refresh keeps a live low-memory renderer and recovers cached pages and WebGL", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
    deviceScaleFactor: 3,
  });
  const page = await context.newPage();
  const pageErrors = [];
  page.on("pageerror", (error) => pageErrors.push(error.message));
  await prepareWorldPage(page, "mobile-refresh");
  await waitForWorld(page);

  const readRenderer = () =>
    page.locator("forkmesh-world").evaluate((shell) => {
      const { renderer, scene, camera } = shell.world;
      const canvas = renderer.domElement;
      const gl = renderer.getContext();
      renderer.render(scene, camera);
      const pixel = new Uint8Array(4);
      gl.readPixels(
        Math.max(0, Math.floor(gl.drawingBufferWidth / 2)),
        Math.max(0, Math.floor(gl.drawingBufferHeight / 2)),
        1,
        1,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        pixel,
      );
      const rect = canvas.getBoundingClientRect();
      const summary = shell.querySelector(
        "[data-world-diagnostics] > summary",
      );
      const summaryRect = summary?.getBoundingClientRect();
      const compactItems = [
        ...shell.querySelectorAll(
          "[data-world-diagnostics-summary] > span",
        ),
      ];
      return {
        canvases: shell.querySelectorAll("[data-world-canvas-wrap] canvas")
          .length,
        cssWidth: Math.round(rect.width),
        cssHeight: Math.round(rect.height),
        bufferWidth: gl.drawingBufferWidth,
        bufferHeight: gl.drawingBufferHeight,
        pixelRatio: renderer.getPixelRatio(),
        antialias: gl.getContextAttributes()?.antialias,
        stencil: gl.getContextAttributes()?.stencil,
        shadows: renderer.shadowMap.enabled,
        contextLost: gl.isContextLost(),
        frame: renderer.info.render.frame,
        pixel: [...pixel],
        coarse: matchMedia("(pointer: coarse)").matches,
        compactDebugVisible:
          Boolean(summaryRect) &&
          compactItems.length === 9 &&
          compactItems.every((item) => {
            const itemRect = item.getBoundingClientRect();
            return (
              itemRect.width > 0 &&
              itemRect.height > 0 &&
              itemRect.left >= summaryRect.left + 58 &&
              itemRect.right <= summaryRect.right + 1 &&
              itemRect.top >= summaryRect.top - 1 &&
              itemRect.bottom <= summaryRect.bottom + 1
            );
          }),
      };
    });

  for (let cycle = 0; cycle < 3; cycle += 1) {
    if (cycle) {
      await page.reload({ waitUntil: "domcontentloaded" });
      await waitForWorldReady(page);
    }
    const renderer = await readRenderer();
    expect(renderer).toMatchObject({
      canvases: 1,
      cssWidth: 390,
      cssHeight: 844,
      bufferWidth: 390,
      bufferHeight: 844,
      pixelRatio: 1,
      antialias: false,
      stencil: false,
      shadows: false,
      contextLost: false,
      coarse: true,
      compactDebugVisible: true,
    });
    expect(renderer.frame).toBeGreaterThan(0);
    expect(renderer.pixel[0] + renderer.pixel[1] + renderer.pixel[2])
      .toBeGreaterThan(0);
  }

  const frameBeforeCache = await page.locator("forkmesh-world").evaluate(
    (shell) => {
      window.__forkmeshMobileRenderer = shell.world.renderer;
      const frame = shell.world.renderer.info.render.frame;
      window.dispatchEvent(
        new PageTransitionEvent("pagehide", { persisted: true }),
      );
      return frame;
    },
  );
  const cachedState = await page.locator("forkmesh-world").evaluate(
    (shell) => ({
      destroyed: shell.destroyed,
      sameRenderer: shell.world?.renderer === window.__forkmeshMobileRenderer,
      canvases: shell.querySelectorAll("[data-world-canvas-wrap] canvas")
        .length,
    }),
  );
  expect(cachedState).toEqual({
    destroyed: false,
    sameRenderer: true,
    canvases: 1,
  });
  await page.evaluate(() => {
    window.dispatchEvent(
      new PageTransitionEvent("pageshow", { persisted: true }),
    );
  });
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.renderer.info.render.frame,
    ),
  ).toBeGreaterThan(frameBeforeCache);

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.renderer.forceContextLoss();
  });
  await expect(page.locator("[data-world-renderer-recovery]")).toBeVisible();
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.renderer.getContext().isContextLost(),
    ),
  ).toBe(true);
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.renderer.forceContextRestore();
  });
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.renderer.getContext().isContextLost(),
    ),
  ).toBe(false);
  await expect(page.locator("[data-world-renderer-recovery]")).toBeHidden();
  expect(pageErrors).toEqual([]);
  await context.close();
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
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return Boolean(shell?.distanceTimer);
  });
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.stopRadio(false);
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
    shell.activeAudio = {
      kind: "focus-music",
      trackId: "cosmic-waves",
      element: {
        currentTime: 75,
        duration: 240,
      },
    };
    shell.focusMusicState = "playing";
    shell.focusMusicAutoplayPending = false;
    shell.renderDiagnostics();
  });
  await page.waitForTimeout(1150);

  const diagnostics = page.locator("[data-world-diagnostics]");
  await expect(diagnostics.locator("summary")).toContainText("FPS");
  await expect(diagnostics.locator("summary")).toContainText("N online");
  await expect(diagnostics.locator("summary")).toContainText("1p");
  await expect(diagnostics.locator("summary")).toContainText("v0.7.0");
  await expect(diagnostics.locator("summary")).toContainText(
    "Cosmic Waves",
  );
  await expect(
    diagnostics.locator("[data-world-diagnostics-music-position]"),
  ).toHaveText("1:15/4:00");
  const playbackPosition = await diagnostics
    .locator("[data-world-diagnostics-music-progress]")
    .evaluate((progress) => ({
      value: progress.value,
      max: progress.max,
      text: progress.getAttribute("aria-valuetext"),
    }));
  expect(playbackPosition).toEqual({
    value: 75_000,
    max: 240_000,
    text: "Cosmic Waves, 1:15 of 4:00, playing",
  });
  for (const compactMetric of [
    "renderer",
    "frame",
    "input",
    "world",
    "connection",
    "traffic",
    "queues",
    "build",
  ]) {
    await expect(
      diagnostics.locator(
        `[data-world-diagnostics-${compactMetric}-compact]`,
      ),
    ).not.toBeEmpty();
  }
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
  expect(snapshot.music).toEqual({
    state: "playing",
    title: "Cosmic Waves",
    positionMs: 75_000,
    durationMs: 240_000,
  });
  expect(Object.keys(snapshot).sort()).toEqual(
    ["build", "connection", "music", "queues", "renderer", "traffic"].sort(),
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

test("thumbstick walking defers renderer resize until release", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
  });
  const page = await context.newPage();
  await prepareWorldPage(page, "thumbstick-resize");
  await waitForWorld(page);

  const thumbstick = page.locator("[data-world-thumbstick]");
  const box = await thumbstick.boundingBox();
  expect(box).not.toBeNull();
  const centre = {
    x: Math.round(box.x + box.width / 2),
    y: Math.round(box.y + box.height / 2),
    id: 1,
  };
  const client = await page.context().newCDPSession(page);
  await client.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [centre],
  });
  await client.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: [{ ...centre, y: centre.y - box.height * 0.3 }],
  });

  const bufferBeforeResize = await page
    .locator(".world-canvas")
    .evaluate((canvas) => ({ width: canvas.width, height: canvas.height }));
  await page.locator("[data-world-canvas-wrap]").evaluate((wrap) => {
    wrap.style.height = `${Math.max(
      240,
      Math.round(wrap.getBoundingClientRect().height - 120),
    )}px`;
  });
  await page.waitForTimeout(150);
  expect(
    await page
      .locator(".world-canvas")
      .evaluate((canvas) => ({ width: canvas.width, height: canvas.height })),
  ).toEqual(bufferBeforeResize);

  await client.send("Input.dispatchTouchEvent", {
    type: "touchEnd",
    touchPoints: [],
  });
  await expect.poll(() =>
    page.locator(".world-canvas").evaluate((canvas) => canvas.height),
  ).not.toBe(bufferBeforeResize.height);
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
  ).toMatchObject({ zoom: 0.06, minZoom: 0.06 });

  await client.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: points(0.2),
  });
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getCameraState(),
    ),
  ).toMatchObject({ zoom: 28, maxZoom: 28 });

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

test("approved instances stay truthful", async ({
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
});

test("System Capacity fits one height-scaled bar per populated D1 table", async ({
  page,
}) => {
  test.setTimeout(60_000);
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
    systemCapacityD1Storage: {
      bytes: 14_811_136,
      freeDatabaseLimitBytes: 500_000_000,
      paidDatabaseLimitBytes: 10_000_000_000,
      includedAccountStorageBytes: 5_000_000_000,
    },
  });
  await waitForWorld(page);

  const capacity = await page.locator("forkmesh-world").evaluate((shell) => {
    const platform = shell.world.scene.getObjectByName(
      "system-capacity-infrastructure",
    );
    const tableLayer = shell.world.scene.getObjectByName(
      "system-capacity-database-tables",
    );
    const footprint = shell.world.scene.getObjectByName(
      "forkmesh-infrastructure-worker-footprint",
    );
    const footprintFace = shell.world.scene.getObjectByName(
      "forkmesh-infrastructure-worker-footprint-face",
    );
    const components = shell.world.scene.getObjectByName(
      "forkmesh-infrastructure-worker-components",
    );
    const componentsFace = shell.world.scene.getObjectByName(
      "forkmesh-infrastructure-worker-components-face",
    );
    const d1Storage = shell.world.scene.getObjectByName(
      "system-capacity-d1-storage",
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
      footprint: {
        floor: footprint?.userData.officeFloorId || "",
        width: footprintFace?.material?.map?.image?.width || 0,
        height: footprintFace?.material?.map?.image?.height || 0,
      },
      components: {
        floor: components?.userData.officeFloorId || "",
        width: componentsFace?.material?.map?.image?.width || 0,
        height: componentsFace?.material?.map?.image?.height || 0,
      },
      d1StorageVisible: Boolean(d1Storage),
      bars,
    };
  });

  expect(capacity.platformName).toBe("system-capacity-infrastructure");
  expect(capacity.visibleTableCount).toBe(3);
  expect(capacity.footprint).toEqual({
    floor: "infrastructure",
    width: 1800,
    height: 1100,
  });
  expect(capacity.components).toEqual({
    floor: "infrastructure",
    width: 1800,
    height: 1100,
  });
  expect(capacity.d1StorageVisible).toBe(true);
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
    const face = shell.world.scene.getObjectByName(
      "forkmesh-infrastructure-worker-footprint-face",
    );
    const target = face.getWorldPosition(shell.world.camera.position.clone());
    const front = face
      .localToWorld(target.clone().set(0, 0, 1))
      .sub(target)
      .normalize();
    shell.world.camera.position.copy(target).add(front.multiplyScalar(28));
    shell.world.camera.lookAt(target);
    shell.world.camera.updateMatrixWorld();
    shell.world.renderer.render(shell.world.scene, shell.world.camera);
  });
  await expect(page).toHaveScreenshot("world-worker-footprint.png", {
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

test("the flagship pin keeps user ownership separate from source node identity", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await prepareWorldPage(page, "world-source-user-node-identity", {
    repositoryFixture: { sourceUserOwner: true },
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
      aliasCommit: alias?.commit || "",
      servingOwner: alias?.servingOwner || "",
      activeCommit: shell.activeRepository?.commit || "",
    };
  });
  expect(snapshot).toEqual({
    aliasCommit: "a".repeat(40),
    servingOwner: "jett",
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
      hasPerimeter:
        Boolean(scene.getObjectByName("repository-size-map-perimeter")),
    };
  });
  expect(initial.portals).toEqual(["repository-portal:forkmesh/forkmesh"]);
  expect(initial.portals).toHaveLength(initial.repositories.length);
  expect(initial.sizeMount).toBe("repository-portal:forkmesh/forkmesh");
  expect(initial.sizeMountRadius).toBeCloseTo(68, 5);
  expect(initial.legacyVisible).toBe(false);
  expect(initial.relationshipsVisible).toBe(false);
  expect(initial.hasPerimeter).toBe(false);
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

test("the flagship portal opens once mid-sync mirrors converge after entry", async ({
  page,
}) => {
  test.setTimeout(120_000);
  const repositoryFixture = { conflictingHealthyAlias: true };
  await prepareWorldPage(page, "world-late-flagship-pin", {
    repositoryFixture,
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.repositoryMapState === "unavailable";
  });

  // The lagging mirror finishes syncing: both eligible nodes now publish the
  // same commit, so the alias becomes pinned. The portal must open from the
  // bounded retry, without the visitor reloading or picking the repository.
  repositoryFixture.conflictingHealthyAlias = false;

  await page.waitForFunction(
    () => {
      const shell = document.querySelector("forkmesh-world");
      return shell?.repositoryMapState === "ready";
    },
    undefined,
    { timeout: 90_000 },
  );

  const opened = await page.locator("forkmesh-world").evaluate((shell) => {
    const sizeLayer = shell.world.scene.getObjectByName(
      "repository-3d-size-map",
    );
    return {
      active: `${shell.activeRepository?.owner}/${shell.activeRepository?.repo}`,
      manualSelection: shell.repositoryManualSelection,
      mount: sizeLayer?.parent?.name || "",
      faceHidden:
        sizeLayer?.parent?.userData?.repositoryFace?.visible === false,
    };
  });
  expect(opened).toEqual({
    active: "forkmesh/forkmesh",
    manualSelection: "",
    mount: "repository-portal:forkmesh/forkmesh",
    faceHidden: true,
  });
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

test("the repository circle carries the stored star total and its fediverse followers", async ({
  page,
}) => {
  const starFixture = { count: 1284, starred: false, requests: [] };
  const followerFixture = {
    followers: 9,
    requests: [],
    followersList: [
      {
        handle: "@kate@mastodon.social",
        url: "https://mastodon.social/users/kate",
        name: "Kate Mirrors",
        avatarUrl: "https://files.mastodon.social/kate.png",
        about: "Rust, embedded, and self-hosted git.",
        instance: "mastodon.social",
        profileUrl: "https://mastodon.social/@kate",
        followedAt: 1_767_225_600_000,
      },
      {
        handle: "@sam@fosstodon.org",
        url: "https://fosstodon.org/users/sam",
        // No cached actor document yet: name/avatar/bio are still empty.
        instance: "fosstodon.org",
        profileUrl: "https://fosstodon.org/users/sam",
        followedAt: 1_767_139_200_000,
      },
      // Hostile rows: a non-https avatar and an internal host are dropped
      // before anything reaches a texture loader.
      {
        handle: "@mallory@evil.test",
        url: "https://evil.test/users/mallory",
        avatarUrl: "javascript:alert(1)",
        profileUrl: "https://evil.test/users/mallory",
      },
    ],
  };
  await prepareWorldPage(page, "world-repository-followers", {
    repositoryFixture: {},
    repositoryStarFixture: starFixture,
    repositoryFollowerFixture: followerFixture,
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return (
      shell?.repositoryFollowerStates?.get("forkmesh/forkmesh")?.status ===
      "ready"
    );
  });
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return Boolean(
      shell?.world?.scene?.getObjectByName(
        "repository-star-button:forkmesh/forkmesh",
      )?.userData?.repositoryStar?.starCount,
    );
  });

  const portal = await page.locator("forkmesh-world").evaluate((shell) => {
    const scene = shell.world.scene;
    const starButton = scene.getObjectByName(
      "repository-star-button:forkmesh/forkmesh",
    );
    const gallery = scene.getObjectByName(
      "repository-fediverse-followers:forkmesh/forkmesh",
    );
    const figures = [];
    gallery?.children.forEach((child) => {
      if (String(child.name || "").startsWith("repository-follower:")) {
        figures.push({
          name: child.name,
          // Seated on the ground, inside the ring, turned back at the circle.
          y: Number(child.position.y.toFixed(2)),
          z: Number(child.position.z.toFixed(2)),
          facesCircle: Math.abs(child.rotation.y - Math.PI) < 0.001,
          follower: child.userData.repositoryFollower,
        });
      }
    });
    return {
      starCountFromDatabase: starButton?.userData?.repositoryStar?.starCount,
      starCaptionPresent: Boolean(
        scene.getObjectByName("repository-star-caption:forkmesh/forkmesh"),
      ),
      followerCount: shell.repositoryFollowerStates.get("forkmesh/forkmesh")
        ?.count,
      figures,
      captionPresent: Boolean(
        gallery?.getObjectByName?.(
          "repository-fediverse-follower-caption:forkmesh/forkmesh",
        ),
      ),
    };
  });

  // The star on top of the circle reports the relay's stored repo_stars total.
  expect(portal.starCountFromDatabase).toBe(1284);
  expect(portal.starCaptionPresent).toBe(true);
  // The caption uses the authoritative total, not the capped list length.
  expect(portal.followerCount).toBe(9);
  expect(portal.captionPresent).toBe(true);
  expect(portal.figures.map(({ name }) => name)).toEqual([
    "repository-follower:@kate@mastodon.social",
    "repository-follower:@sam@fosstodon.org",
    "repository-follower:@mallory@evil.test",
  ]);
  portal.figures.forEach((figure) => {
    expect(figure.facesCircle).toBe(true);
    expect(figure.y).toBeCloseTo(-2.53, 2);
    expect(figure.z).toBeGreaterThan(0);
  });
  expect(portal.figures[0].follower).toMatchObject({
    handle: "@kate@mastodon.social",
    name: "Kate Mirrors",
    about: "Rust, embedded, and self-hosted git.",
    instance: "mastodon.social",
    avatar: "https://files.mastodon.social/kate.png",
    profileUrl: "https://mastodon.social/@kate",
  });
  expect(portal.figures[1].follower).toMatchObject({
    handle: "@sam@fosstodon.org",
    name: "",
    about: "",
    avatar: "",
  });
  expect(portal.figures[2].follower.avatar).toBe("");
  expect(
    followerFixture.requests.filter((request) => request.method === "GET")
      .length,
  ).toBeGreaterThanOrEqual(1);
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

test("visiting the repository sunburst frames it in third-person", async ({
  page,
}) => {
  await prepareWorldPage(page, "world-repository-third-person", {
    repositoryFixture: {},
  });
  await openWorldRepositoryExplorer(page);

  await page.locator("[data-world-repo-scene]").click();
  const toggle = page.locator("[data-world-camera-toggle]");
  await expect(toggle).toHaveAttribute("aria-pressed", "false");
  await expect(toggle).toHaveAttribute(
    "aria-label",
    "Enter first-person view",
  );
  const framed = await page.locator("forkmesh-world").evaluate((shell) => ({
    camera: shell.world.getCameraState(),
    playerVisible: shell.world.player.visible,
    canvasMode: shell.world.renderer.domElement.dataset.cameraMode,
  }));
  expect(framed).toMatchObject({
    camera: { mode: "third-person", firstPerson: false },
    playerVisible: true,
    canvasMode: "third-person",
  });
  expect(framed.camera.zoom).toBeLessThanOrEqual(0.55);

  // First person stays available, just never automatic.
  await toggle.click();
  await expect(toggle).toHaveAttribute("aria-pressed", "true");
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => ({
      mode: shell.world.getCameraState().mode,
      playerVisible: shell.world.player.visible,
    })),
  ).toEqual({ mode: "first-person", playerVisible: false });
});

test("zooming first-person all the way back restores third-person", async ({
  page,
}) => {
  await prepareWorldPage(page, "world-first-person-zoom-out");
  await waitForWorld(page);

  const toggle = page.locator("[data-world-camera-toggle]");
  await toggle.click();
  await expect(toggle).toHaveAttribute("aria-pressed", "true");

  const canvas = page.locator("[data-world-canvas-wrap] canvas");
  const box = await canvas.boundingBox();
  expect(box).not.toBeNull();
  const centre = {
    x: Math.round(box.x + box.width / 2),
    y: Math.round(box.y + box.height / 2),
  };
  await page.mouse.move(centre.x, centre.y);

  // Widening the eyes stays in first person right up to the zoom-out floor.
  await page.mouse.wheel(0, 480);
  await expect
    .poll(() =>
      page
        .locator("forkmesh-world")
        .evaluate((shell) => shell.world.getCameraState().mode),
    )
    .toBe("first-person");
  await page.mouse.wheel(0, 480);
  const floored = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getCameraState(),
  );
  expect(floored.mode).toBe("first-person");
  expect(floored.zoom).toBeCloseTo(floored.minZoom, 5);

  // One more notch back steps out of the avatar's head entirely.
  await page.mouse.wheel(0, 480);
  await expect(toggle).toHaveAttribute("aria-pressed", "false");
  await expect(toggle).toHaveAttribute(
    "aria-label",
    "Enter first-person view",
  );
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => ({
      mode: shell.world.getCameraState().mode,
      playerVisible: shell.world.player.visible,
      canvasMode: shell.world.renderer.domElement.dataset.cameraMode,
      fov: shell.world.camera.fov,
    })),
  ).toEqual({
    mode: "third-person",
    playerVisible: true,
    canvasMode: "third-person",
    fov: 44,
  });
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
  await expect(
    page.getByRole("heading", { name: "Repository portals" }),
  ).toHaveCount(0);
  await expect(
    page.getByText(
      "Authorized repositories form distinct perimeter portals with size-weighted file rings.",
    ),
  ).toHaveCount(0);
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

test("the authenticated member appears immediately and active time advances locally", async ({
  page,
}) => {
  const session = {
    nodeName: "jett",
    sessionToken: "world-activity-session",
  };
  await prepareWorldPage(page, "active-leaderboard", {
    session,
    ticketActivity: {
      totalActiveMs: 0,
      activityObservedAt: FIXED_NOW,
    },
    directoryUsers: [
      { name: "jett", nodes: [], totalActiveMs: 0 },
      { name: "alice", nodes: [], totalActiveMs: 120_000 },
    ],
  });
  await waitForWorld(page);

  const first = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.syncMemberLounge();
    const own = shell
      .leaderboardMembers()
      .find((member) => member.name === "jett");
    const key = JSON.parse(
      shell.world.scene.getObjectByName("world-active-leaderboard").userData.key,
    );
    return {
      activeNow: own.activeNow,
      totalActiveMs: own.totalActiveMs,
      rankedNames: key.map((row) => row[0]),
    };
  });
  expect(first.activeNow).toBe(true);
  expect(first.totalActiveMs).toBeGreaterThanOrEqual(0);
  expect(first.rankedNames).toEqual(["alice", "jett"]);

  await page.waitForTimeout(1_150);
  const advanced = await page.locator("forkmesh-world").evaluate((shell) => {
    const own = shell
      .leaderboardMembers()
      .find((member) => member.name === "jett");
    return own.totalActiveMs;
  });
  expect(advanced).toBeGreaterThan(first.totalActiveMs + 900);

  const paused = await page.locator("forkmesh-world").evaluate((shell) => {
    shell.pauseWorldActivity();
    return {
      totalActiveMs: shell.currentWorldActivityMs(),
      running: Boolean(shell.worldActivityBaseAt),
      continuation: shell.worldActivityContinuation,
    };
  });
  await page.waitForTimeout(1_100);
  const stillPaused = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.currentWorldActivityMs(),
  );
  expect(paused.running).toBe(false);
  expect(paused.continuation).toBe("");
  expect(stillPaused).toBeCloseTo(paused.totalActiveMs, 3);
});

test("a fresh member spawns seated in the open Members Circle", async ({
  page,
}) => {
  const session = {
    nodeName: "newcomer",
    sessionToken: "fresh-member-circle-session",
  };
  await prepareWorldPage(page, "fresh-member-circle", {
    session,
    directoryUsers: [
      {
        name: "newcomer",
        nodes: [],
        createdAt: FIXED_NOW - 1_000,
      },
    ],
  });
  await waitForWorld(page);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return shell?.world?.scene?.getObjectByName(
      "campfire-newest-member-name-sparkles",
    )?.visible === true;
  });

  const arrival = await page.locator("forkmesh-world").evaluate((shell) => {
    const player = shell.world.player;
    const count = shell.world.scene.getObjectByName("campfire-member-count");
    const sparkle = shell.world.scene.getObjectByName(
      "campfire-newest-member-name-sparkles",
    );
    const fireSparksLeft = shell.world.scene.getObjectByName(
      "campfire-newest-member-fire-sparks-left",
    );
    const fireSparksRight = shell.world.scene.getObjectByName(
      "campfire-newest-member-fire-sparks-right",
    );
    const flame = shell.world.scene.getObjectByName("campfire-primary-flame");
    const dirt = shell.world.scene.getObjectByName(
      "campfire-member-circle-dirt",
    );
    const startHere = shell.world.scene.getObjectByName(
      "forkmesh-start-here-map",
    );
    return {
      seated: shell.freshArrivalCampfireSeated,
      activity: shell.lastMovement.activity,
      x: player.position.x,
      y: player.position.y,
      z: player.position.z,
      leftKnee: player.userData.leftKnee.rotation.x,
      countScale: count.scale.toArray(),
      countY: count.position.y,
      sparkleVisible: sparkle.visible,
      fireSparksVisible: fireSparksLeft.visible && fireSparksRight.visible,
      fireSparksSpan:
        fireSparksRight.geometry.attributes.position.getX(15) -
        fireSparksLeft.geometry.attributes.position.getX(15),
      fireHeight: flame.scale.y,
      fireWidth: flame.scale.x,
      dirtY: dirt.position.y,
      signPresent: Boolean(
        shell.world.scene.getObjectByName(
          "forkmesh-members-circle-path-sign",
        ),
      ),
      startHere: {
        x: startHere.position.x,
        z: startHere.position.z,
        rotation: startHere.rotation.y,
      },
    };
  });

  expect(arrival.seated).toBe(true);
  expect(arrival.activity).toBe("sitting beside the campfire");
  expect(Math.hypot(arrival.x, arrival.z - 130)).toBeGreaterThan(5);
  expect(Math.hypot(arrival.x, arrival.z - 130)).toBeLessThan(10);
  expect(arrival.y).toBeLessThan(0.38);
  expect(Math.abs(arrival.leftKnee)).toBeGreaterThan(0.5);
  expect(arrival.countScale).toEqual([9.5, 4.75, 1]);
  expect(arrival.countY).toBeGreaterThan(14);
  expect(arrival.sparkleVisible).toBe(true);
  expect(arrival.fireSparksVisible).toBe(true);
  expect(Math.abs(arrival.fireSparksSpan)).toBeGreaterThan(8);
  expect(arrival.fireHeight).toBeGreaterThan(2.5);
  expect(arrival.fireWidth).toBeGreaterThan(2.5);
  expect(arrival.dirtY).toBeGreaterThan(0.105);
  expect(arrival.signPresent).toBe(false);
  expect(arrival.startHere.x).toBe(0);
  expect(arrival.startHere.z).toBe(168);
  expect(arrival.startHere.rotation).toBeCloseTo(Math.PI, 5);

  // Position storage contains coordinates but deliberately no activity label.
  // A clean reload must recognize the bench ring and rebuild the seated pose.
  await page.reload();
  await waitForWorld(page);
  const reloaded = await page.locator("forkmesh-world").evaluate((shell) => ({
    activity: shell.lastMovement.activity,
    y: shell.world.player.position.y,
    leftKnee: shell.world.player.userData.leftKnee.rotation.x,
    radius: Math.hypot(
      shell.world.player.position.x,
      shell.world.player.position.z - 130,
    ),
  }));
  expect(reloaded.activity).toBe("sitting beside the campfire");
  expect(reloaded.y).toBeLessThan(0.38);
  expect(Math.abs(reloaded.leftKnee)).toBeGreaterThan(0.5);
  expect(reloaded.radius).toBeGreaterThan(5);
  expect(reloaded.radius).toBeLessThan(10);
});

test("the System Status board countdown advances between minute syncs", async ({
  page,
}) => {
  await prepareWorldPage(page, "status-board-countdown");
  await waitForWorld(page);

  const countdown = await page.locator("forkmesh-world").evaluate(
    (shell, fixedNow) => {
      let now = fixedNow + 5_000;
      Date.now = () => now;
      shell.statusBoardLoad = null;
      shell.statusBoardRequestedAt = fixedNow;
      shell.statusBoardLastCheckAt = fixedNow - 25_000;
      shell.syncSystemStatusBoardTimer();
      const dial = shell.world.scene.getObjectByName(
        "forkmesh-status-banner-countdown",
      );
      const first = {
        seconds: dial.userData.countdownSeconds,
        loading: dial.userData.countdownLoading,
      };
      now += 11_000;
      shell.syncSystemStatusBoardTimer();
      return {
        first,
        second: {
          seconds: dial.userData.countdownSeconds,
          loading: dial.userData.countdownLoading,
        },
      };
    },
    FIXED_NOW,
  );

  expect(countdown.first).toEqual({ seconds: 55, loading: false });
  expect(countdown.second).toEqual({ seconds: 44, loading: false });
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

test("sound button is the master switch for local playback", async ({
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

  await page.locator("forkmesh-world").evaluate(async (shell) => {
    const now = document.createElement("div");
    now.dataset.worldMediaNow = "";
    shell.append(now);
    await shell.playRadio("forkmesh-focus");
  });
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
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.playRadio("forkmesh-focus"),
  );
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

  await page.locator("[data-world-sound-toggle]").click();
  await expect(page.locator("[data-world-sound-toggle]")).toHaveAttribute(
    "aria-pressed",
    "false",
  );
  await expect(page.locator("[data-world-media-now]")).toContainText(
    "Nothing is playing",
  );
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => ({
      soundEnabled: shell.soundEnabled,
      activeAudio: shell.activeAudio,
      soundContext: shell.soundContext,
    })),
  ).toEqual({
    soundEnabled: false,
    activeAudio: null,
    soundContext: null,
  });

  // The same button can turn audio consent back on after a full shutdown.
  await page.locator("[data-world-sound-toggle]").click();
  await expect(page.locator("[data-world-sound-toggle]")).toHaveAttribute(
    "aria-pressed",
    "true",
  );
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

test("focus music defaults to Cosmic Waves and preserves local controls", async ({
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
  const cosmic = page.locator(
    "[data-world-focus-track='cosmic-waves']",
  );
  await expect(cosmic).toBeVisible();
  expect(await focusMusicTrackIsChecked(page, "cosmic-waves")).toBe(true);
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Cosmic Waves is selected. Press Play",
  );

  const idle = await focusMusicProbeSnapshot(page);
  expect(idle.created).toEqual([]);
  expect(mediaRequests).toEqual([]);
  const intervalRegistrationsBeforePlay = idle.intervalRegistrations;

  await page.locator("[data-world-focus-play]").click();
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Cosmic Waves is playing",
  );
  let playback = await focusMusicProbeSnapshot(page);
  expect(playback.created).toHaveLength(1);
  expect(playback.created[0]).toMatchObject({
    src: "/assets/music/cosmic-waves.ogg",
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

  await chooseFocusMusicTrack(page, "dreamscape");
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "DreamScape is playing",
  );
  playback = await focusMusicProbeSnapshot(page);
  expect(playback.created).toHaveLength(2);
  expect(playback.created[0]).toMatchObject({
    currentTime: 0,
    paused: true,
    pauseCalls: 1,
  });
  expect(playback.created[1]).toMatchObject({
    src: "/assets/music/dreamscape.ogg",
    loop: true,
    paused: false,
    playCalls: 1,
  });

  await page.locator("[data-world-focus-pause]").click();
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "DreamScape is paused",
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
    focusMusicTrackId: "dreamscape",
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

  await chooseFocusMusicTrack(page, "too-brief-a-time");
  await page.locator("[data-world-focus-volume]").fill("48");
  await page.locator("[data-world-focus-mute]").click();
  expect((await focusMusicProbeSnapshot(page)).created).toEqual([]);

  await page.reload();
  await waitForWorldReady(page);
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("broadcast"),
  );
  expect(await focusMusicTrackIsChecked(page, "too-brief-a-time")).toBe(true);
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
    src: "/assets/music/too-brief-a-time.ogg",
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
  expect(await focusMusicTrackIsChecked(page, "cosmic-waves")).toBe(true);
  await expect(page.locator("[data-world-focus-now]")).toContainText(
    "Cosmic Waves is selected. Press Play",
  );
  expect((await focusMusicProbeSnapshot(page)).created).toEqual([]);
});

test("portrait coarse-pointer thumbstick and visual viewport remain usable", async ({
  browser,
}) => {
  test.setTimeout(60_000);
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
  });
  const page = await context.newPage();
  const hiddenFocusWarnings = [];
  page.on("console", (message) => {
    if (message.text().includes("Blocked aria-hidden on an element")) {
      hiddenFocusWarnings.push(message.text());
    }
  });
  await prepareWorldPage(page, "portrait-touch");
  await waitForWorld(page);

  await expect(page.locator("[data-world-thumbstick]")).toBeVisible();
  await page.locator("[data-world-settings-open]").first().click();
  await expect(page.locator("[data-world-settings]")).toHaveAttribute(
    "data-open",
    "true",
  );
  const statusNoteInput = page.locator("[data-world-status-note]");
  await statusNoteInput.focus();
  const before = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  await page.keyboard.press("w");
  const afterInput = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  expect(afterInput).toEqual(before);

  const stableHeight = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.lastStableViewportHeight,
  );
  await page.setViewportSize({ width: 390, height: 520 });
  await page.waitForTimeout(300);
  expect(
    await page.locator("forkmesh-world").evaluate(
      (shell) => shell.lastStableViewportHeight,
    ),
  ).toBe(stableHeight);
  await page.setViewportSize({ width: 520, height: 390 });
  await page.waitForFunction(
    () =>
      document.querySelector("forkmesh-world")?.style
        .getPropertyValue("--world-viewport-height") === "390px",
  );
  await page.locator("[data-world-settings-close]").click();
  await expect(page.locator("[data-world-settings]")).toHaveAttribute(
    "data-open",
    "false",
  );
  expect(hiddenFocusWarnings).toEqual([]);

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
      session: {
        kind: "user",
        nodeName: "visual-member",
        email: "visual-member@example.test",
        sessionToken: `visual-${viewport.name}-office-token`,
      },
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
    await expect(dock).toBeHidden();
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

test("ForkMesh Office remains a continuous World at 320 CSS pixels", async ({
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
    session: {
      kind: "user",
      nodeName: "alice",
      email: "alice@example.test",
      sessionToken: "alice-office-token",
    },
  });
  await waitForWorld(page);
  await openOfficeForVisual(page);
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(320);
  await expect(page.locator("[data-world-office-lobby]")).toBeHidden();
  await expect(page.locator("canvas.world-canvas")).toBeVisible();
  await context.close();
});
