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
    ["w", [-10, 13, 6], 0, 50],
    ["x", [-8, 14, 8], 0, 45],
    ["y", [-12, 12, 4], 0, 55],
    ["z", [-10, 14, -10], 0, 48],
    ["aa", [-26, 13, 6], 0, 50],
    ["ab", [-10, 11, 6], 0, 50],
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
          shell.world.camera.position.clone().set(-18, 5.3, -2)
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
