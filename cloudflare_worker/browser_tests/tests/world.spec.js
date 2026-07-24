const { test, expect } = require("@playwright/test");
const path = require("node:path");
const {
  createCipheriv,
  createDecipheriv,
  createHash,
  pbkdf2Sync,
} = require("node:crypto");

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
  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let status = 200;
    const body =
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
              ? {
                  authenticated: true,
                  accountStatus: "Registered",
                  name: session.nodeName,
                  ticket: "playwright-world-ticket",
                  expiresAt: FIXED_NOW + 300_000,
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
            : url.pathname === "/api/repositories"
              ? { repositories: [] }
              : {};
    return route.fulfill({
      status,
      contentType: "application/json; charset=utf-8",
      body: JSON.stringify(body),
    });
  });
  await page.routeWebSocket("**/api/world/ws*", (socket) => {
    socket.send(
      JSON.stringify({
        type: "welcome",
        id: socketId,
        peers: [],
      }),
    );
  });
}

function decryptChatEnvelope(envelope, passphrase, room) {
  const salt = createHash("sha256")
    .update(`ForkMesh room:${room}`, "utf8")
    .digest()
    .subarray(0, 16);
  const key = pbkdf2Sync(passphrase, salt, 210000, 32, "sha256");
  const decipher = createDecipheriv(
    "aes-256-gcm",
    key,
    Buffer.from(envelope.nonce, "base64"),
  );
  decipher.setAuthTag(Buffer.from(envelope.tag, "base64"));
  return JSON.parse(
    Buffer.concat([
      decipher.update(Buffer.from(envelope.body, "base64")),
      decipher.final(),
    ]).toString("utf8"),
  );
}

function encryptChatEnvelope(message, passphrase, room) {
  const salt = createHash("sha256")
    .update(`ForkMesh room:${room}`, "utf8")
    .digest()
    .subarray(0, 16);
  const key = pbkdf2Sync(passphrase, salt, 210000, 32, "sha256");
  const nonce = Buffer.alloc(12, 7);
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
  };
}

async function waitForWorld(page, url = "/world/") {
  await page.goto(url);
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return Boolean(shell?.world?.renderer?.domElement);
  });
  await page.evaluate(() => document.fonts?.ready);
  await page.locator("[data-world-loading]").waitFor({
    state: "attached",
  });
  await page.waitForFunction(
    () =>
      document
        .querySelector("[data-world-loading]")
        ?.getAttribute("aria-hidden") === "true",
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

test("World chat stays embedded without navigating or opening a tab", async ({
  page,
  context,
}) => {
  const passphrase = "playwright-public-world-general-passphrase";
  const roomKeyRequests = [];
  const chatSocketURLs = [];
  const chatFrames = [];
  let publicChatSocket = null;
  page.on("request", (request) => {
    const url = new URL(request.url());
    if (url.pathname === "/api/chat/room-key") {
      roomKeyRequests.push({
        room: url.searchParams.get("room"),
        authorization: request.headers().authorization || "",
      });
    }
  });
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      publicChatSocket = socket;
      chatSocketURLs.push(socket.url());
      socket.onMessage((message) => {
        chatFrames.push(JSON.parse(String(message)));
      });
    },
  );
  await prepareWorldPage(page, "world-chat", {
    chatPassphrase: passphrase,
  });
  await waitForWorld(page);

  const worldURL = page.url();
  const pageCount = context.pages().length;
  await page.locator("[data-world-chat-open]").first().click();

  const chat = page.locator("[data-world-chat]");
  await expect(chat).toBeVisible();
  const chatFrame = page.frameLocator("[data-world-chat-frame]");
  const chatInput = chatFrame.locator("#fullChatInput");
  await expect(chatInput).toBeVisible();
  await expect(
    chatFrame.locator("[data-dashboard-chat-status]").first(),
  ).toHaveText("Connected · public World #general");
  publicChatSocket.send(JSON.stringify(encryptChatEnvelope(
    {
      type: "chat",
      id: "forged-user-claim",
      senderId: "self-asserted-peer",
      sender: "Verified Admin",
      accountKind: "user",
      channel: "#general",
      text: "This identity claim is not verified.",
      ts: FIXED_NOW,
    },
    passphrase,
    "world-general",
  )));
  await expect(chatFrame.locator("#fullChatMessages")).toContainText(
    "World visitor · Verified Admin",
  );
  await expect(chatFrame.locator("#fullChatMessages")).toContainText(
    "This identity claim is not verified.",
  );
  await chatInput.fill("Hello from the public World");
  await chatFrame.locator("#fullChatSend").click();
  await expect.poll(
    () => chatFrames.filter((frame) => frame.persist === true).length,
  ).toBe(1);

  const envelope = chatFrames.find((frame) => frame.persist === true);
  const message = decryptChatEnvelope(
    envelope,
    passphrase,
    "world-general",
  );
  expect(message).toMatchObject({
    type: "chat",
    channel: "#general",
    text: "Hello from the public World",
    accountKind: "guest",
  });
  expect(message.sender).toMatch(/^World visitor · /);
  expect(roomKeyRequests).toContainEqual({
    room: "world-general",
    authorization: "",
  });
  expect(chatSocketURLs).toHaveLength(1);
  expect(new URL(chatSocketURLs[0]).pathname).toBe(
    "/api/repo/mainnode/forkmesh/rooms/world-general/ws",
  );
  expect(page.url()).toBe(worldURL);
  expect(context.pages()).toHaveLength(pageCount);

  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.openWorldChat("/dashboard/chat?space=sky-campus");
  });
  const restrictedFrame = page.frameLocator("[data-world-chat-frame]");
  await expect(restrictedFrame.locator("#fullChatInput")).toBeDisabled();
  await expect(
    restrictedFrame.locator("[data-dashboard-chat-status]").first(),
  ).toHaveText("User login required for this channel");
  expect(chatSocketURLs).toHaveLength(1);

  await chat.getByRole("button", { name: "Close World chat" }).click();
  await expect(chat).toBeHidden();
  expect(page.url()).toBe(worldURL);
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

