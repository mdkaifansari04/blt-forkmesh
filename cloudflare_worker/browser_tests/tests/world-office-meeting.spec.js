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
const OFFICE_ENTRANCE_POSITION = [45, 0.38, -20.8];
const OFFICE_ENTRY_TICKET = "playwright-office-entry-ticket";

const REMOTE_PARTICIPANT = {
  id: "participant-remote",
  name: "Remote teammate",
  accountStatus: "Registered",
  x: 1.5,
  y: 0.38,
  z: -1,
  yaw: 0,
  moving: false,
  pose: "standing",
  chairId: "",
  bindingKey: null,
  updatedAt: 1785000000000,
};

function encryptChatEnvelope(message) {
  const salt = createHash("sha256")
    .update("ForkMesh room:world-general", "utf8")
    .digest()
    .subarray(0, 16);
  const key = pbkdf2Sync(
    "playwright-office-meeting-passphrase",
    salt,
    210000,
    32,
    "sha256",
  );
  const nonce = Buffer.alloc(12, 9);
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

async function prepareMeetingWorld(page) {
  const chatFrames = [];
  const meetingFrames = [];
  const officeEntryRequests = [];
  const generalAccessHeaders = [];
  let chatSocket = null;
  let meetingSocket = null;
  await page.addInitScript(() => {
    Date.now = () => 1785000000000;
    let seed = 0x0ff1ce;
    Math.random = () => {
      seed = (seed * 1664525 + 1013904223) >>> 0;
      return seed / 0x1_0000_0000;
    };
    sessionStorage.setItem("forkmesh.world.guestId.v1", "office-meeting");
  });
  await page.emulateMedia({ reducedMotion: "reduce" });
  await page.route(THREE_MODULE_URL, (route) => route.fulfill({
    path: THREE_MODULE_PATH,
    contentType: "text/javascript; charset=utf-8",
  }));
  await page.route("**/api/**", (route) => {
    const url = new URL(route.request().url());
    let status = 200;
    let body = {};
    if (url.pathname === "/api/world/context") {
      body = { now: 1785000000000, countryCode: "" };
    } else if (url.pathname === "/api/world/instances") {
      body = { instances: [] };
    } else if (url.pathname === "/api/world/fediverse") {
      body = { mastodon: [], lemmy: [], x: [], reddit: [] };
    } else if (url.pathname === "/api/world/media/spaces") {
      body = { spaces: [] };
    } else if (url.pathname === "/api/repositories") {
      body = { repositories: [] };
    } else if (url.pathname === "/api/chat/channels") {
      status = 401;
      body = { error: "unauthorized" };
    } else if (url.pathname === "/api/chat/room-key") {
      body = {
        ok: true,
        room: "world-general",
        passphrase: "playwright-office-meeting-passphrase",
      };
    } else if (url.pathname === "/api/world/office/general/status") {
      body = { ok: true, occupied: false };
    } else if (url.pathname === "/api/world/office/general/entry") {
      let requestBody = {};
      try {
        requestBody = route.request().postDataJSON();
      } catch (_) {}
      officeEntryRequests.push({
        method: route.request().method(),
        url: route.request().url(),
        body: requestBody,
      });
      body = {
        ok: true,
        occupied: false,
        entryTicket: OFFICE_ENTRY_TICKET,
        expiresAt: 1785000060000,
      };
    } else if (url.pathname === "/api/world/office/general/access") {
      generalAccessHeaders.push(route.request().headers());
      body = {
        room: { id: "general", name: "general", visibility: "public" },
        meetingWebSocketUrl:
          "/api/world/office/general/ws?ticket=playwright-office-ticket",
        expiresAt: 1785000060000,
      };
    }
    return route.fulfill({
      status,
      contentType: "application/json; charset=utf-8",
      body: JSON.stringify(body),
    });
  });
  await page.routeWebSocket("**/api/world/ws*", (socket) => {
    socket.send(JSON.stringify({ type: "welcome", id: "world-self", peers: [] }));
  });
  await page.routeWebSocket("**/api/world/office/general/ws*", (socket) => {
    meetingSocket = socket;
    socket.send(JSON.stringify({
      type: "welcome",
      id: "participant-self",
      participants: [REMOTE_PARTICIPANT],
    }));
    socket.onMessage((raw) => {
      const frame = JSON.parse(String(raw));
      meetingFrames.push(frame);
      if (frame.type !== "seat-request") return;
      socket.send(JSON.stringify({
        type: "seat",
        participant: {
          ...REMOTE_PARTICIPANT,
          id: "participant-self",
          name: "Guest 0001",
          pose: "seated",
          chairId: frame.chairId,
        },
      }));
    });
  });
  await page.routeWebSocket(
    "**/api/repo/mainnode/forkmesh/rooms/world-general/ws",
    (socket) => {
      chatSocket = socket;
      socket.onMessage((raw) => chatFrames.push(JSON.parse(String(raw))));
    },
  );

  await page.goto("/world/");
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return (
      Boolean(shell?.world?.renderer?.domElement) &&
      Number(shell.world.renderer.info?.render?.frame || 0) > 0
    );
  });
  return {
    chatFrames,
    meetingFrames,
    officeEntryRequests,
    generalAccessHeaders,
    sendChatFrame: (plain) => chatSocket.send(JSON.stringify(encryptChatEnvelope(plain))),
    closeMeeting: (code = 1008) => meetingSocket.close({
      code,
      reason: "room access revoked",
      wasClean: true,
    }),
  };
}

