import {evaluate} from "./cdp.mjs";

const {chromium} = await import(process.env.BRICK_PLAYWRIGHT_MODULE);
const browser = await chromium.connectOverCDP("http://127.0.0.1:18080");
const call = (method, ...args) => evaluate(`DeckyBackend.call("loader/call_plugin_method","Brick Translate",${JSON.stringify(method)},${args.map(value => JSON.stringify(value)).join(",")})`);
const original = await call("get_settings");
try {
  const pages = await Promise.all(browser.contexts()[0].pages().map(async page => ({page, title: await page.title()})));
  const qam = pages.find(value => value.title === "QuickAccess_uid2").page;
  const main = pages.find(value => value.title === "Steam 大屏幕模式").page;
  await evaluate('DeckyPluginLoader.deckyState.closeActivePlugin();SteamUIStore.WindowStore.GamepadUIMainWindowInstance.MenuStore.OpenQuickAccessMenu(999)');
  await qam.getByText("Brick Translate", {exact: true}).click();
  const toggle = qam.getByRole("checkbox", {name: "OCR 定位", exact: true});
  const before = (await call("get_settings")).pipeline;
  await toggle.click();
  const changed = await call("get_settings");
  if (changed.pipeline === before) throw new Error("OCR toggle was not persisted");
  if (changed.pipeline === "vision" && await qam.getByText("OCR 模型", {exact: true}).count()) throw new Error("OCR controls remain visible");
  if (changed.pipeline !== "vision") await toggle.click();
  await call("save_settings", {hold_seconds: 0});
  await qam.getByText("翻译当前画面", {exact: false}).click();
  let ready = false;
  const deadline = Date.now() + 60000;
  while (Date.now() < deadline && !ready) {
    ready = await main.evaluate(() => {
      const root = document.querySelector("#brick-translate-overlay");
      return !!root?.querySelector("canvas") && !root.querySelector('[role="status"]');
    });
    if (!ready) await new Promise(resolve => setTimeout(resolve, 500));
  }
  if (!ready) throw new Error("No completed overlay to test");
  // DOM-only key events test the overlay handler without injecting an OS/game action.
  await main.evaluate(() => window.dispatchEvent(new KeyboardEvent("keydown", {key: "F13", code: "F13", repeat: true})));
  if (!await main.locator("#brick-translate-overlay").count()) throw new Error("Repeat incorrectly dismissed overlay");
  await main.evaluate(() => window.dispatchEvent(new KeyboardEvent("keydown", {key: "F13", code: "F13", repeat: false})));
  await main.locator("#brick-translate-overlay").waitFor({state: "detached", timeout: 5000});
  console.log(JSON.stringify({ocrTogglePersisted: true, directTranslation: true, repeatIgnored: true, newKeyDismissed: true}));
} finally {
  await call("arm_dismiss", false);
  await call("save_settings", {pipeline: original.pipeline, hold_seconds: original.hold_seconds});
  await evaluate('SteamUIStore.WindowStore.GamepadUIMainWindowInstance.MenuStore.CloseSideMenus()');
  await browser.close();
}
