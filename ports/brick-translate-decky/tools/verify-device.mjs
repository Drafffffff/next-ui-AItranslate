import {writeFile, mkdir} from "node:fs/promises";
import {evaluate} from "./cdp.mjs";

const runtime = process.env.BRICK_PLAYWRIGHT_MODULE;
if (!runtime) throw new Error("Set BRICK_PLAYWRIGHT_MODULE to your Playwright module path");
const {chromium} = await import(runtime);
const browser = await chromium.connectOverCDP(process.env.BRICK_CDP_ORIGIN ?? "http://127.0.0.1:18080");
const config = await evaluate('DeckyBackend.call("loader/call_plugin_method","Brick Translate","get_settings")');
try {
  await evaluate('DeckyBackend.call("loader/call_plugin_method","Brick Translate","save_settings",{hold_seconds:0})');
  const pages = browser.contexts()[0].pages();
  const titles = await Promise.all(pages.map(async page => ({page, title: await page.title()})));
  const qam = titles.find(value => value.title === "QuickAccess_uid2").page;
  const main = titles.find(value => value.title === "Steam 大屏幕模式").page;
  await evaluate('SteamUIStore.WindowStore.GamepadUIMainWindowInstance.MenuStore.OpenQuickAccessMenu(999)');
  // Explicitly discard Decky's retained React element after a plugin hot reload.
  await evaluate('DeckyPluginLoader.deckyState.closeActivePlugin()');
  const title = qam.getByText("Brick Translate", {exact: true});
  await title.click({timeout: 5000});
  await qam.getByText("翻译当前画面", {exact: false}).click({timeout: 5000});
  await main.waitForFunction(() => {
    const canvas = document.querySelector("#brick-translate-overlay canvas");
    if (!canvas) return false;
    const pixels = canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height).data;
    for (let i = 3; i < pixels.length; i += 4) if (pixels[i]) return true;
    return false;
  }, undefined, {timeout: 95000, polling: 1000});
  const metrics = await main.evaluate(() => {
    const root = document.querySelector("#brick-translate-overlay");
    const canvas = root.querySelector("canvas"), image = root.querySelector("img");
    const rect = canvas.getBoundingClientRect(), imageRect = image.getBoundingClientRect();
    const data = canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height).data;
    let coloredPixels = 0;
    for (let i = 3; i < data.length; i += 4) if (data[i]) coloredPixels++;
    return {width: canvas.width, height: canvas.height, coloredPixels,
      canvasRect: {x: rect.x, y: rect.y, width: rect.width, height: rect.height},
      imageRect: {x: imageRect.x, y: imageRect.y, width: imageRect.width, height: imageRect.height},
      composition: root.innerText};
  });
  await mkdir("out", {recursive: true});
  await main.screenshot({path: "out/device-overlay.png"});
  const basePlane = await evaluate('DeckyBackend.call("loader/call_plugin_method","Brick Translate","capture")');
  await writeFile("out/device-base-plane.png", Buffer.from(basePlane.image.split(",")[1], "base64"));
  await writeFile("out/device-metrics.json", JSON.stringify(metrics, null, 2));
  console.log(JSON.stringify(metrics));
} finally {
  await evaluate(`DeckyBackend.call("loader/call_plugin_method","Brick Translate","save_settings",{hold_seconds:${config.hold_seconds}})`);
  await browser.close();
}