async function freezeWorld(page) {
  await page.locator("forkmesh-world").evaluate((shell) => {
    // Keep the one shared clock authoritative. FIXED_NOW is 08:00 in the
    // four-hour cycle; visual tests must not revive a conflicting fixed-day
    // theme merely to stabilize the lighting.
    shell.world.setTheme("world");
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

async function holdTouch(page, locator, durationMs = 300) {
  const box = await locator.boundingBox();
  expect(box).not.toBeNull();
  const client = await page.context().newCDPSession(page);
  const point = {
    x: Math.round(box.x + box.width / 2),
    y: Math.round(box.y + box.height / 2),
    id: 1,
  };
  await client.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [point],
  });
  await page.waitForTimeout(durationMs);
  await client.send("Input.dispatchTouchEvent", {
    type: "touchEnd",
    touchPoints: [],
  });
  await client.detach();
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

test("desktop camera supports pointer-lock mouse look, camera-relative WASD, and wheel zoom", async ({
  page,
}) => {
  await prepareWorldPage(page, "desktop-fps-controls");
  await waitForWorld(page);

  const canvas = page.locator("[data-world-canvas-wrap] canvas");
  const box = await canvas.boundingBox();
  expect(box).not.toBeNull();
  const centre = {
    x: Math.round(box.x + box.width / 2),
    y: Math.round(box.y + box.height / 2),
  };

  await page.mouse.click(centre.x, centre.y);
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getCameraState().pointerLocked,
    ),
  ).toBe(true);

  const beforeLook = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getCameraState(),
  );
  // Playwright keeps its virtual cursor stationary while Chromium owns the
  // pointer. Dispatch the relative values that a real locked mousemove carries.
  await page.evaluate(() => {
    const relativeMove = new MouseEvent("mousemove", { bubbles: true });
    Object.defineProperties(relativeMove, {
      movementX: { value: 120 },
      movementY: { value: 35 },
    });
    document.dispatchEvent(relativeMove);
  });
  const afterLook = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getCameraState(),
  );
  expect(afterLook.yaw).not.toBeCloseTo(beforeLook.yaw, 4);
  expect(afterLook.pitch).not.toBeCloseTo(beforeLook.pitch, 4);

  const beforeMove = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getPosition(),
  );
  await page.keyboard.down("w");
  await page.waitForTimeout(240);
  await page.keyboard.up("w");
  const afterMove = await page.locator("forkmesh-world").evaluate(
    (shell) => shell.world.getPosition(),
  );
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

  await page.keyboard.press("Escape");
  await expect.poll(() =>
    page.locator("forkmesh-world").evaluate(
      (shell) => shell.world.getCameraState().pointerLocked,
    ),
  ).toBe(false);

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