test("visitors join a native Office meeting and sit after server approval", async ({ page }) => {
  const { meetingFrames, officeEntryRequests, generalAccessHeaders } =
    await prepareMeetingWorld(page);
  await page.locator("forkmesh-world").evaluate((shell, position) => {
    shell.world.player.position.set(...position);
  }, OFFICE_ENTRANCE_POSITION);
  await expect(page.locator("[data-world-office-enter]")).toBeVisible();
  await page.keyboard.press("e");

  const lobby = page.locator("[data-world-office-lobby]");
  await expect(lobby).toBeVisible();
  await expect(page.locator(".world-quick-dock")).toBeHidden();
  await expect(page.locator("[data-world-office-frame]")).not.toHaveAttribute(
    "src",
    /.+/,
  );
  await lobby.getByRole("button", { name: "Join #general" }).click();
  expect(officeEntryRequests).toEqual([
    {
      method: "POST",
      url: expect.stringMatching(/\/api\/world\/office\/general\/entry$/),
      body: {},
    },
  ]);
  expect(generalAccessHeaders).toHaveLength(1);
  expect(generalAccessHeaders[0]["x-forkmesh-office-entry"]).toBe(
    OFFICE_ENTRY_TICKET,
  );

  const room = page.locator("[data-world-office-room]");
  await expect(room).toBeVisible();
  await expect(room).toContainText("#general");
  await expect(
    room.locator('[data-world-office-participant="participant-remote"]'),
  ).toContainText("Remote teammate");

  await page.keyboard.down("d");
  await page.waitForTimeout(180);
  await page.keyboard.up("d");
  await expect.poll(() => meetingFrames.some(
    (frame) => frame.type === "move" && frame.moving === true,
  )).toBe(true);

  await room.locator('[data-world-office-seat="chair-1"]').click();
  await expect(room.getByRole("button", { name: "Stand up" })).toBeVisible();
  await expect(room.locator('[data-world-office-seat="chair-1"]')).toHaveAttribute(
    "aria-pressed",
    "true",
  );
});

test("native Office messages, pasted images, and documents stay encrypted", async ({ page }) => {
  const { chatFrames, sendChatFrame } = await prepareMeetingWorld(page);
  await page.locator("forkmesh-world").evaluate((shell, position) => {
    shell.world.player.position.set(...position);
  }, OFFICE_ENTRANCE_POSITION);
  await expect(page.locator("[data-world-office-enter]")).toBeVisible();
  await page.keyboard.press("e");
  await page.getByRole("button", { name: "Join #general" }).click();

  const room = page.locator("[data-world-office-room]");
  const input = room.locator("[data-world-office-input]");
  await expect(input).toBeEnabled();
  await input.fill("A verified meeting message");
  await input.press("Enter");
  await expect(room.locator("[data-world-office-transcript]")).toContainText(
    "A verified meeting message",
  );
  await expect(page.locator('[data-world-office-bubble="participant-self"]'))
    .toContainText("A verified meeting message");

  sendChatFrame({
    type: "chat",
    id: "forged-remote-message",
    senderId: "forged-remote-sender",
    sender: "Remote teammate",
    channel: "#general",
    text: "Tampered avatar claim",
    ts: 1785000000000,
    meetingProof: {
      v: 1,
      participantId: "participant-remote",
      ts: 1785000000000,
      signature: "not-a-valid-signature",
    },
  });
  await expect(room.locator("[data-world-office-transcript]")).toContainText(
    "Tampered avatar claim",
  );
  await expect(room.locator("[data-world-office-transcript]")).toContainText(
    "Remote channel participant",
  );
  await expect(page.locator('[data-world-office-bubble="participant-remote"]'))
    .toHaveCount(0);

  await input.evaluate((element) => {
    const png = Uint8Array.from(atob(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=",
    ), (char) => char.charCodeAt(0));
    const transfer = new DataTransfer();
    transfer.items.add(new File([png], "office-paste.png", { type: "image/png" }));
    element.dispatchEvent(new ClipboardEvent("paste", {
      bubbles: true,
      cancelable: true,
      clipboardData: transfer,
    }));
  });
  await expect(room.locator(".world-office-transcript-image")).toHaveAttribute(
    "alt",
    "office-paste.png",
  );
  await room.locator("[data-world-office-attachment-input]").setInputFiles({
    name: "meeting-notes.pdf",
    mimeType: "application/pdf",
    buffer: Buffer.from("private meeting document"),
  });
  await expect(room.locator(".world-office-transcript-file")).toContainText(
    "meeting-notes.pdf",
  );

  await expect.poll(
    () => chatFrames.filter((frame) => frame.persist === true).length,
  ).toBe(3);
  const wire = JSON.stringify(chatFrames);
  expect(wire).not.toContain("A verified meeting message");
  expect(wire).not.toContain("office-paste.png");
  expect(wire).not.toContain("meeting-notes.pdf");
});

