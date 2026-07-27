const { defineConfig } = require("@playwright/test");
const path = require("node:path");

module.exports = defineConfig({
  testDir: path.join(__dirname, "tests"),
  fullyParallel: false,
  forbidOnly: Boolean(process.env.CI),
  retries: process.env.CI ? 1 : 0,
  workers: 1,
  reporter: process.env.CI ? [["line"]] : [["list"]],
  timeout: 30_000,
  expect: { timeout: 8_000 },
  use: {
    baseURL: "http://127.0.0.1:4179",
    browserName: "chromium",
    headless: true,
    serviceWorkers: "block",
  },
  webServer: {
    command: "bash run-browser-worker.sh",
    cwd: __dirname,
    url: "http://127.0.0.1:4179/world/",
    reuseExistingServer: !process.env.CI,
    timeout: 120_000,
  },
});
