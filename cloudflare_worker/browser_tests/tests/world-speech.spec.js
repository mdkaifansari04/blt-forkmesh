const { test, expect } = require("@playwright/test");

const LOCAL_PORT = 45678;
const LOCAL_ORIGIN = `http://127.0.0.1:${LOCAL_PORT}`;
const PAGE_ORIGIN = "http://127.0.0.1:4179";

async function mockLocalBridge(page, records) {
  let poll = 0;
  await page.addInitScript(() => {
    globalThis.__forkmeshMediaCaptureCalls = 0;
    if (navigator.mediaDevices?.getUserMedia) {
      const original = navigator.mediaDevices.getUserMedia.bind(
        navigator.mediaDevices,
      );
      navigator.mediaDevices.getUserMedia = (...args) => {
        globalThis.__forkmeshMediaCaptureCalls += 1;
        return original(...args);
      };
    }
  });
  await page.route(`${LOCAL_ORIGIN}/**`, async (route) => {
    const request = route.request();
    records.push({
      method: request.method(),
      url: request.url(),
      headers: request.headers(),
      body: request.postData() || "",
    });
    const cors = {
      "Access-Control-Allow-Origin": PAGE_ORIGIN,
      "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
      "Access-Control-Allow-Headers":
        "Authorization, Content-Type, X-ForkMesh-Request-Id",
      "Access-Control-Allow-Private-Network": "true",
      "Cache-Control": "no-store",
    };
    if (request.method() === "OPTIONS") {
      await route.fulfill({ status: 204, headers: cors, body: "" });
      return;
    }
    const path = new URL(request.url()).pathname;
    let status = 200;
    let body = {};
    if (path === "/v1/pair") {
      body = {
        sessionToken: "session-capability-only-in-memory",
        expiresAt: new Date(Date.now() + 300_000).toISOString(),
        destinations: ["chat", "issue", "note"],
        audioTransport: "none-local-capture-only",
      };
    } else if (path === "/v1/transcription/start") {
      status = 202;
      body = {
        state: "recording",
        captureId: "capture-voice-1",
        destination: "issue",
        audioRelayed: false,
      };
    } else if (path === "/v1/transcription/stop") {
      status = 202;
      body = { state: "transcribing", captureId: "capture-voice-1" };
    } else if (path === "/v1/transcription/cancel") {
      status = 202;
      body = { state: "cancelled", captureId: "capture-voice-1" };
    } else if (path === "/v1/session/revoke") {
      body = { state: "revoked" };
    } else if (path === "/v1/transcription") {
      poll += 1;
      body =
        poll < 2
          ? {
              state: "recording",
              captureId: "capture-voice-1",
              destination: "issue",
              transcript: "partial local words",
              error: "",
              revision: "1",
              audioRelayed: false,
            }
          : {
              state: "complete",
              captureId: "capture-voice-1",
              destination: "issue",
              transcript: "final local transcript",
              error: "",
              revision: "2",
              audioRelayed: false,
            };
    } else {
      status = 404;
      body = { error: "not_found" };
    }
    await route.fulfill({
      status,
      headers: { ...cors, "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
  });
}

test("Qt voice bridge inserts text only into the chosen composer without relaying audio", async ({
  page,
}) => {
  const records = [];
  await mockLocalBridge(page, records);
  await page.goto("/world/");

  const launcher = page.getByRole("button", { name: "Open local voice input" });
  await expect(launcher).toBeVisible();
  await launcher.click();
  const panel = page.getByRole("region", { name: "Local voice input" });
  await expect(panel).toBeVisible();
  await expect(panel.getByRole("button", { name: "Start local mic" })).toBeDisabled();

  await panel.getByLabel("Local port").fill(String(LOCAL_PORT));
  await panel.getByLabel("One-time pairing code").fill("one-use-pair-secret");
  await panel.getByRole("button", { name: "Pair desktop" }).click();
  await expect(panel.getByRole("status")).toContainText("Paired");

  await panel.getByRole("tab", { name: "Issue" }).click();
  const issueComposer = panel.getByLabel("Issue composer");
  await expect(issueComposer).toBeVisible();
  await panel.getByRole("button", { name: "Start local mic" }).click();
  await expect(panel.getByRole("status")).toContainText(
    "Transcript ready",
    { timeout: 5000 },
  );
  await expect(issueComposer).toHaveValue("final local transcript");
  await expect(panel.getByLabel("Chat composer")).toHaveValue("");
  await expect(panel.getByLabel("Note composer")).toHaveValue("");

  expect(await page.evaluate(() => globalThis.__forkmeshMediaCaptureCalls)).toBe(0);
  const persisted = await page.evaluate(() => ({
    local: JSON.stringify(localStorage),
    session: JSON.stringify(sessionStorage),
  }));
  expect(`${persisted.local}${persisted.session}`).not.toContain(
    "one-use-pair-secret",
  );
  expect(`${persisted.local}${persisted.session}`).not.toContain(
    "session-capability-only-in-memory",
  );

  const pair = records.find(
    (record) =>
      record.method === "POST" &&
      new URL(record.url).pathname === "/v1/pair",
  );
  expect(pair).toBeTruthy();
  expect(pair.url).not.toContain("one-use-pair-secret");
  expect(pair.headers.authorization).toBe("Bearer one-use-pair-secret");
  const start = records.find(
    (record) =>
      record.method === "POST" &&
      new URL(record.url).pathname === "/v1/transcription/start",
  );
  expect(start.headers.authorization).toBe(
    "Bearer session-capability-only-in-memory",
  );
  expect(start.headers["x-forkmesh-request-id"]).toBeTruthy();
  expect(JSON.parse(start.body)).toEqual({ destination: "issue" });
  expect(records.map((record) => record.url).join(" ")).not.toMatch(
    /audio|microphone|secret/i,
  );
});

test("cancel is explicit and clears the unsubmitted selected draft", async ({
  page,
}) => {
  const records = [];
  await mockLocalBridge(page, records);
  await page.goto("/world/");
  await page.getByRole("button", { name: "Open local voice input" }).click();
  const panel = page.getByRole("region", { name: "Local voice input" });
  await panel.getByLabel("Local port").fill(String(LOCAL_PORT));
  await panel.getByLabel("One-time pairing code").fill("cancel-pair-secret");
  await panel.getByRole("button", { name: "Pair desktop" }).click();
  await panel.getByRole("tab", { name: "Chat" }).click();
  await panel.getByRole("button", { name: "Start local mic" }).click();
  await expect(panel.getByRole("button", { name: "Cancel" })).toBeEnabled();
  await panel.getByRole("button", { name: "Cancel" }).click();
  await expect(panel.getByRole("status")).toContainText("cancelled");
  await expect(panel.getByLabel("Chat composer")).toHaveValue("");
  expect(
    records.some(
      (record) =>
        record.method === "POST" &&
        new URL(record.url).pathname === "/v1/transcription/cancel",
    ),
  ).toBe(true);
});
