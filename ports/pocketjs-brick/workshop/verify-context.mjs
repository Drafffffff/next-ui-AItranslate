import assert from 'node:assert/strict';
import vm from 'node:vm';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {build} from 'esbuild';
import {readFileSync} from 'node:fs';
const directory=path.dirname(fileURLToPath(import.meta.url));
const bundle=await build({entryPoints:[path.join(directory,'agent.ts')],bundle:true,write:false,platform:'neutral',target:'es2020',format:'iife',globalName:'WorkshopAgent',metafile:true});
assert(Object.keys(bundle.metafile.inputs).some(p=>p.endsWith('harness/compaction/compaction.js')));
const context=vm.createContext({});vm.runInContext(bundle.outputFiles[0].text,context);
const {createGameAgent}=context.WorkshopAgent;
const clone=value=>JSON.parse(JSON.stringify(value));
const content=m=>typeof m.content==='string'?m.content:m.content?.filter(c=>c.type==='text').map(c=>c.text).join('\n')||'';
const usage={input:100,output:25,cacheRead:0,cacheWrite:0,totalTokens:125,cost:{input:0,output:0,cacheRead:0,cacheWrite:0,total:0}};
const assistant=(blocks,stopReason='stop')=>({role:'assistant',content:blocks,api:'openai-completions',provider:'deepseek',model:'deepseek-flash',stopReason,timestamp:1,usage:clone(usage)});
function history(count=16,start=0){const messages=[];for(let i=start;i<start+count;i++){
 messages.push({role:'user',content:'需求'+i+'：保留三条命，方向键移动，O 重开。'+'旧玩法讨论：目标、风险与已完成决定。'.repeat(100),timestamp:1});
 messages.push(assistant([{type:'toolCall',id:'old-'+i,name:'read_game',arguments:{}}],'toolUse'));
 messages.push({role:'toolResult',toolCallId:'old-'+i,toolName:'read_game',content:[{type:'text',text:JSON.stringify({revision:i,lua:'-- historical code '+i+'\n'+'x=1\n'.repeat(1800)})}],isError:false,timestamp:1});
 messages.push(assistant([{type:'text',text:'第 '+i+' 版已发布，玩法仍需试玩。'}]));
 }return messages;}
const complete=(text='## Goal\n保留三条命、方向键移动、O重开。\n## Next Steps\n按当前需求修改，先 read_game。',calls=[],reason)=>({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:calls.length?null:text,...(calls.length?{tool_calls:calls}:{})},finish_reason:reason||(calls.length?'tool_calls':'stop')}],usage:{prompt_tokens:100,completion_tokens:25,total_tokens:125}})});
const base={wait:async()=>{},endpoint:'https://example.invalid/v1',modelId:'deepseek-flash',key:'private-fixture-key',systemPrompt:'<skill name="pico8-game-dev">Always read_game. START previews the game.</skill>',maxTurns:4};
const policy={contextWindow:16000,reserveTokens:4096,keepRecentTokens:2200};
const readCall={id:'fresh-read',type:'function',function:{name:'read_game',arguments:'{}'}};
let toolsRun=0;
const tools=[{name:'read_game',description:'Read exact current source.',parameters:{type:'object',properties:{},additionalProperties:false},execute:async()=>{toolsRun++;return {lua:'REAL_CURRENT_SOURCE',revision:99};}}];
function paired(messages){const emitted=new Set();for(const m of messages){for(const call of m.tool_calls||[])emitted.add(call.id);if(m.role==='tool')assert(emitted.has(m.tool_call_id),'Tool result was cut away from its call');}}
// Trigger automatically, retain original history and the latest request, then run real Pi tools.
const original=history(),originalBytes=JSON.stringify(original),records=[],requests=[];let mainCalls=0;
const make=(messages,latest,extra={})=>createGameAgent({...base,messages,tools,context:{...policy,latest,onCommit:async(record,log)=>{assert.equal(JSON.stringify(log.slice(0,original.length)),originalBytes);records.push(clone(record));},...extra},
 request(url,key,body,timeout){assert.equal(key,base.key);const value=JSON.parse(body);requests.push(value);
  if(!value.tools){assert(value.max_tokens>=16384&&value.max_tokens<=32768);assert(timeout<=120000);assert(!JSON.stringify(value).includes(base.key));return {promise:Promise.resolve(complete()),cancel(){}};}
  assert.equal(value.messages[0].content,base.systemPrompt);assert(value.messages.some(m=>content(m).includes('<summary>')));assert.equal(value.messages.filter(m=>m.role==='user').at(-1).content,'最新要求：降低速度');paired(value.messages);mainCalls++;
  return {promise:Promise.resolve(mainCalls%2===1?complete('',[readCall]):complete('已保存，等待试玩。')),cancel(){}};
 }});
