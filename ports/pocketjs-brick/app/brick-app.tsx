import { createSignal, For, Show } from "solid-js";
import { mount } from "@pocketjs/framework/solid";
import { Text, View, FocusGrid, Focusable, FocusScope } from "@pocketjs/framework/solid/components";
import { VirtualList, type VirtualListHandle } from "@pocketjs/framework/virtual-list";
import { onFrame, onButtonPress } from "@pocketjs/framework/solid/lifecycle";
import { BTN } from "@pocketjs/framework/input";
import { reportAppAction } from "@pocketjs/framework/host";
import { installServices, read, write, prepare, request, native, hasSharedCredential } from "./services.ts";
import { installAcceptance } from "./acceptance.ts";

type Message = { role: "user" | "assistant"; content: string };
type Config = { version: number; key: string; baseUrl: string; model: string; timeoutMs: number; savedCount: number };
const defaults: Config = { version: 1, key: "", baseUrl: "https://api.deepseek.com", model: "deepseek-flash", timeoutMs: 30000, savedCount: 0 };
const entries = ["聊一聊游戏", "接着上一段聊", "输入英文问题", "发送 prompt.txt", "阅读最近回复", "阅读聊天记录", "测试网络连接", "保存设置", "取消当前请求", "阅读 prompt.txt", "清空聊天记录", "使用说明"];
const letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,?!-".split("").concat(["删除", "发送", "返回"]);
const help = "在 SD 卡的 .userdata/shared/ai-keys.txt 填入 DEEPSEEK_API_KEY，翻译和聊天共用。旧 config.json 的 key 仍兼容。可在 prompt.txt 写入任意中文问题，或使用英文屏幕键盘。\n\n上下移动，A 选择；X 返回菜单或取消请求，B 返回上一页，MENU 退出。请求期间仍可浏览，取消后不会把未完成回复加入记录。\n\n聊天记录自动保存。完整中文字体使用卡上的 .system/res/font1.ttf。当前不支持中文输入法，表情和超出字体范围的字符可能需要替换。";

