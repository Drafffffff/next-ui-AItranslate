import {projectReads} from './read-projection.ts';
import {compact,prepareCompaction,estimateTokens,calculateContextTokens,shouldCompact,type CompactionSettings} from './node_modules/@earendil-works/pi-agent-core/dist/harness/compaction/compaction.js';
import {convertToLlm,createCompactionSummaryMessage} from './node_modules/@earendil-works/pi-agent-core/dist/harness/messages.js';
import type {AgentMessage} from './node_modules/@earendil-works/pi-agent-core/dist/types.js';
import type {SessionTreeEntry} from './node_modules/@earendil-works/pi-agent-core/dist/harness/types.js';
import type {Model,Models,Usage} from './node_modules/@earendil-works/pi-ai/dist/types.js';

export interface CompactionRecord {
 schema:1;summary:string;firstKept:number;through:number;createdAt:number;tokensBefore:number;usage?:Usage;usageThrough?:number;
}
export interface ContextOptions {
 latest?:CompactionRecord;
 contextWindow?:number;reserveTokens?:number;keepRecentTokens?:number;
 onStart?():void;
 yieldFrame?():Promise<void>;
 // Must durably save the record and original history before projection changes.
 onCommit(record:CompactionRecord,messages:AgentMessage[]):Promise<void>;
}
export const DEFAULT_CONTEXT_WINDOW=500000;
export function contextWindow(value?:number){return Number.isFinite(value)?Math.floor(Math.max(8000,Math.min(DEFAULT_CONTEXT_WINDOW,value!))):DEFAULT_CONTEXT_WINDOW;}
export function validCompaction(record:unknown,length:number):record is CompactionRecord {
 const r=record as CompactionRecord;
 return !!r&&r.schema===1&&typeof r.summary==='string'&&r.summary.trim().length>0&&r.summary.length<=16000&&Number.isInteger(r.firstKept)&&Number.isInteger(r.through)&&r.firstKept>=0&&r.firstKept<r.through&&r.through<=length&&(r.usageThrough===undefined||Number.isInteger(r.usageThrough)&&r.usageThrough>=r.through&&r.usageThrough<=length)&&Number.isFinite(r.createdAt)&&Number.isFinite(r.tokensBefore)&&r.tokensBefore>=0;
}
// Pi's fallback estimates characters/4. Account conservatively for Chinese and
// the fixed Skill/tool declarations before the first provider usage is available.
function estimateText(text:string){const ascii=text.replace(/[^\x00-\x7f]/g,'').length;return Math.ceil((ascii+(text.length-ascii)*3)/3);}
async function entries(messages:AgentMessage[],record?:CompactionRecord,yieldFrame?:()=>Promise<void>):Promise<SessionTreeEntry[]> {
 const result:SessionTreeEntry[]=[];let started=Date.now();
 for(let i=record?.firstKept??0;i<=messages.length;i++){
  if(record&&i===record.through)result.push({type:'compaction',id:'compact-'+i,parentId:i?'m-'+(i-1):null,timestamp:new Date(record.createdAt).toISOString(),summary:record.summary,firstKeptEntryId:'m-'+record.firstKept,tokensBefore:record.tokensBefore});
  if(i<messages.length)result.push({type:'message',id:'m-'+i,parentId:i?'m-'+(i-1):null,timestamp:new Date(messages[i].timestamp||0).toISOString(),message:messages[i]} as SessionTreeEntry);
  if(i%16===15&&Date.now()-started>=6&&yieldFrame){await yieldFrame();started=Date.now();}
 }
 return result;
}
export const COMPACTION_INSTRUCTIONS=`Summarize this one game's development history in concise Simplified Chinese, aiming for under 1200 Chinese characters. Rewrite and consolidate previous summaries: keep current facts and unresolved work; collapse completed debugging into its final outcome, omit superseded intermediate code and duplicated examples. Preserve the user's goals, gameplay rules, controls, visual preferences, constraints, decisions, unfinished requests and player-reported failures. Distinguish draft saved, format checked, published, cancelled and merely suggested. Do not copy full Lua programs, sprite sheets or note arrays: exact source and pixels come from read_game; tracker audio comes from read_skill music. Preserve requested music style, tempo, sound events and silence preferences. Never claim gameplay was tested. Preserve the latest request, project identity and revision facts. Previous summaries are memory, not authoritative current source.`;
export function createContextManager(options:ContextOptions,models:Models,model:Model<any>,fixedContext:string) {
 const window=contextWindow(options.contextWindow);
 const fixedTokens=estimateText(fixedContext);
 const requestedReserve=Number.isFinite(options.reserveTokens)?options.reserveTokens!:16000;
 const reserve=Math.max(2048,Math.min(window/2,Math.max(requestedReserve,model.maxTokens)));
 const keep=Number.isFinite(options.keepRecentTokens)?options.keepRecentTokens!:12000;
 const settings:CompactionSettings={enabled:true,reserveTokens:Math.floor(reserve),keepRecentTokens:Math.floor(Math.max(512,Math.min(Math.floor((window-reserve-fixedTokens-2048)/4),Math.max(512,keep))))};
 let latest=options.latest,attempts=0;
 const stripped=new WeakMap<object,AgentMessage>(),estimated=new WeakMap<object,{text:number;pi:number}>();
 let summaryRecord:CompactionRecord|undefined,summaryMessage:AgentMessage|undefined,estimates=0;
 let prefix:{count:number;first:AgentMessage;last:AgentMessage;refs:AgentMessage[];text:number;pi:number;usage:number;trailing:number}|undefined;
 const project=(messages:AgentMessage[])=>{
  const record=latest&&validCompaction(latest,messages.length)?latest:undefined;
  if(!record)return messages.slice();
  if(summaryRecord!==record){summaryRecord=record;summaryMessage=createCompactionSummaryMessage(record.summary,record.tokensBefore,new Date(record.createdAt).toISOString());}
  // The workshop has one linear, append-only message path. This is Pi's
  // compaction projection without reconstructing all discarded tree entries.
  const projected:AgentMessage[]=[summaryMessage!];
  for(let i=record.firstKept;i<messages.length;i++){
   const m=messages[i];
   if(i<(record.usageThrough??record.through)&&m.role==='assistant'){
    let clean=stripped.get(m);if(!clean){clean={...m,usage:{...m.usage,input:0,output:0,cacheRead:0,cacheWrite:0,totalTokens:0}};stripped.set(m,clean);}projected.push(clean);
   }else projected.push(m);
  }
  // Old usage was measured before this compaction and must not immediately
  // trigger another compaction of a now-small projected context.
  if(record){let lastUser=messages.length-1;while(lastUser>=0&&messages[lastUser].role!=='user')lastUser--;
   if(lastUser>=0&&lastUser<record.firstKept)projected.splice(1,0,messages[lastUser]);
  }
  return projected;
 };
 const tokens=async(messages:AgentMessage[],signal?:AbortSignal)=>{
  messages=projectReads(messages as any) as AgentMessage[];
  // Pi passes new arrays with the same completed, immutable message objects.
  // Reuse the prefix totals on append, and reset at projection/run boundaries.
  if(!prefix||messages.length<prefix.count||messages[0]!==prefix.first||messages[prefix.count-1]!==prefix.last||prefix.refs.some((m,i)=>m!==messages[i]))prefix=undefined;
  let count=prefix?.count??0,text=prefix?.text??fixedTokens,pi=prefix?.pi??0,usage=prefix?.usage??0,trailing=prefix?.trailing??0,started=Date.now();
  for(let i=count;i<messages.length;i++){
   const m=messages[i];let value=estimated.get(m);
   if(!value){value={text:estimateText(JSON.stringify(convertToLlm([m]))),pi:estimateTokens(m)};estimated.set(m,value);estimates++;}
   text+=value.text;pi+=value.pi;
   const u=m.role==='assistant'&&m.stopReason!=='error'&&m.stopReason!=='aborted'?calculateContextTokens(m.usage):0;
   if(u>0){usage=u;trailing=0;}else trailing+=value.pi;
   if(i%16===15&&Date.now()-started>=6&&options.yieldFrame){await options.yieldFrame();if(signal?.aborted)throw new Error('Operation aborted');started=Date.now();}
  }
  if(messages.length)prefix={count:messages.length,first:messages[0],last:messages[messages.length-1],refs:messages.slice(),text,pi,usage,trailing};
  return Math.max(usage?usage+trailing:pi,text);
 };
 return {project,metrics:()=>({estimates}),resetRun(){attempts=0;prefix=undefined;},async transform(messages:AgentMessage[],signal?:AbortSignal){
  let projected=project(messages);
  let before=await tokens(projected,signal);
  if(!shouldCompact(before,window,settings))return projected;
  if(signal?.aborted)return projected;
  // Imported histories can already exceed a provider window. Summarize bounded
  // chronological batches with Pi, saving each checkpoint before advancing.
  if(window>=64000&&before>window){
   const input=projectReads(messages as any) as AgentMessage[];
   const budget=Math.max(8192,Math.floor(window-settings.reserveTokens-20000));
   const batchSettings={...settings,keepRecentTokens:Math.min(settings.keepRecentTokens,Math.floor(budget/8))};
   let batches=0;
   while(shouldCompact(before,window,settings)){
    if(signal?.aborted)throw new Error('Operation aborted');
    if(batches++>=256)throw new Error('较早对话已分批保存，继续即可恢复剩余整理');
    const start=latest?.firstKept??0;let used=0,end=messages.length,safe=start;
    for(let i=start;i<input.length;i++){
     if(i>start&&input[i].role==='user')safe=i;
     const m=input[i];let cached=estimated.get(m);
     if(!cached){cached={text:estimateText(JSON.stringify(convertToLlm([m]))),pi:estimateTokens(m)};estimated.set(m,cached);estimates++;}
     used+=cached.text;
     if(used>budget){end=safe;break;}
     if(i%128===127&&options.yieldFrame)await options.yieldFrame();
    }
    if(end<=start||end<=(latest?.through??0))throw new Error('单次历史交互超过整理预算，历史已保留');
    const preparedBatch=prepareCompaction(await entries(input.slice(0,end),latest&&validCompaction(latest,messages.length)&&latest.through<=end?latest:undefined,options.yieldFrame),batchSettings);
    if(!preparedBatch.ok)throw preparedBatch.error;
    const plan=preparedBatch.value;
    if(!plan||(!plan.messagesToSummarize.length&&!plan.turnPrefixMessages.length))throw new Error('当前历史无法安全分段，历史已保留');
    options.onStart?.();
    const batch=await compact(plan,models,{...model,maxTokens:16384},COMPACTION_INSTRUCTIONS,signal,'medium',{enabled:false,maxRetries:0,baseDelayMs:0});
    if(!batch.ok)throw new Error(batch.error.message.replace(/^(?:Turn prefix summarization|Summarization) failed:\s*/,''));
    if(signal?.aborted)throw new Error('Operation aborted');
    const value=batch.value,firstKept=Number(value.firstKeptEntryId?.replace(/^m-/,''));
    const checkpoint:CompactionRecord={schema:1,summary:value.summary,firstKept,through:end,usageThrough:messages.length,createdAt:Date.now(),tokensBefore:before,usage:value.usage};
    if(!validCompaction(checkpoint,messages.length)||firstKept<=start)throw new Error('分段摘要不完整，原始历史已保留');
    await options.onCommit(checkpoint,messages);latest=checkpoint;projected=project(messages);before=await tokens(projected,signal);
   }
   return projected;
  }
  if(attempts++>=2)throw new Error('本次上下文仍过大，请把需求分成更小的修改；历史已保留');
  const prepared=prepareCompaction(await entries(projectReads(messages as any) as AgentMessage[],latest&&validCompaction(latest,messages.length)?latest:undefined,options.yieldFrame),settings);
  if(!prepared.ok)throw prepared.error;
  const preparation=prepared.value;
  if(!preparation||(!preparation.messagesToSummarize.length&&!preparation.turnPrefixMessages.length))throw new Error('当前需求过长，暂时无法压缩；请缩短需求，历史已保留');
  options.onStart?.();
  const result=await compact(preparation,models,{...model,maxTokens:16384},COMPACTION_INSTRUCTIONS,signal,'medium',{enabled:false,maxRetries:0,baseDelayMs:0});
  if(!result.ok){
   // Pi prefixes errors with an English phase label; show the actionable cause.
   throw new Error(result.error.message.replace(/^(?:Turn prefix summarization|Summarization) failed:\s*/,''));
  }
  if(signal?.aborted)return projected;
  const value=result.value,firstKept=Number(value.firstKeptEntryId?.replace(/^m-/,''));
  const record:CompactionRecord={schema:1,summary:value.summary,firstKept,through:messages.length,createdAt:Date.now(),tokensBefore:before,usage:value.usage};
  if(!validCompaction(record,messages.length))throw new Error('摘要不完整，原始对话已保留');
  await options.onCommit(record,messages);
  latest=record;projected=project(messages);
  if(shouldCompact(await tokens(projected,signal),window,settings))throw new Error('压缩后上下文仍过大，请把需求分成更小的修改；历史已保留');
  return projected;
 }};
}
