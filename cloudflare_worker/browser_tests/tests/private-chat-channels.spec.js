const { test, expect } = require("@playwright/test");


test("admin creates and invites while only the invited user joins", async ({ page }) => {
  const channelId = "a".repeat(32);
  const channels = [];
  const members = new Set();
  const retainedPrivateFrames = [];
  let privateConnections = 0;
  let keyVersion = 1;

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
  }, session("admin", true));

  await page.route("**/api/**", async (route) => {
    const request = route.request();
    const url = new URL(request.url());
    const authorization = request.headers().authorization || "";
    const actor = authorization.replace(/^Bearer\s+/i, "").replace(/-token$/, "");
    const isAdmin = actor === "admin";
    const channelPath = `/api/chat/channels/${channelId}`;
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
      body = { ok: true, users: [] };
    } else if (url.pathname === "/api/chat/activity") {
      status = 200;
      body = { ok: true, messageCount: 0, latestMessageTs: 0, userCount: 0 };
    } else if (url.pathname === "/api/chat/channels" && request.method() === "GET") {
      if (!actor) {
        status = 401;
        body = { error: "invalid_session" };
      } else {
        status = 200;
        body = {
          channels: channels
            .filter(() => isAdmin || members.has(actor))
            .map((channel) => ({ ...channel, canManage: isAdmin })),
        };
      }
    } else if (url.pathname === "/api/chat/channels" && request.method() === "POST") {
      if (!isAdmin) {
        status = 403;
        body = { error: "admin_required" };
      } else {
        const data = request.postDataJSON();
        const channel = {
          id: channelId,
          name: data.name,
          updatedAt: Date.now(),
          keyVersion,
          canManage: true,
        };
        channels.splice(0, channels.length, channel);
        status = 201;
        body = { ok: true, channel };
      }
    } else if (url.pathname === `${channelPath}/members`) {
      if (!isAdmin) {
        status = 403;
        body = { error: "admin_required" };
      } else if (request.method() === "GET") {
        status = 200;
        body = {
          members: [...members].map((username) => ({
            username,
            joinedAt: Date.now(),
          })),
        };
      } else if (request.method() === "POST") {
        const data = request.postDataJSON();
        members.add(data.username);
        status = 201;
        body = {
          ok: true,
          member: { username: data.username, joinedAt: Date.now() },
        };
      } else if (request.method() === "DELETE") {
        const data = request.postDataJSON();
        const removed = members.delete(data.username);
        if (removed) {
          keyVersion += 1;
          channels[0] = {
            ...channels[0],
            keyVersion,
            updatedAt: Date.now(),
          };
        }
        status = 200;
        body = { ok: true, removed, keyVersion };
      }
    } else if (
      url.pathname === `${channelPath}/room-access` &&
      (isAdmin || members.has(actor))
    ) {
      status = 200;
      body = {
        channel: { ...channels[0], canManage: isAdmin },
        room: `chat-channel:${channelId}:v${keyVersion}`,
        passphrase: `playwright-private-release-team-passphrase-v${keyVersion}`,
        webSocketUrl: `${channelPath}/ws?ticket=${actor}-ticket`,
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
  await page.routeWebSocket("**/api/chat/channels/*/ws?ticket=*", (socket) => {
    privateConnections += 1;
    socket.onMessage((message) => {
      const frame = String(message);
      if (JSON.parse(frame).persist === true) retainedPrivateFrames.push(frame);
    });
    if (privateConnections > 1) {
      setTimeout(() => {
        retainedPrivateFrames.forEach((frame) => socket.send(frame));
      }, 50);
    }
  });

  await page.goto("/chat.html");
  await expect(page.locator("#chat-channel-create")).toBeVisible();
  await page.locator("#chat-channel-create").click();
  await page.locator("#chat-channel-name").fill("release-team");
  await page.locator("#chat-channel-create-form button[type='submit']").click();

  await expect(page.locator("#chat-channel-title")).toHaveText("#release-team");
  await expect(page.locator("#chat-status")).toContainText("private #release-team");
  await page.locator("#chat-channel-username").fill("alice");
  await page.locator("#chat-channel-invite-form button[type='submit']").click();
  await expect(page.locator("#chat-channel-members")).toContainText("@alice");
  await page.setViewportSize({ width: 390, height: 844 });
  await expect.poll(() => page.evaluate(
    () => document.documentElement.scrollWidth <= window.innerWidth,
  )).toBe(true);

  await page.locator("#chat-channel-dialog-close").click();
  await page.locator("#chat-input").fill("Private release is ready");
  await page.locator("#chat-send").click();
  await expect.poll(() => retainedPrivateFrames.length).toBe(1);
  expect(retainedPrivateFrames[0]).not.toContain("Private release is ready");

  await page.evaluate((value) => {
    localStorage.setItem("forkmesh.session", JSON.stringify(value));
  }, session("alice"));
  await page.reload();

  await expect(page.locator("#chat-channel-create")).toBeHidden();
  await expect(page.locator("#chat-channel-title")).toHaveText("#release-team");
  await expect(page.locator("#chat-log")).toContainText("Private release is ready");

  await page.evaluate((value) => {
    localStorage.setItem("forkmesh.session", JSON.stringify(value));
  }, session("admin", true));
  await page.reload();

  await expect(page.locator("#chat-channel-manage")).toBeVisible();
  await page.locator("#chat-channel-manage").click();
  await page.getByRole("button", { name: "Remove alice from channel" }).click();
  await expect(page.locator("#chat-channel-members")).not.toContainText("@alice");

  await page.evaluate((value) => {
    localStorage.setItem("forkmesh.session", JSON.stringify(value));
  }, session("alice"));
  await page.reload();

  await expect(page.locator("#chat-channel-title")).toHaveText("#general");
  await expect(page.locator("#chat-rooms")).not.toContainText("release-team");
  await expect(page.locator("#chat-channel-create")).toBeHidden();

  await page.evaluate((value) => {
    localStorage.setItem("forkmesh.session", JSON.stringify(value));
  }, session("bob"));
  await page.reload();

  await expect(page.locator("#chat-channel-title")).toHaveText("#general");
  await expect(page.locator("#chat-rooms")).not.toContainText("release-team");
  await expect(page.locator("#chat-channel-create")).toBeHidden();
});