function App() {
  installServices();
  const [screen, setScreen] = createSignal<"menu" | "read" | "keyboard" | "confirm">("menu");
  const [status, setStatus] = createSignal("正在读取设置…");
  const [ready, setReady] = createSignal(false);
  const [busy, setBusy] = createSignal(false);
  const [position, setPosition] = createSignal(1);
  const [lines, setLines] = createSignal<string[]>([]);
  const [draft, setDraft] = createSignal("");
  const [readTitle, setReadTitle] = createSignal("阅读");
  let config = { ...defaults }, messages: Message[] = [];
  let list: VirtualListHandle | undefined;
  let currentRequest: ReturnType<typeof request> | undefined;
  let generation = 0, heldDirection = 0, heldFrames = 0, lastState = -1;
  const error = (e: unknown) => { setStatus(e instanceof Error ? e.message : "操作失败"); reportAppAction("app.error", 1); };
  const home = () => { setScreen("menu"); setPosition(1); list = undefined; heldFrames = 0; heldDirection = 0; };
  const showText = async (title: string, text: string) => {
    const rows = await prepare(text); setReadTitle(title); list = undefined; setLines(rows); setPosition(1); setScreen("read");
    reportAppAction("font.ready", rows.length);
  };
  const cancel = () => {
    generation++; currentRequest?.cancel(); currentRequest = undefined; setBusy(false); setStatus("请求已取消"); reportAppAction("chat.cancel", 1);
  };
  const historyText = () => messages.map(m => (m.role === "user" ? "我：" : "AI：") + m.content).join("\n\n");
  const send = async (text: string) => {
    if (!ready() || busy()) return;
    text = text.trim(); if (!text) { setStatus("请先输入问题"); return; }
    const mine = ++generation;
    setBusy(true); setStatus("正在请求，可以继续浏览…");
    try {
      if (!config.key && !await hasSharedCredential(config.baseUrl)) { setStatus("请在 ai-keys.txt 填入 DEEPSEEK_API_KEY"); return; }
      if (mine !== generation) return;
      const context = messages.slice(-8);
      while (context.length && context.reduce((n, m) => n + m.content.length, 0) > 6000) context.shift();
      currentRequest = request(config.baseUrl.replace(/\/$/, "") + "/chat/completions", config.key, JSON.stringify({
        model: config.model, messages: [{ role: "system", content: "请用简体中文回答。适合掌机阅读，优先短段落；不要使用 Markdown 表格。" }, ...context, { role: "user", content: text.slice(0, 4000) }],
        max_tokens: 1500, thinking: { type: "disabled" }, stream: false,
      }), config.timeoutMs);
      const response = await currentRequest.promise;
      if (mine !== generation) return;
      if (response.status < 200 || response.status >= 300) throw new Error(response.status === 401 ? "Key 无效，请检查设置" : response.status === 402 ? "余额不足，请检查账户" : response.status === 429 ? "请求限流，请稍后重试" : `服务返回 HTTP ${response.status}`);
      let value;
      try { value = JSON.parse(response.text); } catch { throw new Error("服务返回格式异常"); }
      const answer = value?.choices?.[0]?.message?.content;
      if (typeof answer !== "string" || !answer.trim()) throw new Error("服务没有返回可读内容");
      const content = answer.slice(0, 12000);
      if (mine !== generation) return;
      const updated: Message[] = [...messages, { role: "user", content: text.slice(0, 4000) }, { role: "assistant", content }];
      while (updated.length > 2 && JSON.stringify(updated).length > 22000) updated.splice(0, 2);
      let saved = true;
      try { await write("history.json", JSON.stringify(updated)); } catch { saved = false; }
      if (mine !== generation) return;
      messages = updated;
      const rows = await prepare(content);
      if (mine !== generation) return;
      setReadTitle("AI 回复"); list = undefined; setLines(rows); setPosition(1); setScreen("read");
      setStatus(saved ? (value.choices[0].finish_reason === "length" ? "回复已保存，达到长度上限" : "回复已保存") : "回复已收到，保存失败");
      reportAppAction("chat.ready", messages.length);
    } catch (e) { if (mine === generation) error(e); }
    finally { if (mine === generation) { currentRequest = undefined; setBusy(false); } }
  };
  const activate = async (index: number) => {
    reportAppAction("list.activate", index);
    if (!ready() && index !== 11) return;
    try {
      switch (index) {
        case 0: await send("请介绍一下你自己，并说说你能怎样帮我理解游戏剧情。"); break;
        case 1: await send("请接着刚才的话题继续解释，用简短的段落回答。"); break;
        case 2: if (!busy()) { list = undefined; setScreen("keyboard"); } break;
        case 3: await send(await read("prompt.txt")); break;
        case 4: await showText("最近回复", messages.filter(m => m.role === "assistant").at(-1)?.content ?? "还没有回复。先选择一个问题发送。"); break;
        case 5: await showText("聊天记录", historyText() || "还没有聊天记录。"); break;
        case 6: {
          if (busy()) { setStatus("已有请求，请等待或取消"); break; }
          const mine = ++generation; setBusy(true); setStatus("正在检测连接…");
          try {
            currentRequest = request(config.baseUrl.replace(/\/$/, "") + "/models", config.key, "", config.timeoutMs);
            const response = await currentRequest.promise;
            if (mine === generation) { setStatus(response.status === 401 ? "网络可达，请填写有效 Key" : `连接完成 HTTP ${response.status}`); reportAppAction("network.status", response.status); }
          } finally { if (mine === generation) { currentRequest = undefined; setBusy(false); } }
          break;
        }
        case 7: {
          const updated = { ...config, savedCount: config.savedCount + 1 };
          await write("config.json", JSON.stringify(updated, null, 2)); config = updated;
          setStatus(`设置已保存 ${config.savedCount} 次`); reportAppAction("config.saved", config.savedCount); break;
        }
        case 8: cancel(); break;
        case 9: await showText("本地文本", await read("prompt.txt") || "prompt.txt 为空。可以在电脑上写入中文问题。"); break;
        case 10: if (busy()) setStatus("请先取消当前请求"); else { list = undefined; setScreen("confirm"); } break;
        case 11: await showText("使用说明", help); break;
      }
    } catch (e) { error(e); }
  };
  onFrame((buttons) => {
    if (!list || (screen() !== "menu" && screen() !== "read")) return;
    const direction = buttons & BTN.DOWN ? 1 : buttons & BTN.UP ? -1 : 0;
    if (screen() === "menu") {
      if (direction !== heldDirection) { heldDirection = direction; heldFrames = 0; }
      else if (direction && ++heldFrames >= 24 && (heldFrames - 24) % 6 === 0) list.focusRow((list.focusedIndex() ?? 0) + direction);
      const index = list.focusedIndex() ?? 0; setPosition(index + 1);
      const state = index * 10000 + Math.round(list.scroller.offset());
      if (state !== lastState) { lastState = state; reportAppAction("list.state", state); }
    } else setPosition(Math.min(lines().length, Math.floor(list.scroller.offset() / 56) + 1));
  });
  onButtonPress(BTN.CROSS | BTN.TRIANGLE, (pressed) => {
    if ((pressed & BTN.TRIANGLE) && busy()) { cancel(); home(); }
    else if (screen() !== "menu") home();
    else if (busy()) cancel();
    else reportAppAction("app.exit", 1);
  });
  void (async () => {
    try {
      const stored = await read("config.json");
      if (stored) {
        const parsed = JSON.parse(stored);
        if (typeof parsed.key !== "string" || parsed.key.length >= 512 || /[\r\n]/.test(parsed.key) || typeof parsed.baseUrl !== "string" || !/^https:\/\//.test(parsed.baseUrl) && !(native.testing && /^http:\/\/127\.0\.0\.1:/.test(parsed.baseUrl))) throw new Error("config.json 格式无效");
        config = { ...defaults, ...parsed };
        if (!Number.isInteger(config.timeoutMs) || config.timeoutMs < 100 || config.timeoutMs > 120000 || typeof config.model !== "string" || config.model.length > 128) throw new Error("设置参数超出范围");
        if (!Number.isInteger(config.savedCount) || config.savedCount < 0) config.savedCount = 0;
      } else await write("config.json", JSON.stringify(config, null, 2));
      const saved = await read("history.json");
      if (saved) {
        const parsed = JSON.parse(saved);
        if (!Array.isArray(parsed) || parsed.length > 100 || parsed.some(m => !m || !["user", "assistant"].includes(m.role) || typeof m.content !== "string")) throw new Error("聊天记录格式无效");
        messages = parsed;
      }
      const available = config.key || await hasSharedCredential(config.baseUrl);
      setReady(true); setStatus(available ? "准备就绪" : "先在 ai-keys.txt 填入 Key"); reportAppAction("app.ready", messages.length);
    } catch (e) { error(e); }
  })();
  installAcceptance({ send, activate, home, showText, ready, busy, position, screen, status, draft, messages: () => messages.length });
  return (
    <View class="flex-col w-full h-full bg-[#151c18] p-[40] gap-[20]">
      <View class="flex-row items-center justify-between h-[78]">
        <Text class="text-5xl font-bold text-[#f3f1de]">口袋聊天</Text>
        <Text class="text-2xl text-[#c1cbbf]">DeepSeek · Brick</Text>
      </View>
      <View class="flex-row items-center justify-between h-[40]">
        <Text class="text-2xl text-[#c1cbbf]">{status().slice(0, 27)}</Text>
        <Text class="text-2xl text-[#c1cbbf]">{position()} / {screen() === "read" ? lines().length : entries.length}</Text>
      </View>
      <Show when={screen() === "menu"}>
        <VirtualList count={entries.length} rowHeight={80} height={400} overscan={80}
          ref={h => { list = h; h.focusRow(0); }} onRowPress={index => void activate(index)}
          renderRow={index => <View class={position() === index + 1 ? "flex-row items-center w-full h-[80] px-[24] gap-[24] bg-[#344d2e]" : "flex-row items-center w-full h-[80] px-[24] gap-[24] bg-[#151c18]"}>
            <Text class="text-2xl text-[#c1cbbf]">{index + 1}.</Text><Text class="text-4xl text-[#f3f1de]">{entries[index]}</Text>
          </View>} />
      </Show>
      <Show when={screen() === "read" && lines()} keyed>{rows =>
        <VirtualList count={rows.length} rowHeight={56} height={400} overscan={56} focusRows={false}
          ref={h => { list = h; }} renderRow={index => <Text class="text-4xl text-[#f3f1de]" style={{ fontSlot: 20, height: 56 }}>{rows[index]}</Text>} />
      }</Show>
      <Show when={screen() === "keyboard"}>
        <View class="flex-col h-[400] gap-[16]">
          <Text class="text-2xl text-[#f3f1de]">{draft().slice(-45) || "输入问题（英文 / 数字）"}</Text>
          <FocusScope autoFocus><FocusGrid columns={9} class="flex-row flex-wrap gap-[8]">
            <For each={letters}>{letter => <Focusable class="w-[96] h-[54] items-center justify-center bg-[#233322] focus:bg-[#49643a]"
              onPress={() => { if (letter === "返回") home(); else if (letter === "删除") setDraft(draft().slice(0, -1)); else if (letter === "发送") { home(); void send(draft()); } else if (draft().length < 4000) setDraft(draft() + letter); }}>
              <Text class="text-2xl text-[#f3f1de]">{letter === " " ? "空格" : letter}</Text>
            </Focusable>}</For>
          </FocusGrid></FocusScope>
        </View>
      </Show>
      <Show when={screen() === "confirm"}>
        <FocusScope autoFocus><View class="flex-col h-[400] gap-[32]">
          <Text class="text-4xl text-[#f3f1de]">清空本机聊天记录？</Text>
          <Focusable class="p-[24] bg-[#233322] focus:bg-[#49643a]" onPress={home}><Text class="text-4xl text-[#f3f1de]">返回，保留记录</Text></Focusable>
          <Focusable class="p-[24] bg-[#233322] focus:bg-[#49643a]" onPress={() => { void write("history.json", "[]").then(() => { messages = []; home(); setStatus("记录已清空"); }).catch(error); }}><Text class="text-4xl text-[#f3f1de]">确认清空</Text></Focusable>
        </View></FocusScope>
      </Show>
      <View class="flex-row justify-between items-center h-[44]">
        <Text class="text-2xl text-[#f3f1de]">{screen() === "read" ? readTitle() : busy() ? "请求中：X 取消" : "A 选择　X 返回"}</Text>
        <Text class="text-2xl text-[#c1cbbf]">{busy() ? "处理中…" : "随时可退出"}</Text>
      </View>
      <Text class="text-2xl text-[#c1cbbf]">上下：移动　A：确认　B：返回　MENU：退出</Text>
    </View>
  );
}
mount(() => <App />);