let agent=make(original);await agent.prompt('最新要求：降低速度');
assert.equal(agent.messages.at(-1).stopReason,'stop');assert.equal(toolsRun,1);assert(records.length>=1);assert.equal(JSON.stringify(agent.messages.slice(0,original.length)),originalBytes);
const main=requests.find(r=>r.tools),summaryCalls=requests.filter(r=>!r.tools).length;
assert(JSON.stringify(main.messages).length<originalBytes.length/2);assert.equal(agent.messages.length,original.length+4);
assert(!JSON.stringify(agent.messages).includes('<summary>'));
// Reopen using the stored checkpoint. No repeated compaction due to old usage counts.
const saved=clone(agent.messages),latest=records.at(-1),before=requests.filter(r=>!r.tools).length;
agent=make(saved,latest);await agent.prompt('最新要求：降低速度');assert.equal(requests.filter(r=>!r.tools).length,before);assert.equal(agent.messages.at(-1).stopReason,'stop');
// Subsequent compaction updates the previous summary and advances its boundary.
const expanded=clone(agent.messages).concat(history(16,100));let updateSeen=false;
const updateAgent=createGameAgent({...base,messages:expanded,tools,context:{...policy,latest,onCommit:async record=>records.push(clone(record))},
 request(url,key,body){const value=JSON.parse(body);if(!value.tools){const text=value.messages.map(content).join('\n');updateSeen ||= text.includes('<previous-summary>')&&text.includes(latest.summary);return {promise:Promise.resolve(complete()),cancel(){}};}return {promise:Promise.resolve(complete('继续修改。')),cancel(){}};}});
