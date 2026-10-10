import { native, read, write, prepare, request } from "./services.ts";
import { reportAppAction } from "@pocketjs/framework/host";
type App = { send(text: string): Promise<void>; activate(index: number): Promise<void>; home(): void; showText(title: string, text: string): Promise<void>; ready(): boolean; busy(): boolean; position(): number; screen(): string; status(): string; draft(): string; messages(): number };
export function installAcceptance(app: App) {
  if (!native.testing) return;
  let stage = 0, failure = "";
  (globalThis as Record<string, unknown>).__brickAcceptance = (op: number) => {
    if (op === 11) { void app.activate(2); return 1; }
    if (op === 12) { void app.activate(10); return 1; }
    if (op === 13) return app.draft().length;
    if (op === 10) throw new Error(failure);
    if (op === 0) return app.ready() ? 1 : 0;
    if (op === 1) return stage;
    if (op === 2) { void run(); return 1; }
    if (op === 3) return app.busy() ? 1 : 0;
    if (op === 4) return app.messages();
    if (op === 5) { app.home(); return 1; }
    if (op === 6) return app.position();
    if (op === 8) { void request(native.testUrl! + "/slow").promise.catch(() => {}); return 1; }
    if (op === 9) return app.screen() === "menu" ? 1 : 0;
    if (op === 7) { void app.send("A TEST QUESTION"); return 1; }
    return failure ? -1 : 0;
  };
  async function run() {
    const check = (condition: unknown, name: string) => { if (!condition) throw new Error(name); };
    try {
      await write("prompt.txt", "麒麟翡翠龘龟：运行时中文，不在应用界面字库里。");
      const text = await read("prompt.txt"); check(text.includes("麒麟"), "file roundtrip");
      const lines = await prepare(text); check(lines.length > 0, "dynamic glyphs");
      const url = native.testUrl!;
      const response = await request(url + "/ok", "", "").promise;
      check(response.status === 200 && response.text.includes("麒麟"), "HTTP body");
      const httpError = await request(url + "/unauthorized").promise;
      check(httpError.status === 401, "HTTP error status");
      let rejected = false;
      try { await request(url + "/slow", "", "", 150).promise; } catch { rejected = true; }
      check(rejected, "timeout");
      rejected = false;
      try { await request(url + "/large").promise; } catch { rejected = true; }
      check(rejected, "bounded response");
      rejected = false;
      try { await request(native.testTlsUrl!).promise; } catch (e) { rejected = String(e).includes("证书"); }
      check(rejected, "TLS verification");
      const pending = request(url + "/slow", "", "", 2000); pending.cancel();
      rejected = false; try { await pending.promise; } catch { rejected = true; }
      check(rejected, "cancellation");
      let invalid = false; try { await read("../outside"); } catch { invalid = true; } check(invalid, "file boundary");
      await app.send("A TEST QUESTION");
      check(app.messages() === 2 && app.screen() === "read", "chat completion: " + app.status());
      check(JSON.parse(await read("history.json")).length === 2, "history persisted");
      await app.showText("动态中文验证", "麒麟翡翠龘龟：动态字形已经生成。\n\n" + text.repeat(20));
      stage = 1; reportAppAction("acceptance.done", 1);
    } catch (e) { failure = String(e); stage = -1; reportAppAction("acceptance.failed", 1); throw e; }
  }
}
