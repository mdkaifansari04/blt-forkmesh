const { test, expect } = require("@playwright/test");





function mockChatApis(page) {
  return page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let body = { error: "not_found" };
    let status = 404;
    if (url.pathname === "/api/chat/room-key") {
      status = 200;
      body = {
        ok: true,
        room: "world-general",
        access: "public-world-general",
        passphrase: "playwright-dashboard-message-actions-passphrase",
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
}


test("dashboard chat edits and deletes your own message", async ({ page }) => {
  const retainedFrames = [];
  await mockChatApis(page);
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      socket.onMessage((message) => {
        if (JSON.parse(String(message)).persist === true) {
          retainedFrames.push(String(message));
        }
      });
    },
  );

  await page.goto("/dashboard/chat/index.html");
  await expect(page.locator("[data-dashboard-chat-status]").first()).toContainText(
    "Connected",
  );

  await page.locator("#fullChatInput").fill("first draft of my message");
  await page.locator("#fullChatSend").click();
  const row = page.locator("#fullChatMessages .group").first();
  await expect(row).toContainText("first draft of my message");
  await expect.poll(() => retainedFrames.length).toBe(1);



  await row.getByRole("button", { name: "Edit message" }).click();
  await row.locator("textarea").fill("second draft of my message");
  await row.getByRole("button", { name: "Save edit" }).click();
  await expect(row).toContainText("second draft of my message");
  await expect(row.locator(".chat-edited")).toHaveText("(edited)");
  await expect(row.locator("textarea")).toHaveCount(0);

  await expect.poll(() => retainedFrames.length).toBe(2);



  await row.getByRole("button", { name: "Delete message" }).click();
  await row.getByRole("button", { name: "Confirm delete" }).click();
  await expect(page.locator("#fullChatMessages")).not.toContainText(
    "second draft of my message",
  );
  await expect.poll(() => retainedFrames.length).toBe(3);
});





test("dashboard chat offers no edit or delete on someone else's message", async ({
  browser,
}) => {
  const room = "**/api/repo/mainnode/forkmesh/rooms/world-general/ws";
  const sockets = [];
  const contexts = [await browser.newContext(), await browser.newContext()];
  const pages = [];
  for (const context of contexts) {
    const page = await context.newPage();
    await mockChatApis(page);
    await page.routeWebSocket(room, (socket) => {
      sockets.push(socket);
      socket.onMessage((message) => {
        for (const peer of sockets) {
          if (peer !== socket) peer.send(String(message));
        }
      });
    });
    await page.goto("/dashboard/chat/index.html");
    await expect(page.locator("[data-dashboard-chat-status]").first())
      .toContainText("Connected");
    pages.push(page);
  }
  const [author, reader] = pages;

  await author.locator("#fullChatInput").fill("hello from a peer");
  await author.locator("#fullChatSend").click();

  const authorRow = author.locator("#fullChatMessages .group", {
    hasText: "hello from a peer",
  });
  await expect(authorRow.getByRole("button", { name: "Edit message" }))
    .toHaveCount(1);

  const readerRow = reader.locator("#fullChatMessages .group", {
    hasText: "hello from a peer",
  });
  await expect(readerRow).toBeVisible();
  await expect(readerRow.locator(".chat-message-actions")).toHaveCount(0);

  for (const context of contexts) await context.close();
});
