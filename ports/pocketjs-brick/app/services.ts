import { onFrame } from "@pocketjs/framework/solid/lifecycle";
interface Result { id: number; error: string; text: string; status: number; lines: string[] }
interface Native { start(json: string): number; cancel(id: number): void; poll(): Result[]; testing?: boolean; testUrl?: string; testTlsUrl?: string }
export const native = (globalThis as unknown as { brick: Native }).brick;
const waiting = new Map<number, { resolve: (value: Result) => void; reject: (error: Error) => void }>();
export function installServices() {
  onFrame(() => {
    if (!waiting.size) return;
    for (const result of native.poll()) {
      const task = waiting.get(result.id); if (!task) continue;
      waiting.delete(result.id);
      if (result.error) task.reject(new Error(result.error)); else task.resolve(result);
    }
  });
}
export function task(request: Record<string, unknown>) {
  const id = native.start(JSON.stringify(request));
  return {
    id,
    cancel: () => native.cancel(id),
    promise: new Promise<Result>((resolve, reject) => waiting.set(id, { resolve, reject })),
  };
}
export const read = async (name: string) => (await task({ op: "read", name }).promise).text;
export const write = (name: string, text: string) => task({ op: "write", name, text }).promise;
export const hasSharedCredential = async (url: string) => (await task({ op: "credentials", url }).promise).text === "yes";
export const cleanText = (text: string) => Array.from(text.replace(/\r\n?/g, "\n").replace(/\t/g, "  "))
  .map(c => { const n = c.codePointAt(0)!; return n === 10 || (n >= 32 && n < 0xfffd && !(n >= 0xfe00 && n <= 0xfe0f)) ? c : "?"; }).join("");
export const prepare = async (text: string) => (await task({ op: "text", text: cleanText(text).slice(0, 16000) }).promise).lines;
export function request(url: string, key = "", body = "", timeoutMs = 30000) {
  return task({ op: "http", url, key, text: body, method: body ? "POST" : "GET", timeoutMs });
}
