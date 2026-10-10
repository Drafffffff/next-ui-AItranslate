import {read, write,writeChunks,task, request,requestStream,delay} from '../app/services.ts';
import {largeHistory,projectSnapshot,streamProject} from './persistence.ts';
import {saveJournal,hydrateHistory,validManifest,resetHistory,type HistoryManifest} from './journal.ts';
import {transientFailure} from './retry.ts';
import {contentHash,VALIDATOR_VERSION,DESIGN_PROPERTIES,validDesign,validWork,type DesignRecord,type WorkState,type Diagnostic} from './project-state.ts';
import {deliveryDiagnostics} from './delivery.ts';
import {createGameAgent, MAX_AGENT_TURNS, type GameMessage, type GameTool, type GameAgentEvent} from './agent.ts';
import {pico8SystemPrompt,BUILTIN_GFX,BLANK_GFX,validateGfx,mergeSprites,gameAssets,readSkillModule,SKILL_MODULE_NAMES} from './skill.ts';
import {BUILTIN_AUDIO,AUDIO_CATALOG,validateAudio,mergeAudio,audioSections,AUDIO_PATCH_PROPERTIES,type GameAudio} from './audio.ts';
import {validCompaction,type CompactionRecord} from './context.ts';
import {EDIT_PROPERTIES,updateLua,codeDifference} from './editing.ts';
import {generationProvider,providerName,type Config} from './generation-models.ts';
export type {Config} from './generation-models.ts';
import {PICO8_SKILL_MANIFEST} from './skill.generated.ts';
import {STARTER} from './starter.ts';
export {STARTER} from './starter.ts';
import {makeQuestion,validQuestion,recordAnswer,QUESTION_PARAMETERS,type UserQuestion,type QuestionAnswer} from './questions.ts';
import {returnView,makeReturnView,type ReturnView} from './return-view.ts';
import type {TranscriptRow} from './transcript.ts';