test("landscape touch controls remain visible and move the avatar", async ({
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

  const control = page.locator("[data-move='forward']");
  await expect(control).toBeVisible();
  const canvasBox = await page.locator("[data-world-canvas-wrap]").boundingBox();
  expect(canvasBox).not.toBeNull();
  expect(canvasBox.height).toBeLessThanOrEqual(390);

  const before = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  await holdTouch(page, control);
  const after = await page.locator("forkmesh-world").evaluate((shell) =>
    shell.world.getPosition(),
  );
  expect(Math.hypot(after.x - before.x, after.z - before.z)).toBeGreaterThan(
    0.05,
  );
  await context.close();
});

test("two-finger pinch traverses the complete bounded mobile camera range", async ({
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

test("approved instances, local setup, and project support stay truthful", async ({
  page,
}) => {
  await prepareWorldPage(page, "truthful-panels");
  await waitForWorld(page);

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("routing"),
  );
  await expect(page.getByText("North relay")).toBeVisible();
  await expect(page.getByText("Garden relay")).toBeVisible();
  await expect(page.getByText("fresh signed node health")).toBeVisible();
  const routing = await page.locator("[data-world-detail]").textContent();
  expect(routing).not.toContain("must-not-render");
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

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("support"),
  );
  await expect(page.getByText("Voluntary project support")).toBeVisible();
  await expect(page.getByText("no financial return")).toBeVisible();
  await expect(page.getByRole("link", { name: "Open Patreon" })).toHaveAttribute(
    "href",
    "https://www.patreon.com/16434219/join",
  );
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

  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("routing"),
  );
  await page
    .getByRole("button", { name: "Inspect live server" })
    .first()
    .click();
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
  await page.locator("forkmesh-world").evaluate((shell) =>
    shell.openLandmark("support"),
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
    worldOffsetMs: shell.activeAudio?.worldOffsetMs,
    license: shell.activeAudio?.license,
  }));
  expect(playback.durationMs).toBe(4 * 60 * 60 * 1000);
  expect(playback.worldOffsetMs).toBeGreaterThanOrEqual(0);
  expect(playback.worldOffsetMs).toBeLessThan(playback.durationMs);
  expect(playback.license).toContain("CC0-1.0");
  await expect(page.locator("[data-world-track]")).toContainText(
    "loops with the shared World day",
  );
  expect(mediaRequests).toEqual([]);

  await page.locator("[data-world-radio-stop]").click();
  expect(
    await page.locator("forkmesh-world").evaluate((shell) => shell.activeAudio),
  ).toBeNull();
});

test("portrait coarse-pointer controls and visual viewport remain usable", async ({
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

  await expect(page.locator("[data-move='forward']")).toBeVisible();
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

  const control = page.locator("[data-move='forward']");
  // Leave enough time for more than one animation frame even when the release
  // gate is sharing a loaded CI host; the assertion still requires real
  // position movement from an actual CDP touch sequence.
  await holdTouch(page, control, 600);
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
  test(`visual acceptance ${viewport.name}`, async ({ browser }) => {
    const context = await browser.newContext(viewport.options);
    const page = await context.newPage();
    await prepareWorldPage(page, `visual-${viewport.name}`);
    await waitForWorld(page);
    await freezeWorld(page);
    await expect(page).toHaveScreenshot(`world-${viewport.name}.png`, {
      animations: "disabled",
      caret: "hide",
      maxDiffPixelRatio: 0.01,
    });
    await context.close();
  });
}
