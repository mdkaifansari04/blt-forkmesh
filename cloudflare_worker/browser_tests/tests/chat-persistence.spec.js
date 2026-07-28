const { test, expect } = require("@playwright/test");


test("self-authored retained chat survives a page refresh", async ({ page }) => {
  const passphrase = "playwright-public-world-general-passphrase";
  const retainedFrames = [];
  let connectionCount = 0;

  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let body = { error: "not_found" };
    let status = 404;
    if (url.pathname === "/api/chat/room-key") {
      status = 200;
      body = {
        ok: true,
        room: "world-general",
        access: "public-world-general",
        passphrase,
      };
    } else if (url.pathname === "/api/accounts/users") {
      status = 200;
      body = { ok: true, users: [] };
    } else if (url.pathname === "/api/chat/activity") {
      status = 200;
      body = { ok: true, messageCount: 0, latestMessageTs: 0, userCount: 0 };
    }
    return route.fulfill({
      status,
      contentType: "application/json",
      body: JSON.stringify(body),
    });
  });

  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      connectionCount += 1;
      socket.onMessage((message) => {
        const frame = String(message);
        if (JSON.parse(frame).persist === true) retainedFrames.push(frame);
      });
      if (connectionCount > 1) {
        setTimeout(() => {
          retainedFrames.forEach((frame) => socket.send(frame));
        }, 50);
      }
    },
  );

  await page.goto("/chat.html");
  await expect(page.locator("#chat-status")).toContainText("Connected");
  await page.locator("#chat-input").fill("My message must survive refresh");
  await page.locator("#chat-send").click();
  await expect(page.locator("#chat-log")).toContainText(
    "My message must survive refresh",
  );
  await expect.poll(() => retainedFrames.length).toBe(1);

  await page.reload();

  await expect(page.locator("#chat-status")).toContainText("Connected");
  await expect(page.locator("#chat-log")).toContainText(
    "My message must survive refresh",
  );
});


test("a public chat message accepts and sends a thread reply", async ({ page }) => {
  const passphrase = "playwright-public-thread-passphrase";
  const retainedFrames = [];

  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let body = { error: "not_found" };
    let status = 404;
    if (url.pathname === "/api/chat/room-key") {
      status = 200;
      body = {
        ok: true,
        room: "world-general",
        access: "public-world-general",
        passphrase,
      };
    } else if (url.pathname === "/api/accounts/users") {
      status = 200;
      body = { ok: true, users: [] };
    } else if (url.pathname === "/api/chat/activity") {
      status = 200;
      body = { ok: true, messageCount: 0, latestMessageTs: 0, userCount: 0 };
    }
    return route.fulfill({
      status,
      contentType: "application/json",
      body: JSON.stringify(body),
    });
  });
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      socket.onMessage((message) => {
        const frame = String(message);
        if (JSON.parse(frame).persist === true) retainedFrames.push(frame);
      });
    },
  );

  await page.goto("/chat.html");
  await expect(page.locator("#chat-status")).toContainText("Connected");
  const rootText = "Thread root from the browser";
  const replyText = "Thread reply from the browser";
  await page.locator("#chat-input").fill(rootText);
  await page.locator("#chat-input").press("Enter");
  const root = page.locator(".chat-msg", { hasText: rootText });
  await expect(root).toBeVisible();
  await root.hover();
  await root.getByRole("button", { name: "Reply in thread" }).click();
  await expect(page.locator("#chat-thread-input")).toBeFocused();
  await page.locator("#chat-thread-input").fill(replyText);
  await page.locator("#chat-thread-send").click();
  await expect(page.locator("#chat-thread-replies")).toContainText(replyText);
  await expect.poll(() => retainedFrames.length).toBe(2);
  await page.locator("#chat-thread-close").click();
  await expect(page.locator(".chat-thread-view")).toBeHidden();
});


test("a confirmed delete removes and broadcasts a public chat message", async ({
  page,
}) => {
  const retainedFrames = [];
  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let body = { error: "not_found" };
    let status = 404;
    if (url.pathname === "/api/chat/room-key") {
      status = 200;
      body = {
        ok: true,
        room: "world-general",
        access: "public-world-general",
        passphrase: "playwright-public-delete-passphrase",
      };
    } else if (url.pathname === "/api/accounts/users") {
      status = 200;
      body = { ok: true, users: [] };
    } else if (url.pathname === "/api/chat/activity") {
      status = 200;
      body = { ok: true, messageCount: 0, latestMessageTs: 0, userCount: 0 };
    }
    return route.fulfill({
      status,
      contentType: "application/json",
      body: JSON.stringify(body),
    });
  });
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      socket.onMessage((message) => {
        const frame = String(message);
        if (JSON.parse(frame).persist === true) retainedFrames.push(frame);
      });
    },
  );

  await page.goto("/chat.html");
  await expect(page.locator("#chat-status")).toContainText("Connected");
  const message = "Delete this public message";
  await page.locator("#chat-input").fill(message);
  await page.locator("#chat-input").press("Enter");
  const row = page.locator("#chat-log .chat-msg", { hasText: message });
  await expect(row).toBeVisible();
  await row.hover();
  await row.getByRole("button", { name: "Delete message" }).click();
  await expect(page.locator("#chat-delete-dialog")).toBeVisible();
  await page.locator("#chat-delete-confirm").click();
  await expect(page.locator("#chat-log")).not.toContainText(message);
  await expect.poll(() => retainedFrames.length).toBe(2);
});
