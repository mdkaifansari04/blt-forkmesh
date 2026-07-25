const { test, expect } = require("@playwright/test");


test("registered users share one participant-only direct message", async ({ page }) => {
  const conversationId = "d".repeat(32);
  const participants = new Set(["alice", "bob"]);
  const retainedFrames = [];
  let conversationCreated = false;
  let createCount = 0;
  let startRequests = 0;

  const session = (name, isAdmin = false) => ({
    kind: "user",
    nodeName: name,
    email: `${name}@example.test`,
    sessionToken: `${name}-token`,
    isAdmin,
  });

  await page.addInitScript((value) => {
    if (!localStorage.getItem("forkmesh.session")) {
      localStorage.setItem("forkmesh.session", JSON.stringify(value));
    }
  }, session("alice"));

  await page.route("**/api/**", async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const authorization = request.headers().authorization || "";
    const actor = authorization
      .replace(/^Bearer\s+/i, "")
      .replace(/-token$/, "");
    const directPath = `/api/chat/direct-messages/${conversationId}`;
    let status = 404;
    let body = { error: "not_found" };

    if (url.pathname === "/api/chat/room-key") {
      status = 200;
      body = {
        ok: true,
        room: "world-general",
        access: "public-world-general",
        passphrase: "playwright-public-room-passphrase",
      };
    } else if (url.pathname === "/api/accounts/users") {
      status = 200;
      body = {
        ok: true,
        users: ["alice", "bob", "admin"].map((name) => ({
          name,
          createdAt: Date.now() - 60_000,
        })),
      };
    } else if (url.pathname === "/api/chat/activity") {
      status = 200;
      body = {
        ok: true,
        messageCount: 0,
        latestMessageTs: 0,
        userCount: 0,
      };
    } else if (
      url.pathname === "/api/chat/channels" &&
      request.method() === "GET"
    ) {
      status = actor ? 200 : 401;
      body = actor ? { channels: [] } : { error: "invalid_session" };
    } else if (
      url.pathname === "/api/chat/direct-messages" &&
      request.method() === "GET"
    ) {
      if (!actor) {
        status = 401;
        body = { error: "invalid_session" };
      } else {
        status = 200;
        body = {
          conversations:
            conversationCreated && participants.has(actor)
              ? [{
                  id: conversationId,
                  otherUser: actor === "alice" ? "bob" : "alice",
                  updatedAt: Date.now(),
                  keyVersion: 1,
                }]
              : [],
        };
      }
    } else if (
      url.pathname === "/api/chat/direct-messages" &&
      request.method() === "POST"
    ) {
      startRequests += 1;
      const data = request.postDataJSON();
      if (!participants.has(actor) || !participants.has(data.username)) {
        status = 404;
        body = { error: "user_not_found" };
      } else {
        status = conversationCreated ? 200 : 201;
        if (!conversationCreated) createCount += 1;
        conversationCreated = true;
        body = {
          ok: true,
          conversation: {
            id: conversationId,
            otherUser: data.username,
            updatedAt: Date.now(),
            keyVersion: 1,
          },
        };
      }
    } else if (
      url.pathname === `${directPath}/room-access` &&
      request.method() === "GET" &&
      participants.has(actor)
    ) {
      status = 200;
      body = {
        conversation: {
          id: conversationId,
          otherUser: actor === "alice" ? "bob" : "alice",
          updatedAt: Date.now(),
          keyVersion: 1,
        },
        room: `chat-direct:${conversationId}:v1`,
        passphrase: "playwright-alice-bob-direct-passphrase",
        webSocketUrl: `${directPath}/ws?ticket=${actor}-ticket`,
      };
    }

    await route.fulfill({
      status,
      contentType: "application/json",
      body: JSON.stringify(body),
    });
  });

  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    () => {},
  );
  await page.routeWebSocket(
    "**/api/chat/direct-messages/*/ws?ticket=*",
    (socket) => {
      socket.onMessage((message) => {
        const frame = String(message);
        if (JSON.parse(frame).persist === true) retainedFrames.push(frame);
      });
      setTimeout(() => {
        retainedFrames.forEach((frame) => socket.send(frame));
      }, 50);
    },
  );

  await page.goto("/chat.html");
  await expect(page.locator("#chat-direct-section")).toBeVisible();
  await page.getByRole("button", { name: "New direct message" }).click();
  await page.locator("#chat-direct-search").fill("bob");
  await page.locator("#chat-direct-dialog")
    .getByRole("button", { name: "Message bob", exact: true })
    .click();

  await expect(page.locator("#chat-channel-title")).toHaveText("@bob");
  await expect(page.locator("#chat-channel-visibility-badge")).toHaveText("direct");
  await expect(page.locator("#chat-input")).toHaveAttribute(
    "placeholder",
    "Message @bob…",
  );
  await page.locator("#chat-input").fill("Private hello for Bob");
  await page.locator("#chat-send").click();
  await page.locator("#chat-attachment-input").setInputFiles({
    name: "private-note.txt",
    mimeType: "text/plain",
    buffer: Buffer.from("for Bob only"),
  });
  await expect.poll(() => retainedFrames.length).toBe(2);
  expect(retainedFrames.join("\n")).not.toContain("Private hello for Bob");
  expect(retainedFrames.join("\n")).not.toContain("for Bob only");

  await page.getByRole("button", { name: "Message bob", exact: true }).click();
  expect(startRequests).toBe(2);
  expect(createCount).toBe(1);

  await page.evaluate((value) => {
    localStorage.setItem("forkmesh.session", JSON.stringify(value));
  }, session("bob"));
  await page.reload();

  await expect(page.locator("#chat-channel-title")).toHaveText("@alice");
  await expect(page.locator("#chat-log")).toContainText("Private hello for Bob");
  await expect(page.locator("#chat-log")).toContainText("private-note.txt");
  await expect(page.locator("#chat-direct-list")).toContainText("alice");

  await page.setViewportSize({ width: 390, height: 844 });
  const mobileLayout = await page.evaluate(() => ({
    innerWidth: window.innerWidth,
    scrollWidth: document.documentElement.scrollWidth,
    offenders: [...document.querySelectorAll("body *")]
      .map((element) => ({
        tag: element.tagName.toLowerCase(),
        id: element.id,
        className: String(element.className || ""),
        left: Math.round(element.getBoundingClientRect().left),
        right: Math.round(element.getBoundingClientRect().right),
      }))
      .filter((item) => item.right > window.innerWidth + 1 || item.left < -1)
      .slice(0, 12),
  }));
  expect(mobileLayout.scrollWidth, JSON.stringify(mobileLayout, null, 2))
    .toBeLessThanOrEqual(mobileLayout.innerWidth);

  await page.evaluate((value) => {
    localStorage.setItem("forkmesh.session", JSON.stringify(value));
  }, session("admin", true));
  await page.reload();

  await expect(page.locator("#chat-channel-title")).toHaveText("#general");
  await expect(page.locator("#chat-direct-list")).not.toContainText("alice");
  await expect(page.locator("#chat-direct-list")).not.toContainText("bob");
  const denied = await page.evaluate(async (path) => {
    const response = await fetch(path, {
      headers: { Authorization: "Bearer admin-token" },
    });
    return { status: response.status, body: await response.json() };
  }, `/api/chat/direct-messages/${conversationId}/room-access`);
  expect(denied).toEqual({ status: 404, body: { error: "not_found" } });
});