await updateAgent.prompt('再增加一个关卡');assert(updateSeen);assert(records.at(-1).firstKept>latest.firstKept);assert.equal(updateAgent.messages.length,expanded.length+2);
// Failed or truncated summaries never commit, execute tools, or send an oversized game request.
for(const response of [{status:503,text:JSON.stringify({error:{message:'temporarily unavailable '+base.key}})},complete('partial',[],'length'),complete('')]){
 let commits=0,normal=0;
 const failing=createGameAgent({...base,messages:original,tools,context:{...policy,onCommit:async()=>{commits++;}},request(url,key,body){if(JSON.parse(body).tools)normal++;return {promise:Promise.resolve(response),cancel(){}};}});
 await failing.prompt('继续修改');assert.equal(commits,0);assert.equal(normal,0);assert.equal(failing.messages.at(-1).stopReason,'error');assert(!JSON.stringify(failing.messages).includes(base.key));assert.equal(JSON.stringify(failing.messages.slice(0,original.length)),originalBytes);
}
// Cancel a hung summary through the ordinary Agent abort signal.
// A growing iterative summary can hit its output cap. Increase it once, and
// commit only the completed retry; apply game instructions to split prefixes.
let retryBudgets=[],retryCommits=[],retryNormal=0;
const retrying=createGameAgent({...base,messages:original,tools,context:{...policy,onCommit:async record=>retryCommits.push(clone(record))},request(url,key,body){
 const value=JSON.parse(body);
 if(value.tools){retryNormal++;return {promise:Promise.resolve(complete('已继续修改')),cancel(){}};}
 assert(value.messages[0].content.includes('under 1200 Chinese characters'));
 assert(value.messages[0].content.includes('collapse completed debugging'));
 retryBudgets.push(value.max_tokens);
 return {promise:Promise.resolve(retryBudgets.length===1?complete('TRUNCATED_CHECKPOINT',[],'length'):complete('COMPLETE_CHECKPOINT')),cancel(){}};
}});
await retrying.prompt('继续整理菜单');
assert.equal(retrying.messages.at(-1).stopReason,'stop');assert.equal(retryNormal,1);assert(retryCommits.length>0);
assert.equal(retryBudgets[1],Math.min(32768,retryBudgets[0]*2));
assert(retryCommits.every(r=>!r.summary.includes('TRUNCATED_CHECKPOINT')));
let failureCalls=0;
const capped=createGameAgent({...base,messages:original,tools,context:{...policy,onCommit:async()=>{throw new Error('must not commit truncated checkpoint');}},request(){failureCalls++;return {promise:Promise.resolve(complete('TRUNCATED_CHECKPOINT',[],'length')),cancel(){}};}});
await capped.prompt('继续');assert.equal(failureCalls,2);assert.equal(capped.messages.at(-1).stopReason,'error');
assert.match(capped.messages.at(-1).errorMessage,/摘要达到 \d+ token 输出上限/);assert(!capped.messages.at(-1).errorMessage.includes('Summarization failed'));
assert.equal(JSON.stringify(capped.messages.slice(0,original.length)),originalBytes);
let retryStartedResolve,retryCancelled=false,retryCount=0;
const retryStarted=new Promise(resolve=>retryStartedResolve=resolve);
const cancelRetry=createGameAgent({...base,messages:original,tools,context:{...policy,onCommit:async()=>{throw new Error('cancelled retry committed');}},request(){
 retryCount++;if(retryCount===1)return {promise:Promise.resolve(complete('partial',[],'length')),cancel(){}};
 retryStartedResolve();return {promise:new Promise(()=>{}),cancel(){retryCancelled=true;}};
}});
const retryRunning=cancelRetry.prompt('继续');await retryStarted;cancelRetry.abort();await retryRunning;
assert(retryCancelled);assert.equal(retryCount,2);assert.equal(cancelRetry.messages.at(-1).stopReason,'aborted');
// Optional read-only replay of a device project through real Pi and fake HTTP.
// Explicit and default 500k windows must accept histories beyond the old 128k
// clamp; small custom budgets above still exercise automatic compaction.
for(const explicit of [undefined,500000]){
 let wideSummaries=0,wideNormal=0;
 const wide=createGameAgent({...base,messages:history(80),tools,context:{contextWindow:explicit,onCommit:async()=>{throw new Error('Premature 500k compaction');}},request(url,key,body){
  const value=JSON.parse(body);if(!value.tools)wideSummaries++;else wideNormal++;
  return {promise:Promise.resolve(complete('继续修改')),cancel(){}};
 }});
 await wide.prompt('继续');assert.equal(wide.messages.at(-1).stopReason,'stop');assert.equal(wideSummaries,0);assert.equal(wideNormal,1);
}
// Never call the saved endpoint, publish, or mutate the source project file.
if(process.env.WORKSHOP_CONTEXT_REPLAY){
 const source=readFileSync(process.env.WORKSHOP_CONTEXT_REPLAY,'utf8'),savedProject=JSON.parse(source),replayBudgets=[],replayRecords=[];let normal=0;
 const replay=createGameAgent({...base,messages:savedProject.messages,context:{latest:savedProject.compactions?.at(-1),onCommit:async r=>replayRecords.push(clone(r))},tools,
  request(url,key,body){const value=JSON.parse(body);
   if(!value.tools){replayBudgets.push(value.max_tokens);return {promise:Promise.resolve(replayBudgets.length===1?complete('partial',[],'length'):complete('当前项目已有第5版；菜单与HUD修改已保存草稿，尚未检查或发布。继续时先读取当前代码，保留射击、强化和用户操作约定。')),cancel(){}};}
   normal++;assert.equal(value.messages.filter(m=>m.role==='user').at(-1).content,'继续');return {promise:Promise.resolve(normal===1?complete('',[readCall]):complete('已读取当前草稿，继续修改。')),cancel(){}};
  }});
 await replay.prompt('继续');assert.equal(replay.messages.at(-1).stopReason,'stop');assert.equal(normal,2);assert.equal(replayRecords.length,0);
 assert.deepEqual(replayBudgets,[],'500k budget should not repeat premature summaries');assert.equal(JSON.stringify(replay.messages.slice(0,savedProject.messages.length)),JSON.stringify(savedProject.messages));
 assert.equal(readFileSync(process.env.WORKSHOP_CONTEXT_REPLAY,'utf8'),source);
 console.log(`PASS: read-only replay ${savedProject.title} / ${savedProject.messages.length} messages / summary budgets ${replayBudgets.join(',')} / current-source tool; no paid API or project/game mutation.`);
}
let startedResolve,cancelled=false,commits=0;const started=new Promise(resolve=>startedResolve=resolve);
const cancelling=createGameAgent({...base,messages:original,tools,context:{...policy,onCommit:async()=>{commits++;}},request(){startedResolve();return {promise:new Promise(()=>{}),cancel(){cancelled=true;}};}});
const running=cancelling.prompt('继续修改');await started;assert(cancelling.busy);cancelling.abort();await running;assert(cancelled);assert.equal(commits,0);assert.equal(cancelling.messages.at(-1).stopReason,'aborted');assert(!cancelling.busy);
// A failed durable save must not adopt a checkpoint or send the game request.
let saves=0,normal=0;
const diskFailure=createGameAgent({...base,messages:original,tools,context:{...policy,onCommit:async()=>{saves++;throw new Error('SD write failed');}},request(url,key,body){if(JSON.parse(body).tools)normal++;return {promise:Promise.resolve(complete()),cancel(){}};}});
await diskFailure.prompt('继续修改');assert.equal(saves,1);assert.equal(normal,0);assert.match(diskFailure.messages.at(-1).errorMessage,/SD write failed/);
console.log(`PASS: automatic real Pi compaction (${summaryCalls} initial summary calls); durable checkpoint/reopening; iterative summary; fresh source tool; paired calls; full local history; cancellation; failed/truncated/empty summary; failed persistence; no Node/browser globals.`);
// Project integration: persist full history plus checkpoint, reopen, and publish.
const files=new Map();let summaryCount=0,stage=0,failSave=false;const summaryStages=[];
const projectHarness={
 read:async name=>files.get(name),
 write:async(name,raw)=>{if(failSave&&JSON.parse(raw).compactions?.length){failSave=false;throw new Error('SD checkpoint save failed');}files.set(name,raw);},
 task:input=>({promise:Promise.resolve({text:'published'})}),
 request(url,key,body){const value=JSON.parse(body);
  if(!value.tools){summaryStages.push(stage);summaryCount++;return {promise:Promise.resolve(complete()),cancel(){}};}
  assert(value.messages[0].content.includes('<skill name="pico8-game-dev">'));assert(value.messages.some(m=>content(m).includes('<summary>')));paired(value.messages);
  const call=(name,args={})=>({id:'integration-'+stage+'-'+name,type:'function',function:{name,arguments:JSON.stringify(args)}});
  const calls=[()=>[call('read_game')],()=>[call('write_game',{lua:'function _init() music(6) end\nfunction _update() if btnp(4) then sfx(0,3) end end\nfunction _draw() cls(0) end\n',title:'压缩测试',summary:'保留三条命'})],()=>[call('check_game'),call('publish_game')],()=>[]][stage++%4]();
  return {promise:Promise.resolve(complete('已保存，等待试玩。',calls)),cancel(){}};
 }
};
const projectBundle=await build({entryPoints:[path.join(directory,'model.ts')],bundle:true,write:false,platform:'neutral',target:'es2020',format:'iife',globalName:'WorkshopModel',
 plugins:[{name:'isolated-native',setup(build){build.onResolve({filter:/services\.ts$/},()=>({path:'services',namespace:'isolated-native'}));build.onLoad({filter:/.*/,namespace:'isolated-native'},()=>({contents:'export const read=(...a)=>globalThis.__harness.read(...a);export const write=(...a)=>globalThis.__harness.write(...a);export const task=(...a)=>globalThis.__harness.task(...a);export const writeChunks=async(n,produce)=>{const parts=[];await produce(p=>parts.push(p));return write(n,parts.join(""));};export const delay=async()=>{};export const requestStream=undefined;export const request=(...a)=>globalThis.__harness.request(...a);',loader:'js'}));}}]});
