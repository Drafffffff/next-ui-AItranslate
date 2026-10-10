// Runs the actual bundled Pi loop in a VM without Node/browser transport globals.
import assert from "node:assert/strict";
import vm from "node:vm";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

const directory = path.dirname(fileURLToPath(import.meta.url));
const result = await build({
  entryPoints: [path.join(directory, "agent.ts")], bundle: true, write: false,
  platform: "neutral", target: "es2020", format: "iife", globalName: "WorkshopAgent", metafile: true,
});
assert(Object.keys(result.metafile.inputs).some(p => p.endsWith("pi-agent-core/dist/agent.js")));
assert(Object.keys(result.metafile.inputs).some(p => p.endsWith("pi-agent-core/dist/agent-loop.js")));
const context = vm.createContext({});
vm.runInContext(result.outputFiles[0].text, context);
assert.equal(vm.runInContext("typeof AbortController", context), "function");
assert.equal(vm.runInContext("typeof structuredClone", context), "function");
assert.equal(vm.runInContext("typeof fetch", context), "undefined");
const { createGameAgent } = context.WorkshopAgent;
const toolCall = (id, name, args) => ({ id, type: "function", function: { name, arguments: typeof args === "string" ? args : JSON.stringify(args) } });
const completion = (content, calls = [], reason) => ({ status: 200, text: JSON.stringify({
  choices: [{ message: { role: "assistant", content, ...(calls.length ? { tool_calls: calls } : {}) },
    finish_reason: reason || (calls.length ? "tool_calls" : "stop") }],
  usage: { prompt_tokens: 100, completion_tokens: 25, total_tokens: 125 },
}) });
function mock(responses) {
  const requests = [];
  return {
    requests,
    request(url, key, body, timeoutMs) {
      requests.push({ url, key, body: JSON.parse(body), timeoutMs });
      const next = responses.shift();
      assert(next, "Unexpected extra model turn");
      return { promise: Promise.resolve(typeof next === "function" ? next(requests.at(-1)) : next), cancel() {} };
    },
  };
}
const base = { endpoint: "https://example.invalid/v1", modelId: "deepseek-flash", key: "private-test-key", systemPrompt: "Create one game." };
const schema = { type: "object", properties: { lua: { type: "string" } }, required: ["lua"], additionalProperties: false };

// A real Pi tool call, failed native tool, model repair, then final response.
let executions = 0;
const events = [];
const transport = mock([
  completion(null, [toolCall("write-1", "write_game", { lua: "bad lua" })]),
  request => {
    assert.equal(request.body.messages.at(-1).role, "tool");
    assert.match(request.body.messages.at(-1).content, /invalid lua/);
    return completion(null, [toolCall("write-2", "write_game", { lua: "function _draw() cls(1) end" })]);
  },
  request => {
    assert.equal(request.body.messages.at(-1).role, "tool");
    assert.match(request.body.messages.at(-1).content, /saved/);
    return completion("Ready to preview.");
  },
]);
const agent = createGameAgent({ ...base, request: transport.request, tools: [{
  name: "write_game", description: "Write game Lua.", parameters: schema,
  async execute(args) { executions++; if (args.lua === "bad lua") throw new Error("invalid lua"); return { saved: true }; },
}], onEvent(event) { events.push(event.type); } });
await agent.prompt("Create a game.");
assert.equal(executions, 2, JSON.stringify(agent.messages));
assert.equal(transport.requests.length, 3);
assert.equal(transport.requests[0].url, "https://example.invalid/v1/chat/completions");
assert.equal(transport.requests[0].body.stream, false);
assert.equal(transport.requests[0].body.tools[0].function.name, "write_game");
assert.equal(agent.busy, false);
assert.equal(agent.messages.length, 6);
assert(events.includes("tool_execution_start") && events.includes("tool_execution_end"));
assert.equal(events.at(-1), "agent_end");
assert(!JSON.stringify(agent.messages).includes(base.key));

// Save/reopen the exact Pi transcript, retaining tool results on subsequent turns.
const saved = JSON.parse(JSON.stringify(agent.messages));
const reopenedTransport = mock([request => {
  assert.equal(request.body.messages.filter(m => m.role === "tool").length, 2);
  assert.equal(request.body.messages.at(-1).content, "Make it slower.");
  return completion("Slower now.");
}]);
const reopened = createGameAgent({ ...base, messages: saved, tools: [], request: reopenedTransport.request });
await reopened.prompt("Make it slower.");
assert.equal(reopened.messages.length, 8);

// Invalid JSON arguments are reported by Pi validation and never reach the tool.
let invalidExecuted = false;
const invalidTransport = mock([
  completion(null, [toolCall("invalid", "write_game", "{not-json")]),
  request => {
    assert.match(request.body.messages.at(-1).content, /Validation failed/);
    return completion("Please clarify.");
  },
]);
await createGameAgent({ ...base, request: invalidTransport.request, tools: [{
  name: "write_game", description: "Write game Lua.", parameters: schema,
  async execute() { invalidExecuted = true; return "saved"; },
}] }).prompt("Create.");
assert.equal(invalidExecuted, false);

