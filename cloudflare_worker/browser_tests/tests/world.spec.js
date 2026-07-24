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

async function prepareWorldPage(page, socketId, { includeThread = false } = {}) {
  let mentionState = "review";
  await page.addInitScript(({ now, identity }) => {
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
  }, { now: FIXED_NOW, identity: socketId });
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
    const body =
      url.pathname === "/api/world/context"
        ? { now: FIXED_NOW, countryCode: "" }
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
        : url.pathname === "/api/world/events"
          ? { events: [] }
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
            : url.pathname === "/api/security/quarantine"
              ? {
                  visibility: "aggregate-only",
                  summary: {
                    active: 2,
                    temporary: 2,
                    permanent: 0,
                    appealed: 0,
                    byReason: { automated_scanning: 2 },
                  },
                  restrictions: [],
                  allowedActions: [],
                }
            : url.pathname === "/api/repositories"
              ? { repositories: [] }
              : {};
    return route.fulfill({
      status: 200,
      contentType: "application/json; charset=utf-8",
      body: JSON.stringify(body),
    });
  });
  await page.routeWebSocket("**/api/world/ws", (socket) => {
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
    shell.world.setTheme("day");
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
