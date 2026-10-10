// QuickJS has promises and collections but no browser AbortController. No timer,
// fetch, Node, or DOM shim is necessary for the non-streaming native transport.
class PocketAbortSignal {
  aborted = false;
  reason: unknown;
  onabort: ((event: { type: string }) => void) | null = null;
  private listeners = new Set<(event: { type: string }) => void>();
  addEventListener(type: string, listener: (event: { type: string }) => void) {
    if (type === "abort") this.listeners.add(listener);
  }
  removeEventListener(type: string, listener: (event: { type: string }) => void) {
    if (type === "abort") this.listeners.delete(listener);
  }
  throwIfAborted() { if (this.aborted) throw this.reason; }
  dispatch(reason: unknown) {
    if (this.aborted) return;
    this.aborted = true;
    this.reason = reason ?? new Error("Operation aborted");
    const event = { type: "abort" };
    for (const listener of this.listeners) listener(event);
    this.listeners.clear();
    this.onabort?.(event);
  }
}
class PocketAbortController {
  readonly signal = new PocketAbortSignal();
  abort(reason?: unknown) { this.signal.dispatch(reason); }
}
const globals = globalThis as unknown as Record<string, unknown>;
if (!globals.AbortController) globals.AbortController = PocketAbortController;
if (!globals.AbortSignal) globals.AbortSignal = PocketAbortSignal;
if (!globals.structuredClone) globals.structuredClone = (value: unknown) =>
  value === undefined ? undefined : JSON.parse(JSON.stringify(value));
if (!globals.queueMicrotask) globals.queueMicrotask = (fn: () => void) => { void Promise.resolve().then(fn); };


// TypeBox hashes schemas using UTF-8 during dependency initialization.
class PocketTextEncoder {
  readonly encoding = "utf-8";
  encode(value = ""): Uint8Array {
    const bytes: number[] = [];
    for (const character of String(value)) {
      let point = character.codePointAt(0)!;
      if (point >= 0xd800 && point <= 0xdfff) point = 0xfffd;
      if (point < 0x80) bytes.push(point);
      else if (point < 0x800) bytes.push(0xc0 | (point >> 6), 0x80 | (point & 0x3f));
      else if (point < 0x10000) bytes.push(0xe0 | (point >> 12), 0x80 | ((point >> 6) & 0x3f), 0x80 | (point & 0x3f));
      else bytes.push(0xf0 | (point >> 18), 0x80 | ((point >> 12) & 0x3f), 0x80 | ((point >> 6) & 0x3f), 0x80 | (point & 0x3f));
    }
    return new Uint8Array(bytes);
  }
}
if (!globals.TextEncoder) globals.TextEncoder = PocketTextEncoder;

// TypeBox uses URL for schema IDs/references, including its default base URL.
// This covers hierarchical IDs and fragments; it is not a browser URL API.
class PocketURL {
  readonly href: string;
  readonly protocol: string;
  readonly host: string;
  readonly hostname: string;
  readonly pathname: string;
  readonly search: string;
  readonly hash: string;
  readonly origin: string;
  constructor(input: string | PocketURL, base?: string | PocketURL) {
    let value = String(input);
    if (!/^[a-z][a-z0-9+.-]*:/i.test(value)) {
      if (!base) throw new TypeError("Invalid URL");
      const parent = new PocketURL(String(base));
      if (value.startsWith("//")) value = parent.protocol + value;
      else if (value.startsWith("#")) value = parent.href.replace(/#.*$/, "") + value;
      else if (value.startsWith("?")) value = parent.href.replace(/[?#].*$/, "") + value;
      else {
        const prefix = parent.protocol + "//" + parent.host;
        const pathname = value.startsWith("/") ? value : parent.pathname.replace(/[^/]*$/, "") + value;
        value = prefix + pathname;
      }
    }
    const parsed = /^([a-z][a-z0-9+.-]*:)(?:\/\/([^/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(value);
    if (!parsed) throw new TypeError("Invalid URL");
    this.protocol = parsed[1].toLowerCase();
    this.host = parsed[2]?.toLowerCase() || "";
    if ((this.protocol === "http:" || this.protocol === "https:") && (!this.host || /\s/.test(this.host)))
      throw new TypeError("Invalid URL");
    this.hostname = this.host.replace(/^.*@/, "").replace(/:\d+$/, "");
    let pathname = parsed[3] || (parsed[2] !== undefined ? "/" : "");
    if (parsed[2] !== undefined) {
      const parts: string[] = [];
      for (const part of pathname.split("/")) {
        if (part === "..") parts.pop();
        else if (part !== ".") parts.push(part);
      }
      pathname = parts.join("/") || "/";
      if (!pathname.startsWith("/")) pathname = "/" + pathname;
    }
    this.pathname = pathname;
    this.search = parsed[4] || "";
    this.hash = parsed[5] || "";
    this.origin = parsed[2] === undefined ? "null" : this.protocol + "//" + this.host;
    this.href = this.protocol + (parsed[2] === undefined ? "" : "//" + this.host) + this.pathname + this.search + this.hash;
  }
  toString() { return this.href; }
}
if (!globals.URL) globals.URL = PocketURL;
