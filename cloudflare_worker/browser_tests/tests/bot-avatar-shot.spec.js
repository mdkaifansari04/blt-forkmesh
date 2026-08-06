// Temporary visual QA: spawn one robot per avatar model and photograph the
// fleet up close. Not part of the suite — delete after review.
const { test } = require("@playwright/test");

test("photograph agent bot avatar faces", async ({ page }) => {
  await page.setViewportSize({ width: 1600, height: 900 });
  await page.goto("/world?nointro=1");
  await page.waitForFunction(() => {
    const shell = document.querySelector("forkmesh-world");
    return (
      Boolean(shell?.world?.renderer?.domElement) &&
      Number(shell.world.renderer.info?.render?.frame || 0) > 0
    );
  });
  await page.locator("forkmesh-world").evaluate((shell) => {
    const roster = [
      { id: "shot-opus", provider: "claude-code", model: "opus", status: "running", title: "Opus run", localAgentId: 1 },
      { id: "shot-sonnet", provider: "claude-code", model: "claude-sonnet-5", status: "queued", title: "Sonnet run", localAgentId: 2 },
      { id: "shot-haiku", provider: "claude-code", model: "haiku", status: "stopped", title: "Haiku run", localAgentId: 3 },
      { id: "shot-fable", provider: "claude-code", model: "claude-fable-5", status: "running", title: "Fable run", localAgentId: 4 },
      { id: "shot-sol", provider: "codex", model: "", status: "running", title: "Codex run", localAgentId: 5 },
    ];
    shell.world.setAgentBotAccess(true);
    shell.world.updateMirrorAgentTasks(roster);
  });
  // Let the face textures finish decoding before the posed render.
  await page.waitForTimeout(2600);
  await page.locator("forkmesh-world").evaluate((shell) => {
    const ids = ["shot-opus", "shot-sonnet", "shot-haiku", "shot-fable", "shot-sol"];
    const bots = ids.map((id) => {
      const bot =
        shell.world.scene.getObjectByName(`claude-agent-droid-${id}`) ||
        shell.world.scene.getObjectByName(`codex-agent-droid-${id}`);
      if (!bot) throw new Error(`${id} bot missing`);
      return bot;
    });
    shell.world.setPaused(true);
    const anchor = bots[0].position.clone();
    bots.forEach((bot, index) => {
      bot.position.set(anchor.x + (index - 2) * 1.7, anchor.y, anchor.z);
      bot.rotation.y = 0;
    });
    const camera = shell.world.camera;
    camera.fov = 45;
    camera.position.set(anchor.x, anchor.y + 1.6, anchor.z + 7.4);
    camera.lookAt(anchor.x, anchor.y + 1.05, anchor.z);
    camera.updateProjectionMatrix();
    shell.world.renderer.render(shell.world.scene, camera);
  });
  await page.locator("canvas.world-canvas").screenshot({
    path: "test-results/bot-avatars-closeup.png",
    animations: "disabled",
  });
});
