const { test, expect } = require("@playwright/test");
const fs = require("node:fs");
const path = require("node:path");

const HEADER_RULES = fs.readFileSync(
  path.resolve(__dirname, "..", "..", "public", "_headers"),
  "utf8",
);

function headerValueForRule(rule, headerName) {
  const lines = HEADER_RULES.split(/\r?\n/);
  const start = lines.findIndex((line) => line.trim() === rule);
  if (start < 0) throw new Error(`Missing ${rule} in public/_headers`);
  const prefix = `${headerName.toLowerCase()}:`;
  for (let index = start + 1; index < lines.length; index += 1) {
    const line = lines[index];
    if (line && !/^\s/.test(line)) break;
    const header = line.trim();
    if (header.toLowerCase().startsWith(prefix)) {
      return header.slice(header.indexOf(":") + 1).trim();
    }
  }
  throw new Error(`Missing ${headerName} in ${rule}`);
}

const DASHBOARD_CHAT_CSP = headerValueForRule(
  "/dashboard/chat*",
  "Content-Security-Policy",
);

test("World embeds same-origin global chat and connects its real room transport", async ({
  page,
}) => {
  const passphrase = "playwright-world-embed-passphrase";
  const socketURLs = [];
  const browserErrors = [];

  page.on("console", (message) => {
    if (message.type() === "error") browserErrors.push(message.text());
  });
  page.on("pageerror", (error) => browserErrors.push(error.message));

  // Keep this test deterministic while still asking Chromium to enforce the
  // production CSP against the real script URLs used by the chat document.
  await page.route("https://cdn.tailwindcss.com/**", (route) =>
    route.fulfill({
      contentType: "application/javascript",
      body: "globalThis.tailwind = globalThis.tailwind || {};",
    }),
  );
  await page.route("https://cdn.jsdelivr.net/**", (route) =>
    route.fulfill({
      contentType: "application/javascript",
      body: "globalThis.lucide = { createIcons() {} };",
    }),
  );
  await page.route("https://static.cloudflareinsights.com/**", (route) =>
    route.fulfill({
      contentType: "application/javascript",
      body: "",
    }),
  );

  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let body = { error: "not_found" };
    let status = 404;
    if (url.pathname === "/api/chat/room-key") {
      expect(url.searchParams.get("room")).toBe("world-general");
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
      socketURLs.push(socket.url());
    },
  );

  // These are the production response controls declared in public/_headers:
  // cross-origin framing stays forbidden, but the World may embed chat from
  // its own origin.
  await page.route(
    "**/dashboard/chat/index.html?worldEmbed=1",
    async (route) => {
      const response = await route.fetch();
      await route.fulfill({
        response,
        headers: {
          ...response.headers(),
          "Content-Security-Policy": DASHBOARD_CHAT_CSP,
          "X-Frame-Options": "SAMEORIGIN",
        },
      });
    },
  );
  await page.route("**/world-chat-embed-fixture", (route) =>
    route.fulfill({
      contentType: "text/html; charset=utf-8",
      body: `<!doctype html>
        <title>World chat embed fixture</title>
        <iframe
          title="ForkMesh World chat terminal"
          src="/dashboard/chat/index.html?worldEmbed=1"
          sandbox="allow-forms allow-same-origin allow-scripts"
        ></iframe>`,
    }),
  );

  await page.goto("/world-chat-embed-fixture");
  const chat = page.frameLocator(
    'iframe[title="ForkMesh World chat terminal"]',
  );
  await expect(chat.locator("[data-dashboard-chat-status]").first()).toContainText(
    "Connected",
  );
  await expect
    .poll(() => socketURLs.length)
    .toBe(1);
  expect(
    await chat.locator("html").getAttribute("data-world-embed"),
  ).toBe("1");
  expect(
    browserErrors.filter((message) =>
      /content security policy|refused to load the script|lucide is not defined/i.test(
        message,
      ),
    ),
  ).toEqual([]);
});


test("clipboard images and documents stay encrypted and survive refresh", async ({ page }) => {
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

  await page.locator("#chat-input").evaluate((input) => {
    const png = Uint8Array.from(atob(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=",
    ), (char) => char.charCodeAt(0));
    const transfer = new DataTransfer();
    transfer.items.add(new File([png], "clipboard-image.png", { type: "image/png" }));
    input.dispatchEvent(new ClipboardEvent("paste", {
      bubbles: true,
      cancelable: true,
      clipboardData: transfer,
    }));
  });

  const image = page.locator(".chat-attachment-image");
  await expect(image).toBeVisible();
  await expect(image).toHaveAttribute("alt", "clipboard-image.png");

  const documentInput = page.locator("#chat-attachment-input");
  await documentInput.setInputFiles({
    name: "release-notes.txt",
    mimeType: "text/plain",
    buffer: Buffer.from("private release notes"),
  });

  const documentCard = page.locator(".chat-attachment-card", {
    hasText: "release-notes.txt",
  });
  await expect(documentCard).toBeVisible();
  await expect(documentCard.locator("a[download='release-notes.txt']")).toBeVisible();
  await expect.poll(() => retainedFrames.length).toBe(2);
  expect(retainedFrames.join("\n")).not.toContain("release-notes.txt");
  expect(retainedFrames.join("\n")).not.toContain("private release notes");

  await documentInput.setInputFiles({
    name: "too-large.bin",
    mimeType: "application/octet-stream",
    buffer: Buffer.alloc(1024 * 1024 + 1),
  });
  await expect(page.locator("#chat-attachment-feedback")).toContainText(
    "1 MiB or smaller",
  );
  await expect.poll(() => retainedFrames.length).toBe(2);

  await page.reload();

  await expect(page.locator("#chat-status")).toContainText("Connected");
  await expect(page.locator(".chat-attachment-image")).toBeVisible();
  await expect(page.locator(".chat-attachment-card", {
    hasText: "release-notes.txt",
  })).toBeVisible();
});


test("dashboard chat shares clipboard images and documents", async ({ page }) => {
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
        passphrase: "playwright-dashboard-attachment-passphrase",
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
    () => {},
  );

  await page.goto("/dashboard/chat/index.html");
  await expect(page.getByRole("link", { name: "Open private channels" }))
    .toHaveAttribute("href", "/chat");
  await expect(page.locator("[data-dashboard-chat-status]").first()).toContainText(
    "Connected",
  );

  await page.locator("#fullChatInput").evaluate((input) => {
    const png = Uint8Array.from(atob(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=",
    ), (char) => char.charCodeAt(0));
    const transfer = new DataTransfer();
    transfer.items.add(new File([png], "dashboard-paste.png", { type: "image/png" }));
    input.dispatchEvent(new ClipboardEvent("paste", {
      bubbles: true,
      cancelable: true,
      clipboardData: transfer,
    }));
  });
  await expect(page.locator("#fullChatMessages .chat-attachment-image")).toBeVisible();

  await page.locator("#fullChatInputAttachmentInput").setInputFiles({
    name: "dashboard-notes.md",
    mimeType: "text/markdown",
    buffer: Buffer.from("encrypted dashboard document"),
  });
  await expect(page.locator("#fullChatMessages .chat-attachment-card", {
    hasText: "dashboard-notes.md",
  })).toBeVisible();
});