// Native HTTP cancellation settles even if its underlying promise never does.
let requestStarted;
const started = new Promise(resolve => requestStarted = resolve);
let cancelled = false;
const cancelAgent = createGameAgent({ ...base, tools: [], request() {
  requestStarted();
  return { promise: new Promise(() => {}), cancel() { cancelled = true; } };
} });
const cancelling = cancelAgent.prompt("Generate.");
await started;
assert.equal(cancelAgent.busy, true);
cancelAgent.abort();
await cancelling;
assert(cancelled);
assert.equal(cancelAgent.busy, false);
assert.equal(cancelAgent.messages.at(-1).stopReason, "aborted");

// Each prompt has a firm model-turn cap, avoiding an unbounded tool loop.
const loopTransport = mock([
  completion(null, [toolCall("loop-1", "ping", {})]),
  completion(null, [toolCall("loop-2", "ping", {})]),
]);
const loopAgent = createGameAgent({ ...base, maxTurns: 2, request: loopTransport.request, tools: [{
  name: "ping", description: "Ping.", parameters: { type: "object", properties: {}, additionalProperties: false },
  async execute() { return "pong"; },
}] });
await loopAgent.prompt("Loop.");
assert.equal(loopTransport.requests.length, 2);
assert.equal(loopAgent.messages.at(-1).stopReason, "error");
assert.match(loopAgent.messages.at(-1).errorMessage, /2/);

// 100 really reaches the transport; no hidden 16-turn clamp. A fresh prompt
// resets its budget, and a tool loop stops before issuing request 101.
const ping={name:'ping',description:'Ping.',parameters:{type:'object',properties:{},additionalProperties:false},async execute(){return 'pong';}};
const hundredTransport=mock([...Array.from({length:100},(_,i)=>i===99?completion('Finished on turn 100.'):completion(null,[toolCall('hundred-'+i,'ping',{})])),completion('Fresh prompt budget.')]);
const hundredAgent=createGameAgent({...base,maxTurns:100,request:hundredTransport.request,tools:[ping]});
await hundredAgent.prompt('Use all 100 turns.');
assert.equal(hundredTransport.requests.length,100);assert.equal(hundredAgent.messages.at(-1).stopReason,'stop');
await hundredAgent.prompt('Another request.');
assert.equal(hundredTransport.requests.length,101);assert.equal(hundredAgent.messages.at(-1).stopReason,'stop');
const defaultTransport=mock(Array.from({length:1000},(_,i)=>completion(null,[toolCall('default-'+i,'ping',{})])));
const defaultAgent=createGameAgent({...base,request:defaultTransport.request,tools:[ping]});
await defaultAgent.prompt('Loop to the default limit.');
assert.equal(defaultTransport.requests.length,1000);assert.equal(defaultAgent.messages.at(-1).stopReason,'error');
assert.match(defaultAgent.messages.at(-1).errorMessage,/1000 轮上限/);

// HTTP errors are visible but the credential is redacted in persisted events.
const errorTransport = mock([{ status: 401, text: JSON.stringify({ error: { message: `invalid key ${base.key}` } }) }]);
const errors = [];
const errorAgent = createGameAgent({ ...base, tools: [], request: errorTransport.request, onEvent(e) { errors.push(e); } });
await errorAgent.prompt("Create.");
assert.equal(errorAgent.messages.at(-1).stopReason, "error");
assert(errors.some(e => e.type === "error" && /HTTP 401/.test(e.message)));
assert(!JSON.stringify(errors).includes(base.key));

// A truncated output never executes apparently complete tool arguments.
let truncatedExecuted = false;
const truncatedTransport = mock([
  completion(null, [toolCall("truncated", "write_game", { lua: "unsafe partial" })], "length"),
  request => { assert.match(request.body.messages.at(-1).content, /token limit/); return completion("Retry later."); },
]);
await createGameAgent({ ...base, request: truncatedTransport.request, tools: [{
  name: "write_game", description: "Write game Lua.", parameters: schema,
  async execute() { truncatedExecuted = true; return "saved"; },
}] }).prompt("Create.");
assert.equal(truncatedExecuted, false);

// A cancelled multi-tool transcript can reopen without dangling tool call IDs.
const incomplete = JSON.parse(JSON.stringify(saved));
incomplete[1].content.push({ type: "toolCall", id: "never-run", name: "write_game", arguments: { lua: "aborted" } });
const incompleteTransport = mock([request => {
  const replayedCalls = request.body.messages.flatMap(m => m.tool_calls || []);
  assert(!replayedCalls.some(c => c.id === "never-run"));
  return completion("Continue safely.");
}]);
await createGameAgent({ ...base, messages: incomplete, tools: [], request: incompleteTransport.request }).prompt("Continue.");

console.log(`PASS: real Pi core bundled (${result.outputFiles[0].text.length} bytes); native VM shims; tool repair; session reopening; invalid arguments; cancellation; explicit 100/default 1000-turn budget; redacted HTTP failure; truncated calls; incomplete transcript recovery.`);