export interface Version {revision:number; title:string; summary:string; lua:string; gfx?:string;audio?:GameAudio;design?:DesignRecord}
export class PublishedReplyError extends Error {
 constructor(public revision:number,cause:string){super('第 '+revision+' 版已发布，可按 SR 试玩；完成说明获取失败：'+cause);}
}
export interface Project {
  schema:1; id:string; title:string; revision:number; updatedAt:number;
  lua:string; summary:string; gfx?:string;audio?:GameAudio; messages:GameMessage[]; versions:Version[];
  pending?:Version;
  share?:{url:string;revision:number;hash:string};
  design?:DesignRecord;
  history?:HistoryManifest;
  work?:WorkState;
  diagnostics?:Diagnostic[];
  scaffoldReady?:true;
  question?:UserQuestion;
  compactions?:CompactionRecord[];
  summaryOnly?:true;
  draftChanged?:boolean;
}
export function newProject():Project {
  const id='game-'+Date.now().toString(36)+'-'+Math.floor(Math.random()*1679616).toString(36);
  return {schema:1,id,title:'新游戏',revision:0,updatedAt:Date.now(),lua:STARTER,gfx:BUILTIN_GFX,audio:mergeAudio(BUILTIN_AUDIO),summary:'描述你想做的游戏',messages:[],versions:[]};
}
export function validateLua(lua:string) {
  if (typeof lua!=='string' || !lua || /[^\x09\x0a\x0d\x20-\x7e]/.test(lua)) throw new Error('游戏代码必须为非空 ASCII Lua');
  for (const name of ['_init','_update','_draw']) if (!new RegExp('function\\s+'+name+'\\s*\\(').test(lua)) throw new Error('缺少游戏入口 '+name);
  if (/\n__(?:gfx|lua|map|sfx|music|gff)__/.test(lua)) throw new Error('请只提供 Lua 代码');
  if (/\b(?:os|io|require|loadfile|dofile|loadstring|load|debug|package|coroutine)\b/.test(lua.replace(/--[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,''))) throw new Error('使用了当前运行器不支持的接口');
}
export function cartridge(lua:string,gfx=BLANK_GFX,audio?:GameAudio) {
  validateLua(lua);validateGfx(gfx);
  return 'pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\n'+lua.trimEnd()+'\n__gfx__\n'+gfx+'\n'+(audio?audioSections(audio):'');
}
export function decodeProject(raw:string):Project {
  const p=JSON.parse(raw);
  if (p?.schema!==1 || typeof p.id!=='string' || !/^game-[a-z0-9-]{1,43}$/.test(p.id) || typeof p.title!=='string' || !Number.isInteger(p.revision) || p.revision<0 || !Array.isArray(p.messages) || !Array.isArray(p.versions) || typeof p.lua!=='string') throw new Error('项目文件格式异常');
  for(const v of [...p.versions,...(p.pending?[p.pending]:[])]) if(!Number.isInteger(v.revision)||typeof v.title!=='string'||typeof v.summary!=='string'||typeof v.lua!=='string') throw new Error('游戏版本格式异常');
  for(const value of [p,...p.versions,...(p.pending?[p.pending]:[])]){if(value.gfx!==undefined)validateGfx(value.gfx);if(value.audio!==undefined)validateAudio(value.audio);}
  if(p.history!==undefined&&!validManifest(p.history,p.id))throw new Error('历史清单格式异常');
  if(p.design!==undefined&&!validDesign(p.design))throw new Error('项目设计记录格式异常');
  if(p.work!==undefined&&!validWork(p.work))delete p.work;
  // Broken optional summary metadata falls back to the complete original log.
  if(p.compactions!==undefined&&(!Array.isArray(p.compactions)||!p.compactions.every((r:unknown)=>validCompaction(r,p.history?.count??p.messages.length))))delete p.compactions;
  if(!p.history&&p.question&&!validQuestion(p.question,p.messages))delete p.question;
  return p;
}
export async function listProjects():Promise<Project[]> {
  const names=JSON.parse((await task({op:'projects'}).promise).text) as string[];
  const result:Project[]=[];
  for(const name of names){
    let p:Project|undefined;
    try{const raw=await read('project-info/'+name),info=raw?JSON.parse(raw):undefined;
      if(info?.cache===1&&info.id+'.json'===name&&typeof info.title==='string'&&Number.isInteger(info.revision)&&Number.isFinite(info.updatedAt))p=projectInfo(info);
    }catch{}
    if(!p){const full=await hydrateHistory(decodeProject(await read('projects/'+name)),read,()=>delay(0));p=projectInfo(full);await writeProjectInfo(full).catch(()=>{});}
    result.push(p);
  }
  return result.sort((a,b)=>b.updatedAt-a.updatedAt);
}
export function hasDraftChanges(p:Project){if(p.summaryOnly||!Array.isArray(p.versions))return !!p.draftChanged;const v=p.versions.find(v=>v.revision===p.revision);return !!v&&(p.lua!==v.lua||p.title!==v.title||p.summary!==v.summary||(p.gfx??BUILTIN_GFX)!==(v.gfx??BUILTIN_GFX)||JSON.stringify(p.audio??BUILTIN_AUDIO)!==JSON.stringify(v.audio??BUILTIN_AUDIO));}
export function projectInfo(p:Project):Project{return {schema:1,id:p.id,title:p.title,revision:p.revision,updatedAt:p.updatedAt,summary:p.summary,scaffoldReady:p.scaffoldReady,work:p.work,draftChanged:hasDraftChanges(p),lua:'',messages:[],versions:[],summaryOnly:true};}
function writeProjectInfo(p:Project,view?:ReturnView){return write('project-info/'+p.id+'.json',JSON.stringify({cache:1,id:p.id,title:p.title,revision:p.revision,updatedAt:p.updatedAt,summary:p.summary,scaffoldReady:p.scaffoldReady,work:p.work,draftChanged:hasDraftChanges(p),...(view?{returnView:view}:{})}));}
export async function loadReturnView(id:string):Promise<{project:Project;view:ReturnView}|undefined>{
 if(!/^game-[a-z0-9-]{1,43}$/.test(id))return;
 try{
  const raw=await read('project-info/'+id+'.json');if(!raw||raw.length>98304)return;
  const info=JSON.parse(raw);
  if(info.cache!==1||info.id!==id||typeof info.title!=='string'||!Number.isInteger(info.revision)||info.revision<0||!Number.isFinite(info.updatedAt))return;
  const view=returnView(info.returnView,info);if(view)return {project:projectInfo(info),view};
 }catch{}
}
/** Project writes invalidate this metadata natively. Queue the view behind them. */
export async function saveReturnView(p:Project,rows:TranscriptRow[]){
 const view=makeReturnView(p,rows);if(!view)return;
 const info={...projectInfo(p)};
 const next=saves.catch(()=>{}).then(async()=>{if(p.updatedAt===view.updatedAt)await writeProjectInfo(info,view);});saves=next;
 await next;
}
export async function loadProject(p:Project):Promise<Project>{
 const full=p.summaryOnly?decodeProject(await read('projects/'+p.id+'.json')):p;
 await hydrateHistory(full,read,()=>delay(0));if(full.question&&!validQuestion(full.question,full.messages))delete full.question;return full;
}
let saves=Promise.resolve<unknown>(undefined);
export function saveProject(p:Project) {
  p.updatedAt=Date.now();const snapshot=projectSnapshot(p);
  const info={...projectInfo(p)};
  const next=saves.catch(()=>{}).then(async()=>{const started=Date.now();if(await largeHistory(snapshot.messages,()=>delay(0)))await saveJournal(p,snapshot,writeChunks,()=>delay(0));else await writeChunks('projects/'+p.id+'.json',append=>streamProject(snapshot,append,()=>delay(0)));await writeProjectInfo(info).catch(()=>{});p.diagnostics=[...(p.diagnostics??[]),{kind:'save',at:Date.now(),elapsedMs:Date.now()-started}].slice(-256);}); saves=next; return next;
}
async function publishVersion(p:Project,v:Version) {
  await task({op:'publish',name:p.id,title:v.title,text:cartridge(v.lua,v.gfx,v.audio)}).promise;
  if(v.revision===0)p.scaffoldReady=true;
  p.revision=v.revision; p.title=v.title; p.summary=v.summary; p.lua=v.lua;
  if(v.gfx!==undefined)p.gfx=v.gfx;else delete p.gfx;
  if(v.audio!==undefined)p.audio=v.audio;else delete p.audio;
  if(!p.versions.some(old=>old.revision===v.revision))p.versions.push(v);
  p.versions=p.versions.slice(-8); delete p.pending;
  if(v.design)p.design=v.design;
  if(p.work){p.work.phase='pending_reply';p.work.revision=v.revision;p.work.updatedAt=Date.now();delete p.work.check;}
  await saveProject(p);
}
export async function initializeProject(p:Project){
  if(p.scaffoldReady)return;
  p.pending={revision:0,title:p.title,summary:'基础脚手架 · 可试玩',lua:p.lua,gfx:p.gfx,audio:p.audio};
  await saveProject(p);await publishVersion(p,p.pending);
}
export async function answerSavedQuestion(p:Project,answer:QuestionAnswer){
  if(!p.question)throw new Error('没有待回答的问题');
  resetHistory(p);recordAnswer(p.messages,p.question,answer);delete p.question;await saveProject(p);
}
export async function recoverProject(p:Project) {
  // Cancellation keeps the draft, but revokes publication intent and proof.
  // A validator upgrade must never turn a cancelled task into resumable work.
  if(p.work?.phase==='cancelled'&&(p.pending||p.work.check)){
    delete p.pending;delete p.work.check;await saveProject(p);
  }
  // Durable intent precedes publication; an interrupted commit is replayed once.
  if(p.pending&&p.work?.phase!=='cancelled'){
    // Revalidate the durable publication intent, never trust an old process's proof.
    const v=p.pending;cartridge(v.lua,v.gfx,v.audio);await task({op:'syntax',text:v.lua}).promise;
    const diagnostics=deliveryDiagnostics(v.lua,v.audio??BUILTIN_AUDIO);if(diagnostics.errors.length)throw new Error(diagnostics.errors.join('\n'));
    await publishVersion(p,v);return;
  }
  if(p.work?.check&&(p.work.check.validator!==VALIDATOR_VERSION||p.work.check.hash!==draftHash(p))){delete p.work.check;p.work.phase='draft_saved';await saveProject(p);}
  const v=p.versions.find(v=>v.revision===p.revision);
  if(v)await task({op:'publish',name:p.id,title:v.title,text:cartridge(v.lua,v.gfx,v.audio)}).promise;
}
export async function playProject(p:Project) {
  if(p.pending)await recoverProject(p);
  if(!p.revision&&!p.scaffoldReady)throw new Error('先描述需求，生成第一个游戏');
  await saveProject(p); await write('last-project.txt',p.id);
  await task({op:'play',name:p.id}).promise;
}
export async function deleteProject(p:Project){
  await saves.catch(()=>{});
  await task({op:'delete',name:p.id}).promise;
}
export function canResumeProject(p:Project){return !!p.work&&!p.question&&( ['generating','draft_saved','checked','publishing','pending_reply'].includes(p.work.phase)||p.work.phase==='interrupted'&&transientFailure(new Error(p.work.error||'')));}
export function draftHash(p:Project){return contentHash(JSON.stringify({lua:p.lua,title:p.title,summary:p.summary,gfx:p.gfx??BUILTIN_GFX,audio:p.audio??BUILTIN_AUDIO,design:p.design}));}
export async function restoreVersion(p:Project,revision:number){
 const v=p.versions.find(v=>v.revision===revision);if(!v)throw new Error('这个历史版本已不在本机');
 const next={...v,revision:p.revision+1,summary:('恢复第 '+revision+' 版：'+v.summary).slice(0,240)};
 cartridge(next.lua,next.gfx,next.audio);await task({op:'syntax',text:next.lua}).promise;
 p.work={schema:1,phase:'publishing',request:'恢复第 '+revision+' 版',startedAt:Date.now(),updatedAt:Date.now(),revision:p.revision};
 p.pending=next;await saveProject(p);await publishVersion(p,next);p.work.phase='complete';await saveProject(p);
}
export function projectAgent(p:Project,config:Config,onStatus:(text:string)=>void,onChange:()=>void,onEvent?:(event:GameAgentEvent,messages:GameMessage[])=>void,requestText='',onQuestion?:(question:UserQuestion,signal?:AbortSignal)=>Promise<QuestionAnswer>,systemPromptOverride?:string) {
  let historySaving=false,historyDirty=false;
  const saveHistory=()=>{historyDirty=true;if(historySaving)return;historySaving=true;
    void(async()=>{try{while(historyDirty){historyDirty=false;await saveProject(p);}}catch{}finally{historySaving=false;}})();};
  const draftFingerprint=()=>draftHash(p);
  const setPhase=(phase:WorkState['phase'])=>{if(p.work){p.work.phase=phase;p.work.updatedAt=Date.now();}};
  const toolStarted=new Map<string,number>();
  let checkedDraft:string|undefined,mutationAttempted=false,publicationSucceeded=false;
  const publishedVersion=()=>p.versions.find(v=>v.revision===p.revision);
  const differsFromPublished=()=>{const published=publishedVersion();return !published||JSON.stringify({lua:p.lua,title:p.title,summary:p.summary,gfx:p.gfx,audio:p.audio})!==JSON.stringify({lua:published.lua,title:published.title,summary:published.summary,gfx:published.gfx,audio:published.audio});};
  const tools:GameTool[]=[
    {name:'ask_user_question',description:'Ask one meaningful gameplay/design question and wait for the actual user answer. Suggested options are optional; voice free-form and skip are always possible. Use sparingly when a decision materially affects the game; never infer consent from a recommendation. Call alone before changes that depend on the answer.',parameters:QUESTION_PARAMETERS,execute:async(args,signal,id)=>{
      const q=makeQuestion(args,id!);p.question=q;await saveProject(p);onStatus('等待你的选择或语音回答');onChange();
      if(!onQuestion)throw new Error('问题已保存，请回到项目回答');
      const answer=await onQuestion(q,signal);if(signal?.aborted)throw new Error('提问已取消，问题已保留');
      q.answer=answer;await saveProject(p);return answer;
    }},
    {name:'terminal',description:'Run a synchronous JavaScript helper script in an isolated interpreter (not a Linux shell or Node.js). print/console.log, readText("game.lua"), writeText(name,text) and listFiles() are available. The current Lua snapshot and temporary .lua/.js/.txt/.json/.csv files exist. Optional system_files loads read-only snapshots: system/meminfo, system/cpuinfo, system/version, system/uptime, system/loadavg, system/app-status; runtime/launch.sh, runtime/README.md, runtime/build-receipt.txt, runtime/P8-launch.sh; logs/workshop.log, logs/launch-error.txt; skill/<bundled relative resource path>. Each is limited to 64KiB; logs use the tail and redact credential lines. Use listFiles/readText to inspect. No arbitrary paths, OS calls, network, imports, subprocesses, secrets or actual filesystem writes. Limits: 3 seconds, 64MiB interpreter heap, 16KiB script/output, 16 temporary files. Use write_game for actual edits. Can analyze source or test pure algorithms; never claim the game was playtested.',parameters:{type:'object',properties:{script:{type:'string',minLength:1,maxLength:16384},system_files:{type:'array',maxItems:8,uniqueItems:true,items:{type:'string',maxLength:95}}},required:['script'],additionalProperties:false},execute:async(args,signal)=>{
      if(typeof args.script!=='string'||!args.script.trim()||args.script.length>16384)throw new Error('请提供16KiB以内的同步JavaScript脚本');
      if(signal?.aborted)throw new Error('辅助脚本已取消');
      const files:Record<string,string>={};const names=args.system_files??[];
      if(!Array.isArray(names)||names.length>8||names.some(n=>typeof n!=='string'||n.length>95))throw new Error('最多读取8个允许范围内的诊断文件');
      for(const name of names){if(signal?.aborted)throw new Error('文件读取已取消');const read=task({op:'system-read',name}),abort=()=>read.cancel();signal?.addEventListener('abort',abort,{once:true});try{files[name]=(await read.promise).text;}finally{signal?.removeEventListener('abort',abort);}}
      if(signal?.aborted)throw new Error('辅助脚本已取消');
      const job=task({op:'terminal',text:JSON.stringify({lua:p.lua,script:args.script,files})});const abort=()=>job.cancel();signal?.addEventListener('abort',abort,{once:true});
      try{return {stdout:(await job.promise).text,language:'JavaScript',scope:'current game, explicitly allowed read-only system snapshots and temporary memory files',project_changed:false,gameplay_tested:false};}finally{signal?.removeEventListener('abort',abort);}
    }},
    {name:'read_game',description:'Read exact current draft, published revision, draft differences, sprite pixels and audio availability. Use the music Skill module for full tracker data.',parameters:{type:'object',properties:{},additionalProperties:false},execute:async()=>({project_id:p.id,revision:p.revision,title:p.title,lua:p.lua,summary:p.summary,design:p.design??null,work:p.work??null,draft:{differs_from_published:differsFromPublished()},published:{revision:p.revision,title:publishedVersion()?.title??null},assets:{...gameAssets(p.gfx??BUILTIN_GFX),audio_summary:{stored:!!p.audio,sfx_count:(p.audio??BUILTIN_AUDIO).sfx.length,pattern_count:(p.audio??BUILTIN_AUDIO).music.length,details_module:'music'}}})},
    {name:'read_skill',description:'Load one relevant bundled Skill module from the listed names. Read only needed details. Music also returns current project audio; examples are advisory, not the current game. No arbitrary paths.',parameters:{type:'object',properties:{module:{type:'string',enum:SKILL_MODULE_NAMES}},required:['module'],additionalProperties:false},execute:async args=>{
      const module=readSkillModule(String(args.module));
      return module.module==='music'?{...module,project_assets:{project_id:p.id,revision:p.revision,stored:!!p.audio,audio:p.audio??BUILTIN_AUDIO,catalog:AUDIO_CATALOG}}:module;
    }},
    {name:'write_game',description:'Update this same game draft. Pass arguments as a JSON object, never a quoted JSON document. For small changes use edits:[{oldText,newText}] with unique non-overlapping matches against the original Lua, like Pi edit. For creation/restructure use complete lua instead; never combine lua and edits. Title, summary and sprite/audio patches are optional; omitted fields are preserved. Returns actual code diff. Custom sprites 32..63, SFX 16..63, music 8..63.',parameters:{type:'object',minProperties:1,properties:{...EDIT_PROPERTIES,...AUDIO_PATCH_PROPERTIES,design_record:DESIGN_PROPERTIES,lua:{type:'string',minLength:1},title:{type:'string',minLength:1,maxLength:32},summary:{type:'string',maxLength:240},sprite_overrides:{type:'array',maxItems:32,items:{type:'object',properties:{id:{type:'integer',minimum:32,maximum:63},pixels:{type:'array',minItems:8,maxItems:8,items:{type:'string',pattern:'^[0-9a-f]{8}$',description:'Exactly 8 lowercase hex pixels (0-9a-f); transparent=0, never dots or spaces.'}}},required:['id','pixels'],additionalProperties:false}}},additionalProperties:false},execute:async args=>{
      if(!Object.keys(args).length)throw new Error('请提供代码修改、标题、摘要或素材');
      const update=updateLua(p.lua,args),lua=update.lua,title=args.title===undefined?p.title:String(args.title).trim(),summary=args.summary===undefined?p.summary:String(args.summary);
      if(!title||/[\t\r\n]/.test(title)||title[0]=='.'||title.endsWith('.disabled'))throw new Error('请使用正常游戏名称');
      validateLua(lua);const gfx=mergeSprites(p.gfx??BUILTIN_GFX,args.sprite_overrides);const audio=mergeAudio(p.audio??BUILTIN_AUDIO,args.sfx_overrides,args.music_overrides);
      const design=args.design_record===undefined?p.design:args.design_record as DesignRecord;if(design!==undefined&&!validDesign(design))throw new Error('设计记录格式异常');
      const difference=codeDifference(p.lua,lua),previous={lua:p.lua,title:p.title,summary:p.summary,gfx:p.gfx,audio:p.audio,design:p.design,work:p.work?{...p.work}:undefined,updatedAt:p.updatedAt};
      checkedDraft=undefined;Object.assign(p,{lua,title,summary,gfx,audio,design});if(p.work)delete p.work.check;setPhase('draft_saved');
      try{await saveProject(p);}catch(error){Object.assign(p,previous);throw error;}
      onChange();return {draft_saved:true,project_id:p.id,title,mode:update.mode,edit_count:update.edit_count,line_count:lua.split('\n').length,...difference};
    }},
    {name:'check_game',description:'Parse PICO-8 Lua and check cartridge/assets, audio calls and unsupported Lua table/math/string libraries. Rejects unbound standard namespaces, missing BGM/SFX and empty slots; dynamic IDs/custom bindings need source review. Long BGM/feedback reuse warns. Does not execute gameplay or prove quality.',parameters:{type:'object',properties:{},additionalProperties:false},execute:async()=>{
      cartridge(p.lua,p.gfx??BUILTIN_GFX,p.audio??BUILTIN_AUDIO);
      await task({op:'syntax',text:p.lua}).promise;
      const delivery=deliveryDiagnostics(p.lua,p.audio??BUILTIN_AUDIO);
      if(delivery.errors.length)throw new Error(delivery.errors.join('\n'));
      checkedDraft=draftFingerprint();if(p.work)p.work.check={hash:checkedDraft,validator:VALIDATOR_VERSION,checkedAt:Date.now()};setPhase('checked');await saveProject(p);return {format_checked:true,syntax_checked:true,gameplay_tested:false,delivery};
    }},
    {name:'publish_game',description:'Publish the draft only after a successful check of its exact current contents. Any write invalidates that check. Update the same project menu game.',parameters:{type:'object',properties:{},additionalProperties:false},execute:async()=>{
      if(checkedDraft!==draftFingerprint())throw new Error('请先成功执行check_game检查当前草稿；写入后需要重新检查');
      checkedDraft=undefined;
      if(!differsFromPublished()){publicationSucceeded=true;setPhase('pending_reply');await saveProject(p);return {published:true,already_published:true,project_id:p.id,revision:p.revision,menu:'AI生成游戏'};}
      const v={revision:p.revision+1,title:p.title,summary:p.summary,lua:p.lua,gfx:p.gfx??BUILTIN_GFX,audio:mergeAudio(p.audio??BUILTIN_AUDIO),design:p.design};cartridge(v.lua,v.gfx,v.audio);
      p.pending=v;setPhase('publishing');await saveProject(p);await publishVersion(p,v);publicationSucceeded=true;checkedDraft=undefined;onChange();return {published:true,project_id:p.id,revision:p.revision,menu:'AI生成游戏'};
    }},
  ];
  const labels:Record<string,string>={terminal:'正在运行辅助脚本',ask_user_question:'正在向你提问',read_skill:'正在加载所需Skill模块',read_game:'正在读取当前游戏',write_game:'正在保存游戏草稿',check_game:'正在检查源码与声音',publish_game:'正在更新游戏目录'};
  const agent=createGameAgent({...config,provider:generationProvider(config),modelId:config.model,systemPrompt:systemPromptOverride??pico8SystemPrompt(p.revision>0||p.lua!==STARTER),messages:p.messages,tools,request,requestStream,wait:delay,maxTurns:MAX_AGENT_TURNS,maxOutputTokens:65536,
    context:{latest:p.compactions?.[p.compactions.length-1],contextWindow:config.contextWindowTokens,reserveTokens:config.contextReserveTokens,keepRecentTokens:config.contextKeepRecentTokens,
      yieldFrame:()=>delay(0),onStart:()=>onStatus('正在整理较早对话，可以按 X 停止'),
      onCommit:async(record,messages)=>{
        const previous=p.compactions;p.messages=messages;p.compactions=[...(previous||[]),record].slice(-8);
        try{await saveProject(p);}catch(error){p.compactions=previous;throw error;}
        onStatus('较早对话已整理，继续修改游戏');
      }},onEvent:event=>{
    if(event.type==='diagnostic'){p.diagnostics=[...(p.diagnostics||[]),event.record].slice(-256);return;}
    if(event.type==='tool_execution_end'){p.diagnostics=[...(p.diagnostics||[]),{kind:'tool_end',at:Date.now(),tool:event.toolName,elapsedMs:Date.now()-(toolStarted.get(event.toolCallId)||Date.now()),...(event.isError?{error:'tool failed'}:{})}].slice(-256);toolStarted.delete(event.toolCallId);}
    if(event.type==='turn_start')onStatus(providerName(generationProvider(config))+' 正在编写，可以按 X 取消');
    if(event.type==='tool_execution_start'){
      toolStarted.set(event.toolCallId,Date.now());
      // Checking reads the current draft and saves proof; it does not require a new publication.
      if(event.toolName==='write_game'||event.toolName==='publish_game')mutationAttempted=true;
      if(event.toolName==='write_game'||event.toolName==='check_game'){checkedDraft=undefined;if(p.work)delete p.work.check;}
      onStatus(labels[event.toolName]??'正在修改游戏');
    }
    if(event.type==='error')onStatus(event.message);
    if(event.type==='retry')onStatus((event.summary?'对话整理':'模型连接')+'中断 · '+Math.ceil(event.delayMs/1000)+' 秒后重试 '+event.attempt+'/3 · X 停止');
    if(event.type==='message_update'&&event.assistantMessageEvent.type==='thinking_start')onStatus(providerName(generationProvider(config))+' 正在思考 · X 停止');
    if(event.type==='message_update'&&(event.assistantMessageEvent.type==='text_start'||event.assistantMessageEvent.type==='toolcall_start'))onStatus(providerName(generationProvider(config))+' 正在生成 · X 停止');
    // Pi's live state is also the persisted history; the conversation page updates
    // on messages and tool events instead of waiting until the whole task ends.
    p.messages=agent.messages;
    if(event.type==='tool_execution_end'&&event.toolName==='ask_user_question'&&!event.isError&&p.question?.answer)delete p.question;
    onEvent?.(event,p.messages);
    // Read-only turns update the live UI immediately; the next mutation or the
    // prompt's final save includes those traces. Persist mutation results once
    // in addition to each tool's own durable draft/publication checkpoint.
    if(event.type==='message_end'&&event.message.role==='user'||event.type==='turn_end'&&event.message.content.some(c=>c.type==='toolCall'&&['write_game','publish_game','ask_user_question'].includes(c.name)))saveHistory();
  }});
  const run=async(text:string,resuming=false)=>{
    p.diagnostics=[...(p.diagnostics??[]),{kind:'environment',at:Date.now(),validator:VALIDATOR_VERSION,skillHash:contentHash(JSON.stringify(PICO8_SKILL_MANIFEST)),model:config.model,provider:generationProvider(config)}].slice(-256);
    checkedDraft=undefined;mutationAttempted=false;publicationSucceeded=resuming&&p.work?.phase==='pending_reply';
    const revision=p.revision;
    if(!resuming){if(p.work?.phase==='cancelled')delete p.pending;p.work={schema:1,phase:'generating',request:text.slice(0,16000),startedAt:Date.now(),updatedAt:Date.now(),revision};}
    if(!p.work)throw new Error('没有可恢复的任务');
    if(resuming&&p.work.check?.validator===VALIDATOR_VERSION&&p.work.check.hash===draftFingerprint())checkedDraft=p.work.check.hash;

    await saveProject(p);
    try{await agent.prompt(text);}finally{
      p.messages=agent.messages;const last=p.messages[p.messages.length-1];
      if(last?.role==='assistant'&&last.stopReason==='aborted'){setPhase('cancelled');delete p.pending;delete p.work.check;}
      else if(last?.role==='assistant'&&last.stopReason==='error'){setPhase(publicationSucceeded?'pending_reply':'interrupted');p.work.error=(last.errorMessage||'模型请求失败').slice(0,600);}
      else setPhase(p.pending?'publishing':mutationAttempted&&!publicationSucceeded?(p.work.check?'checked':'draft_saved'):'complete');
      const metrics=(globalThis as any).brick?.serviceMetrics?.();
      if(metrics)p.diagnostics=[...(p.diagnostics??[]),{kind:'runtime_since_boot',at:Date.now(),frameMaxMs:metrics.frameMaxMs,peakRssKiB:metrics.peakRssKiB,httpPeakBufferBytes:metrics.httpPeakBufferBytes}].slice(-256);
      await saveProject(p);onChange();
    }
    const last=p.messages[p.messages.length-1];
    if(last?.role==='assistant'&&(last.stopReason==='error'||last.stopReason==='aborted')){
      if(last.stopReason==='error'&&publicationSucceeded)throw new PublishedReplyError(p.revision,last.errorMessage||'模型请求失败');
      throw new Error(last.errorMessage||'本次修改已取消');
    }
    if(p.revision===revision&&mutationAttempted&&!publicationSucceeded)throw new Error('这次需求已记录，尚未提交新的游戏版本');
    onStatus(p.revision===revision?'可以继续说想法':'第 '+p.revision+' 版已保存，按 SR 试玩');
  };
  return {abort:()=>agent.abort(),prompt:(text:string)=>run(text),async resume(){
    if(!canResumeProject(p))return;
    const state=p.work!;
    const recovery='[宿主恢复中断任务] 原需求：'+state.request+'。已保存阶段：'+state.phase+'，已发布第 '+p.revision+' 版。先 read_game 核对实际状态；保留已完成修改，只补缺失检查、发布或完成说明；不要重新生成同样的代码或重复增加版本。';
    await run(recovery,true);
  }};
}