test("meeting revocation clears the room and returns to the lobby", async ({ page }) => {
  const { closeMeeting } = await prepareMeetingWorld(page);
  await page.locator("forkmesh-world").evaluate((shell, position) => {
    shell.world.player.position.set(...position);
  }, OFFICE_ENTRANCE_POSITION);
  await expect(page.locator("[data-world-office-enter]")).toBeVisible();
  await page.keyboard.press("e");
  await page.getByRole("button", { name: "Join #general" }).click();
  const room = page.locator("[data-world-office-room]");
  await expect(room).toBeVisible();
  await room.locator('[data-world-office-seat="chair-1"]').click();
  await expect(room.getByRole("button", { name: "Stand up" })).toBeVisible();

  closeMeeting();
  await expect(room).toBeHidden();
  await expect(page.locator("[data-world-office-lobby]")).toBeVisible();
  await expect(page.locator("[data-world-office-lobby-status]")).toContainText(
    "meeting ended",
  );
  await expect(room.locator("[data-world-office-participant]")).toHaveCount(0);
  await expect(page.locator("[data-world-office-bubble]")).toHaveCount(0);
});

for (const viewport of [
  { name: "desktop", options: { viewport: { width: 1440, height: 900 } } },
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
  {
    name: "narrow",
    options: {
      viewport: { width: 320, height: 568 },
      hasTouch: true,
      isMobile: true,
    },
  },
]) {
  test(`Office meeting visual acceptance ${viewport.name}`, async ({ browser }, testInfo) => {
    testInfo.snapshotSuffix = "linux";
    // The transcript renders local time. Pin the browser zone so snapshots
    // remain identical on developer machines and CI runners worldwide.
    const context = await browser.newContext({
      ...viewport.options,
      timezoneId: "UTC",
    });
    const page = await context.newPage();
    await prepareMeetingWorld(page);
    await page.locator("forkmesh-world").evaluate((shell, position) => {
      shell.world.player.position.set(...position);
    }, OFFICE_ENTRANCE_POSITION);
    await expect(page.locator("[data-world-office-enter]")).toBeVisible();
    await page.keyboard.press("e");
    await page.getByRole("button", { name: "Join #general" }).click();
    const room = page.locator("[data-world-office-room]");
    const input = room.locator("[data-world-office-input]");
    await expect(input).toBeEnabled();
    await input.fill("Design review starts in five minutes.");
    await input.press("Enter");
    await room.locator('[data-world-office-seat="chair-1"]').click();
    await expect(room.getByRole("button", { name: "Stand up" })).toBeVisible();
    await room.evaluate((element) => {
      element.scrollTop = element.scrollHeight;
    });
    await expect(room.locator("[data-world-office-input]")).toBeVisible();
    await expect(room.getByRole("button", { name: "Leave ForkMesh Office" }))
      .toBeVisible();
    await page.locator("forkmesh-world").evaluate((shell) => {
      shell.world.setPaused(true);
    });
    await page.evaluate(() => document.fonts?.ready);
    expect(await page.evaluate(
      () => document.documentElement.scrollWidth <= window.innerWidth,
    )).toBe(true);
    await expect(page).toHaveScreenshot(`office-meeting-${viewport.name}.png`, {
      animations: "disabled",
      caret: "hide",
      maxDiffPixelRatio: 0.01,
    });
    await context.close();
  });
}
