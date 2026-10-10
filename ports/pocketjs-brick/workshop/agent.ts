/**
 * Pi 0.81.1 host adapter, following PocketPi's minimal Agent/streamFn boundary.
 * Sources: https://github.com/earendil-works/pi and
 * https://github.com/pocket-nexus/pocket-pi (MIT). Exact npm versions are locked.
 * Deep imports deliberately avoid the coding-agent filesystem/provider runtime.
 */
import "./agent-runtime";
import type {Diagnostic} from './project-state.ts';
import {projectReads} from './read-projection.ts';
import {requestJson} from './request-json.ts';
import {completionSse} from './completion-sse.ts';
import {MAX_NETWORK_RETRIES,transientFailure,timerDelay} from './retry.ts';
import {generationParameters,generationBudget,type GenerationProvider} from './generation-models.ts';
import {createContextManager,contextWindow,COMPACTION_INSTRUCTIONS,type ContextOptions} from './context.ts';
import {convertToLlm} from './node_modules/@earendil-works/pi-agent-core/dist/harness/messages.js';
import { Agent } from "./node_modules/@earendil-works/pi-agent-core/dist/agent.js";
import { AssistantMessageEventStream } from "./node_modules/@earendil-works/pi-ai/dist/utils/event-stream.js";
import type { AgentEvent, AgentMessage } from "./node_modules/@earendil-works/pi-agent-core/dist/types.js";
import type { AssistantMessage, Context, Model, Models, SimpleStreamOptions } from "./node_modules/@earendil-works/pi-ai/dist/types.js";

export type GameMessage = AgentMessage;
export const MAX_AGENT_TURNS = 1000;
export type GameAgentEvent = AgentEvent | {type:'diagnostic';record:Diagnostic} | { type: "error"; message: string } | {type:'retry';attempt:number;delayMs:number;message:string;summary:boolean};
export type JSONSchema = Record<string, unknown>;
export interface GameTool {
  name: string;
  description: string;
  parameters: JSONSchema;
  execute(args: Record<string, unknown>, signal?: AbortSignal,toolCallId?:string): Promise<string | Record<string, unknown>>;
}
export type GameRequest = (url: string, key: string, body: string, timeoutMs: number,onChunk?:(chunk:string)=>void) => {
  promise: Promise<{ status: number; text: string; streamTail?:boolean }>;
  cancel(): void;
};
export interface GameAgentOptions {
  modelId: string;
  provider?:GenerationProvider;
  /** Full /chat/completions URL or the provider's base URL. */
  endpoint: string;
  key: string;
  systemPrompt?: string;
  messages?: GameMessage[];
  tools: GameTool[];
  request: GameRequest;
  requestStream?:GameRequest;
  onEvent?: (event: GameAgentEvent) => void | Promise<void>;
  /** Maximum HTTP/model turns for one prompt, including repair attempts. */
  maxTurns?: number;
  maxOutputTokens?: number;
  timeoutMs?: number;
  wait?:(ms:number,signal?:AbortSignal)=>Promise<void>;
  context?:ContextOptions;
}


function textContent(content: unknown): string {
  if (typeof content === "string") return content;
  if (!Array.isArray(content)) return "";
  return content.filter(c => c?.type === "text").map(c => c.text || "").join("\n");
}

/** Preserve history, while excluding incomplete calls from a cancelled turn. */
function openAiMessages(context: Context): Record<string, unknown>[] {
  const output: Record<string, unknown>[] = [];
  if (context.systemPrompt) output.push({ role: "system", content: context.systemPrompt });
  const paired = new Set(context.messages.filter(m => m.role === "toolResult").map(m => m.toolCallId));
  const emitted = new Set<string>();
  for (const message of projectReads(context.messages)) {
    if (message.role === "user") {
      output.push({ role: "user", content: textContent(message.content) });
    } else if (message.role === "assistant") {
      if (message.stopReason === "error" || message.stopReason === "aborted") continue;
      const calls = message.content.filter(c => c.type === "toolCall" && paired.has(c.id));
      const content = textContent(message.content);
      if (!content && !calls.length) continue;
      const value: Record<string, unknown> = { role: "assistant", content: content || null };
      if (calls.length) {
        value.tool_calls = calls.map(call => {
          emitted.add(call.id);
          return { id: call.id, type: "function", function: { name: call.name, arguments: JSON.stringify(call.arguments) } };
        });
      }
      const reasoning = message.content.filter(c => c.type === "thinking").map(c => c.thinking).join("\n");
      // Old non-thinking conversations legitimately have no reasoning. Do not
      // fabricate it; keep an explicit empty field for DeepSeek tool histories.
      value.reasoning_content = reasoning;
      output.push(value);
    } else if (message.role === "toolResult" && emitted.has(message.toolCallId)) {
      let content=textContent(message.content);
      if(message.isError){
        const at=content.indexOf('\n\nReceived arguments:');
        if(at>=0)content=content.slice(0,at)+'\n参数校验未通过，工具未执行。按字段错误局部修正；完整参数保留在项目历史。';
        content=content.slice(0,3000);
      }
      output.push({ role: "tool", tool_call_id: message.toolCallId, content });
    }
  }
  return output;
}

