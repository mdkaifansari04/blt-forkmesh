const { test } = require("@playwright/test");
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

test("_tmp logo angles", async ({ page }) => {
  test.setTimeout(120_000);
  await page.route(THREE_MODULE_URL, async (route) => {
    await route.fulfill({
      path: THREE_MODULE_PATH,
      contentType: "text/javascript",
    });
  });
  await page.goto("/world/");
  await page.waitForFunction(() =>
    Boolean(document.querySelector("forkmesh-world")?.world?.scene)
  );
  await page.locator("forkmesh-world").evaluate((shell) => {
    shell.world.enterOfficeLobby();
    shell.world.setPaused(false);
  });
  await page.waitForTimeout(1400);
  const views = [
    ["p", [-15, 15, 1], 0, 72],
    ["q", [-15, 15, 1], 0.7, 72],
    ["r", [-15, 15, 1], 1.05, 72],
    ["s", [-15, 15, 1], 1.35, 72],
    ["t", [-15, 15, 1], Math.PI / 2, 72],
    ["u", [-15, 15, 1], 1.8, 72],
    ["v", [-15, 15, 1], 2.1, 72],
  ];
  for (const [name, position, yaw, fov] of views) {
    await page.locator("forkmesh-world").evaluate(
      (shell, spec) => {
        const scene = shell.world.scene;
        const interior = scene.getObjectByName("forkmesh-office-interior");
        const cube = scene.getObjectByName("forkmesh-reflective-fm-cube");
        shell.world.setPaused(true);
        cube.rotation.y = spec.yaw;
        shell.world.camera.fov = spec.fov;
        shell.world.camera.updateProjectionMatrix();
        shell.world.camera.position.copy(interior.localToWorld(
          shell.world.camera.position.clone().set(...spec.position)
        ));
        shell.world.camera.lookAt(interior.localToWorld(
          shell.world.camera.position.clone().set(-18, 7.5, -2)
        ));
        shell.world.renderer.render(scene, shell.world.camera);
      },
      { position, yaw, fov },
    );
    await page.locator("canvas.world-canvas").screenshot({
      path: `/tmp/forkmesh-logo-angle-${name}.png`,
      animations: "disabled",
    });
  }
});
