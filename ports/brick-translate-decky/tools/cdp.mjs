// Use Steam's existing debugger through an SSH tunnel, without changing its configuration.
const origin = process.env.BRICK_CDP_ORIGIN ?? "http://127.0.0.1:18080";
export async function evaluate(expression) {
  const tabs = await fetch(`${origin}/json/list`).then(response => response.json());
  const tab = tabs.find(value => value.title === "SharedJSContext");
  if (!tab) throw new Error("Steam SharedJSContext is unavailable");
  const url = new URL(tab.webSocketDebuggerUrl);
  url.host = new URL(origin).host;
  const socket = new WebSocket(url);
  await new Promise((resolve, reject) => {socket.onopen = resolve; socket.onerror = reject;});
  try {
    return await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {socket.close(); reject(new Error("Steam evaluation timed out"));}, 90000);
      socket.onmessage = event => {
        const message = JSON.parse(event.data);
        if (message.id !== 1) return;
        clearTimeout(timeout);
        if (message.error || message.result?.exceptionDetails) reject(new Error(JSON.stringify(message.error ?? message.result.exceptionDetails)));
        else resolve(message.result.result.value);
      };
      socket.send(JSON.stringify({id: 1, method: "Runtime.evaluate", params: {expression, awaitPromise: true, returnByValue: true}}));
    });
  } finally {socket.close();}
}
if (process.argv[1]?.endsWith("cdp.mjs")) {
  console.log(JSON.stringify(await evaluate(process.argv[2]), null, 2));
}
