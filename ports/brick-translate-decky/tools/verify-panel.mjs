import {mkdir, writeFile} from "node:fs/promises";
import {evaluate} from "./cdp.mjs";

const {chromium} = await import(process.env.BRICK_PLAYWRIGHT_MODULE);
const browser = await chromium.connectOverCDP("http://127.0.0.1:18080");
const call = (method, ...args) => evaluate(`DeckyBackend.call("loader/call_plugin_method","Brick Translate",${JSON.stringify(method)},${args.map(value => JSON.stringify(value)).join(",")})`);
const before = await call("get_settings");
try {
  const pages = await Promise.all(browser.contexts()[0].pages().map(async page => ({page, title: await page.title()})));
  const qam = pages.find(value => value.title === "QuickAccess_uid2").page;
  await evaluate('DeckyPluginLoader.deckyState.closeActivePlugin();SteamUIStore.WindowStore.GamepadUIMainWindowInstance.MenuStore.OpenQuickAccessMenu(999)');
  await qam.getByText("Brick Translate", {exact: true}).click();
  const root = qam.locator("#brick-translate-panel");
  await root.getByText("已就绪", {exact: true}).first().waitFor();
  await mkdir("out", {recursive: true});
  const metrics = [];
  for (const width of [400, 320]) {
    await root.evaluate((node, size) => {node.style.width = `${size}px`;}, width);
    for (const [id, label] of [["translate", "翻译"], ["display", "显示"], ["services", "服务"], ["history", "记录"]]) {
      await qam.getByRole("tab", {name: label, exact: true}).click();
      const metric = await root.evaluate(node => {
        const rect = node.getBoundingClientRect();
        const outside = Array.from(node.querySelectorAll(".bt-tab,.bt-command,.bt-status,.bt-facts,.bt-config-status,button,input")).filter(child => {
          const box = child.getBoundingClientRect();
          return box.width > 0 && (box.left < rect.left - 2 || box.right > rect.right + 2);
        }).map(child => ({tag: child.tagName, className: child.className}));
        return {width: rect.width, height: rect.height, outside};
      });
      if (metric.outside.length) throw new Error(`Panel overflows at ${width}px: ${JSON.stringify(metric.outside)}`);
      await root.screenshot({path: `out/panel-${id}-${width}.png`});
      metrics.push({tab: id, ...metric});
    }
  }
  await qam.getByRole("tab", {name: "服务", exact: true}).click();
  await root.getByRole("button", {name: "接口与模型", exact: true}).click();
  const model = root.locator(".bt-section-row").filter({has: qam.getByText("翻译模型", {exact: true})}).locator("input");
  await model.fill(before.model + "-ui-test");
  await root.getByText("有未保存的更改", {exact: true}).waitFor();
  await qam.getByRole("tab", {name: "翻译", exact: true}).click();
  await root.getByRole("checkbox", {name: "OCR 定位", exact: true}).click();
  const toggled = await call("get_settings");
  if (toggled.pipeline === before.pipeline || toggled.model !== before.model) throw new Error("OCR toggle lost draft isolation");
  await qam.getByRole("tab", {name: "服务", exact: true}).click();
  if (await model.inputValue() !== before.model + "-ui-test") throw new Error("Changing tabs lost model draft");
  await root.getByRole("button", {name: "撤销更改", exact: true}).click();
  await root.getByText("有未保存的更改", {exact: true}).waitFor({state: "detached"});
  if (await model.inputValue() !== before.model) throw new Error("Discard did not restore saved model");
  await qam.getByRole("tab", {name: "翻译", exact: true}).click();
  await root.getByRole("checkbox", {name: "OCR 定位", exact: true}).click();
  const services = qam.getByRole("tab", {name: "服务", exact: true});
  await services.focus(); await services.press("ArrowLeft");
  await qam.getByRole("tabpanel", {name: "显示", exact: true}).waitFor();
  if (await qam.getByRole("tab", {name: "显示", exact: true}).getAttribute("aria-selected") !== "true") throw new Error("Keyboard tab navigation failed");
  const after = await call("get_settings");
  for (const key of ["model", "endpoint", "pipeline", "ocr_model", "ocr_endpoint", "font_scale", "hold_seconds"])
    if (after[key] !== before[key]) throw new Error(`Verification changed ${key}`);
  await root.evaluate(node => {node.style.removeProperty("width");});
  await qam.getByRole("tab", {name: "翻译", exact: true}).click();
  await root.screenshot({path: "out/panel-final.png"});
  await writeFile("out/panel-metrics.json", JSON.stringify({metrics, draftPreserved: true, toggleIsolated: true, keyboardTabs: true}, null, 2));
  console.log(JSON.stringify({views: metrics.length, widths: [320, 400], draftPreserved: true, toggleIsolated: true, keyboardTabs: true}));
} finally {
  const after = await call("get_settings");
  if (after.pipeline !== before.pipeline) await call("save_settings", {pipeline: before.pipeline});
  await browser.close();
}