const projectVM=vm.createContext({__harness:projectHarness});vm.runInContext(projectBundle.outputFiles[0].text,projectVM);
const {newProject,decodeProject,projectAgent}=projectVM.WorkshopModel;
const config={key:base.key,endpoint:base.endpoint,model:base.modelId,timeoutMs:10000,contextWindowTokens:64000};
const statuses=[];let p=newProject();p.messages=history(32);
const rawHistory=JSON.stringify(p.messages);
await projectAgent(p,config,text=>statuses.push(text),()=>{}).prompt('降低速度');
assert.equal(p.revision,1);assert(p.compactions.length>0);assert(summaryCount>0);assert(statuses.some(t=>t.includes('正在整理较早对话')));
assert.equal(JSON.stringify(p.messages.slice(0,128)),rawHistory);
p=await projectVM.WorkshopModel.loadProject(projectVM.WorkshopModel.projectInfo(p));
assert(p.compactions.length>0);const persistedSummary=p.compactions.at(-1).summary,summaryBefore=summaryCount,reopenStage=stage;
await projectAgent(p,config,()=>{},()=>{}).prompt('保留现有规则，再降低速度');
assert(summaryStages.slice(summaryBefore).every(s=>s>reopenStage),"Reopening alone redundantly compressed the same checkpoint");assert.equal(p.revision,1);assert.equal(p.compactions.at(-1).summary,persistedSummary);
const bad=clone(p);bad.compactions=[{schema:1,summary:'broken',firstKept:-1,through:99999}];
const fallback=decodeProject(JSON.stringify(bad));assert.equal(fallback.compactions,undefined);assert.equal(JSON.stringify(fallback.messages),JSON.stringify(p.messages));
// A failed checkpoint write rolls back optional metadata and keeps original messages.
const failingProject=newProject();failingProject.messages=history(32);failSave=true;const oldStage=stage;
await assert.rejects(projectAgent(failingProject,config,()=>{},()=>{}).prompt('降低速度'),/SD checkpoint save failed/);
assert.equal(failSave,false);assert.equal(stage,oldStage);assert.equal(failingProject.compactions,undefined);
assert.equal(JSON.stringify(failingProject.messages.slice(0,128)),rawHistory);
assert.equal(decodeProject(files.get('projects/'+failingProject.id+'.json')).compactions,undefined);
console.log('PASS: project compaction persists full history/checkpoints, reopens without redundant summaries, publishes once and rechecks identical bytes without a duplicate revision, recovers corrupt optional metadata, and rolls back failed checkpoint saves.');