function blankMessage(model: Model<any>): AssistantMessage {
  return {
    role: "assistant", content: [], api: model.api, provider: model.provider, model: model.id,
    stopReason: "stop", timestamp: Date.now(),
    usage: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0, totalTokens: 0,
      cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0, total: 0 } },
  };
}

function completionUrl(endpoint: string): string {
  const value = endpoint.trim().replace(/\/+$/, "");
  if (!/^https?:\/\//i.test(value)) throw new Error("模型地址必须以 http:// 或 https:// 开头");
  return /\/chat\/completions(?:\?|$)/.test(value) ? value : value + "/chat/completions";
}

export function createGameAgent(options: GameAgentOptions) {
  const endpoint = completionUrl(options.endpoint);
  const configuredTurns = options.maxTurns ?? MAX_AGENT_TURNS;
  const maxTurns = Number.isFinite(configuredTurns) ? Math.max(1, Math.min(10000, Math.floor(configuredTurns))) : MAX_AGENT_TURNS;
  const model: Model<any> = {
    id: options.modelId, name: options.modelId, provider: options.provider??"deepseek", api: "openai-completions",
    baseUrl: endpoint, reasoning: true, input: ["text"], contextWindow: contextWindow(options.context?.contextWindow),
    maxTokens:Math.min(options.maxOutputTokens??8192,Math.floor(contextWindow(options.context?.contextWindow)/4)),cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0 },
  };
  let turns = 0,contextError='';
  const safeError = (error: unknown) => {
    let value = error instanceof Error ? error.message : String(error);
    if (options.key) value = value.split(options.key).join("[redacted]");
    return value.slice(0, 600);
  };
  const completionStream = (requestModel: Model<any>, context: Context, streamOptions?: SimpleStreamOptions,summary=false) => {
    const stream = new AssistantMessageEventStream();
    const message = blankMessage(requestModel);
    const signal = streamOptions?.signal;
    const streaming=!!options.requestStream;
    let requestStarted=0,firstTokenMs:number|undefined;
    const parserStream={push:(event:any)=>{if(firstTokenMs===undefined&&event.type.endsWith('_delta')&&event.delta)firstTokenMs=Date.now()-requestStarted;if(!summary)stream.push(event);}} as unknown as AssistantMessageEventStream;
    let parser=streaming?completionSse(message,parserStream):undefined,received=0,closed=false;
    void (async () => {
      let removeAbort = () => {};
      try {
        if (signal?.aborted) throw new Error("Operation aborted");
        if (!summary&&contextError)throw new Error(contextError);
        if (!summary&&++turns > maxTurns) throw new Error(`已达到本次 ${maxTurns} 轮上限，进度已保留；可继续提出修改。`);
        const body: Record<string, unknown> = {
          ...generationParameters(options.provider??'deepseek'),
          ...generationBudget(options.provider??'deepseek',requestModel.id,streamOptions?.maxTokens??requestModel.maxTokens,summary),
          model: requestModel.id, messages: openAiMessages(context), stream: streaming,
        };
        if(streaming)body.stream_options={include_usage:true};
        if (context.tools?.length) {
          body.tools = context.tools.map(tool => ({ type: "function", function: {
            name: tool.name, description: tool.description, parameters: tool.parameters,
          } }));
          body.tool_choice = "auto";
        }
        if(streaming&&!summary)stream.push({type:'start',partial:message});
        const transport=streaming?options.requestStream!:options.request;
        const serialized=await requestJson(body,options.context?.yieldFrame,signal);
        for(let attempt=0;;attempt++){
        let accepting=true;requestStarted=Date.now();firstTokenMs=undefined;let firstByteMs:number|undefined;
        await options.onEvent?.({type:'diagnostic',record:{kind:'request_start',at:requestStarted,model:requestModel.id,provider:options.provider??'deepseek',summary,attempt,requestChars:serialized.length}});
        try{
        const task = transport(endpoint, options.key, serialized, options.timeoutMs??120000,chunk=>{if(accepting&&!closed&&!signal?.aborted){if(firstByteMs===undefined)firstByteMs=Date.now()-requestStarted;received+=chunk.length;parser?.feed(chunk);}});
        // Cancellation settles even if the native transport takes time to reap its child.
        const aborted = new Promise<never>((_resolve, reject) => {
          const cancel = () => { task.cancel(); reject(new Error("Operation aborted")); };
          signal?.addEventListener("abort", cancel, { once: true });
          removeAbort = () => signal?.removeEventListener("abort", cancel);
          if (signal?.aborted) cancel();
        });
        const response = await Promise.race([task.promise, aborted]);
        await options.onEvent?.({type:'diagnostic',record:{kind:'request_end',at:Date.now(),summary,attempt,elapsedMs:Date.now()-requestStarted,firstByteMs,firstTokenMs,status:response.status}});
        if (signal?.aborted) throw new Error("Operation aborted");
        if (response.status < 200 || response.status >= 300) {
          let detail = "";
          try { detail = JSON.parse(response.text)?.error?.message || ""; } catch {}
          throw new Error(`HTTP ${response.status}${detail ? ": " + detail : ""}`);
        }
        if(streaming&&(response.streamTail||!response.text.trimStart().startsWith('{'))){
          parser!.feed(response.streamTail?response.text:response.text.slice(received));parser!.finish();
          if(!summary&&message.stopReason==='length'&&!message.content.some(p=>p.type==='text'||p.type==='toolCall'))throw new Error('模型达到单次 '+(streamOptions?.maxTokens??requestModel.maxTokens)+' token 输出上限（包含思考），尚未产出文字或工具调用；草稿和需求已保留，请按较小阶段继续。');
          stream.push({type:'done',reason:message.stopReason as 'stop'|'length'|'toolUse',message});return;
        }
        // Some compatible endpoints ignore stream=true and return a complete JSON.
        const data = JSON.parse(response.text);
        const choice = data.choices?.[0];
        const incoming = choice?.message;
        if (!incoming) throw new Error("模型响应缺少 choices[0].message");
        if (typeof incoming.reasoning_content === "string" && incoming.reasoning_content) {
          message.content.push({ type: "thinking", thinking: incoming.reasoning_content });
        }
        const content = textContent(incoming.content) || incoming.refusal || "";
        if (content) message.content.push({ type: "text", text: content });
        for (const call of incoming.tool_calls ?? []) {
          if (call.type !== "function" || typeof call.id !== "string" || typeof call.function?.name !== "string")
            throw new Error("模型返回了无效的工具调用");
          let args: unknown;
          try { args = JSON.parse(call.function.arguments || "{}"); }
          catch { args = call.function.arguments; } // Pi's schema validator returns this error to the model.
          message.content.push({ type: "toolCall", id: call.id, name: call.function.name,
            arguments: args as Record<string, unknown> });
        }
        const calls = message.content.some(c => c.type === "toolCall");
        if (!content && !calls && choice.finish_reason !== 'length') throw new Error("模型没有返回文字或工具调用");
        message.stopReason = choice.finish_reason === "length" ? "length" : calls ? "toolUse" : "stop";
        const usage = data.usage || {};
        message.usage.input = Number(usage.prompt_tokens) || 0;
        message.usage.output = Number(usage.completion_tokens) || 0;
        message.usage.totalTokens = Number(usage.total_tokens) || message.usage.input + message.usage.output;
        message.usage.cacheRead = Number(usage.prompt_cache_hit_tokens) || 0;
        if(!summary&&message.stopReason==='length'&&!content&&!calls)throw new Error('模型达到单次 '+(streamOptions?.maxTokens??requestModel.maxTokens)+' token 输出上限（包含思考），尚未产出文字或工具调用；草稿和需求已保留，请按较小阶段继续。');
        stream.push({ type: "done", reason: message.stopReason, message });
        return;
        }catch(error){
          await options.onEvent?.({type:'diagnostic',record:{kind:'request_error',at:Date.now(),summary,attempt,elapsedMs:Date.now()-requestStarted,firstByteMs,firstTokenMs,error:safeError(error)}});
          if(signal?.aborted||attempt>=MAX_NETWORK_RETRIES||!transientFailure(error))throw error;
          const delayMs=Math.round(1000*2**attempt*(.85+Math.random()*.3));
          await options.onEvent?.({type:'retry',attempt:attempt+1,delayMs,message:safeError(error),summary});
          accepting=false;removeAbort();removeAbort=()=>{};
          // Reset the same partial message, rather than pushing a second start
          // into Pi's history. Fragments and tool JSON from attempts never mix.
          message.content=[];message.usage=blankMessage(requestModel).usage;received=0;
          parser=streaming?completionSse(message,parserStream):undefined;
          if(streaming&&!summary)stream.push({type:'text_delta',contentIndex:0,delta:'',partial:message});
          await (options.wait??timerDelay)(delayMs,signal);
        }finally{accepting=false;removeAbort();removeAbort=()=>{};}
        }
      } catch (error) {
        message.content = message.content.filter(part=>part.type==='text');
        message.stopReason = signal?.aborted ? "aborted" : "error";
        message.errorMessage = safeError(error);
        stream.push({ type: "error", reason: message.stopReason, error: message });
      } finally {
        closed=true;
        removeAbort();
      }
    })();
    return stream;
  };
  const streamFn = (_model:Model<any>,context:Context,streamOptions?:SimpleStreamOptions)=>completionStream(model,context,streamOptions);
  // Pi compaction needs only completeSimple. Reuse our native HTTP bridge and
  // cancellation, without loading browser/Node provider transports.
  const summaryModels={completeSimple:async(requestModel:Model<any>,context:Context,streamOptions?:SimpleStreamOptions)=>{
    let limit=Math.min(32768,Math.max(16384,streamOptions?.maxTokens??requestModel.maxTokens)),message:AssistantMessage;
    // Pi's turn-prefix summary does not forward customInstructions. Apply the
    // same game-specific policy to both history and split-turn checkpoints.
    const summaryContext={...context,systemPrompt:(context.systemPrompt||'')+'\n\n'+COMPACTION_INSTRUCTIONS};
    for(let attempt=0;;attempt++){
      message=await completionStream(requestModel,summaryContext,{...streamOptions,maxTokens:limit},true).result();
      // A truncated checkpoint is never committed. Retry only this cause once,
      // One larger-output retry, separate from bounded transport recovery.
      if(message.stopReason==='length'&&attempt===0&&limit<32768&&!streamOptions?.signal?.aborted){limit=Math.min(32768,limit*2);continue;}
      break;
    }
    if(message.stopReason!=='error'&&message.stopReason!=='aborted'&&(message.stopReason!=='stop'||!textContent(message.content).trim())){
      const reason=message.stopReason;
      message.content=[];message.stopReason='error';message.errorMessage=reason==='length'?`摘要达到 ${limit} token 输出上限，原始对话已保留`:'模型返回了空摘要，原始对话已保留';
    }
    return message;
  }} as unknown as Models;
  const contextManager=options.context?createContextManager(options.context,summaryModels,model,(options.systemPrompt||'')+JSON.stringify(options.tools.map(({execute,...tool})=>tool))):undefined;
  const agent = new Agent({
    initialState: {
      model, systemPrompt: options.systemPrompt || "", thinkingLevel: "medium",
      messages: options.messages || [],
      tools: options.tools.map(tool => ({
        name: tool.name, label: tool.name, description: tool.description, parameters: tool.parameters as any,
        execute: async (_id: string, args: Record<string, unknown>, signal?: AbortSignal) => {
          if (signal?.aborted) throw new Error("Operation aborted");
          const result = await tool.execute(args, signal,_id);
          if (signal?.aborted) throw new Error("Operation aborted");
          return { content: [{ type: "text" as const, text: typeof result === "string" ? result : JSON.stringify(result) }], details: result };
        },
      })),
    },
    streamFn, toolExecution: "sequential",
    convertToLlm:options.context?convertToLlm:undefined,
    transformContext:contextManager?async(messages,signal)=>{
      try{return await contextManager.transform(messages,signal);}
      catch(error){contextError='对话整理未完成：'+safeError(error);return contextManager.project(messages);}
    }:undefined,
  });
  agent.subscribe(async event => {
    await options.onEvent?.(event);
    if (event.type === "message_end" && event.message.role === "assistant" && event.message.stopReason === "error")
      await options.onEvent?.({ type: "error", message: event.message.errorMessage || "模型请求失败" });
  });
  return {
    async prompt(text: string) {
      if (agent.state.isStreaming) throw new Error("当前任务仍在进行");
      if (!text.trim()) return;
      turns = 0;
      contextError='';contextManager?.resetRun();
      await agent.prompt(text);
    },
    abort() { agent.abort(); },
    get messages(): GameMessage[] { return agent.state.messages; },
    get busy(): boolean { return agent.state.isStreaming; },
  };
}

export type GameAgent = ReturnType<typeof createGameAgent>;
