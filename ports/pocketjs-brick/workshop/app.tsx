import {displayText} from '../app/display-text.ts';
import {createSignal,createRenderEffect,onCleanup,For,Show,type JSX} from 'solid-js';
import {mount} from '@pocketjs/framework/solid';
import {View,Text,Image} from '@pocketjs/framework/solid/components';
import {onFrame} from '@pocketjs/framework/solid/lifecycle';
import {BTN} from '@pocketjs/framework/input';
import {getOps,reportAppAction} from '@pocketjs/framework/host';
import {installServices,native,read,write,prepare,prepareWindow,task,hasSharedCredential,delay} from '../app/services.ts';
import {shareProject,qrMatrix} from './sharing.ts';
import {newProject,canResumeProject,hasDraftChanges,restoreVersion,initializeProject,answerSavedQuestion,listProjects,loadProject,loadReturnView,saveReturnView,PublishedReplyError,projectInfo,saveProject,recoverProject,playProject,deleteProject,projectAgent,type Project,type Config} from './model.ts';
import type {ReturnView} from './return-view.ts';
import {mix} from './conversation.ts';
import {Transcript,type TranscriptRow,type TranscriptPage,type Position} from './transcript.ts';
import {STRONG,EMPHASIS,CODE,LINK,STRIKE} from './markdown.ts';
import type {UserQuestion,QuestionAnswer} from './questions.ts';
import type {GameMessage} from './agent.ts';
import {GENERATION_MODELS,loadGenerationConfig,selectGenerationModel,modelChoice,modelLabel,generationProvider,providerName} from './generation-models.ts';
import {createAvatar,avatarState,type AvatarState} from './avatar.ts';
interface VoiceState {state:string;session?:number;seconds?:number;rms?:number;text?:string;error?:string}
interface Info {theme:{main:string;accent:string;accent2:string;listText:string;selectedText:string;hint:string;background:string};existingKey:string;resume:boolean;clock():number;qr(bits:string,size:number):number;icon(name:string,color:string):number;ring(step:number,color:string):number;avatar(frame:unknown,ink:string,detail:string,paper:string,still:boolean):number;avatarFrames():number;fonts(text:string):boolean;image(id:string):{handle:number;source:string}|null;voice:{snapshot():VoiceState;command(op:string):boolean}}
const info=(native as typeof native & {workshop:Info}).workshop;
const c=info.theme,bg=c.background,ink=c.listText,muted=c.hint,accent=c.main,selectedInk=c.selectedText;
const line=mix(bg,ink,.13),soft=mix(bg,accent,.09);
const WORDS='耗时 代码参数 字符 活动 秒前 分享 扫码 在线游玩 重新上传 历史版本 恢复 草稿改动 已发布 无历史版本 重试 版本 上传 手机 最新 生成中 abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,:;/()[]_-+↑↓·—… 游戏生成器 我的游戏 个项目 新游戏 尚未生成 第版 正在创作 已保存 按住说话 松开识别 等待语音 正在识别 正在听你说话 最长秒 没有项目 从一句想法开始 按创建你的第一个游戏 打开 新建 返回 退出 试玩 语音 取消 停止 删除 长按 项目与游戏已删除 详情 工具 上下翻页 你 阅读历史 最新 读取当前游戏 局部修改 处 更新信息与素材 代码差异 参数 加载开发参考 玩法与修复 三维开发 像素绘制 音乐与音效 社区来源 收集示例 躲避示例 贪吃蛇示例 平台示例 推箱子示例 第一人称示例 三维线框示例 写入游戏草稿 检查卡带格式 发布游戏版本 完成 进行中 失败 未完成 已加入 格式通过 代码 进度已保存 当前项目 首帧未就绪 你的游戏，从这里开始 按住 A 说话，松开后交给 Agent 可以继续修改 说出你的游戏想法 遇到问题 生成模型 全局设置 当前模型 已启用 待保存 旗舰 百炼 用于所有项目的游戏生成与对话摘要 上下选择 保存 选择 › 玩法共创 基础脚手架 可试玩 等待你的选择或语音回答 选/长按说 跳过 按住 A，用语音回答';
const clip=(raw:string,n:number)=>{const s=displayText(raw);return Array.from(s).length>n?Array.from(s).slice(0,n-1).join('')+'…':s;};
type Row=TranscriptRow;
function App(){
 installServices();
 if(!info.fonts(WORDS))throw new Error('字体加载失败，请重新安装完整应用包');
 const [ready,setReady]=createSignal(false),[busy,setBusy]=createSignal(false),[page,setPage]=createSignal<'list'|'conversation'|'models'|'versions'|'share'>('list');
 const [config,setConfig]=createSignal<Config>(loadGenerationConfig({},info.existingKey)),[choice,setChoice]=createSignal(1);
 const [projects,setProjects]=createSignal<Project[]>([]),[index,setIndex]=createSignal(0),[epoch,setEpoch]=createSignal(0);
 const [rows,setRows]=createSignal<Row[]>([]),[row,setRow]=createSignal(0),[status,setStatus]=createSignal('正在读取项目…');
 const [historyCount,setHistoryCount]=createSignal(0),[historyBlock,setHistoryBlock]=createSignal(0);
 const [voice,setVoice]=createSignal<VoiceState>({state:'idle'}),[levels,setLevels]=createSignal(Array(24).fill(4));
 const [deleteProgress,setDeleteProgress]=createSignal(0),[agentRunning,setAgentRunning]=createSignal(false);
 const [taskSeconds,setTaskSeconds]=createSignal<number|undefined>();
 const [question,setQuestion]=createSignal<UserQuestion|undefined>(),[questionChoice,setQuestionChoice]=createSignal(0),[questionLines,setQuestionLines]=createSignal<string[]>([]),[questionDetails,setQuestionDetails]=createSignal(false);
 const [versionChoice,setVersionChoice]=createSignal(0),[qr,setQr]=createSignal<{handle:number;pixels:number;url:string;revision:number}|undefined>();
 const [sharing,setSharing]=createSignal(false);let shareAbort:AbortController|undefined;
 let answerLive:((answer:QuestionAnswer)=>void)|undefined,questionPress=0;
 const [covers,setCovers]=createSignal<Record<string,{handle:number;source:string;revision:number}>>({}),[expanded,setExpanded]=createSignal(false);
 let current:Project|undefined,agent:ReturnType<typeof projectAgent>|undefined,lastProject:Project|undefined;
 let old=0,held=0,direction=0,frame=0,holding=false,voiceOwner='',awaitingVoice=false,seenSession=-1,failed=false;
 let streamStarted=0,streamCharacters=0,streamActivity=0,streamStatusAt=0;
 let taskStarted:number|undefined;
 const taskElapsed=()=>{const seconds=taskSeconds()??0;return '耗时 '+Math.floor(seconds/60).toString().padStart(2,'0')+':'+(seconds%60).toString().padStart(2,'0');};
 let deleteOwner='',deleteStarted=0,playRequests=0;
 let portrait=createAvatar(),outcome:AvatarState='idle',outcomeUntil=0,avatarTestState:AvatarState|undefined,avatarTestClock=0,promptCancelled=false;
 const avatarNodes=new Set<number>();
 let layoutGeneration=0,layoutWanted=false,layoutRunning=false,statusJobs=Promise.resolve(),coverJobs=Promise.resolve(),fontTitles='';
 const active=new Set<string>();
 const coverPending=new Set<string>();
 const transcript=new Transcript(prepareWindow);
 let indexing=false;
 let visible:TranscriptPage|undefined,followLatest=true,navigation:{position:Position;reverse:boolean}|undefined,streamingMessage:GameMessage|undefined;
 let layoutTime=-Infinity,urgentLayout=false,lastInputBusy=false,layoutCount=0;
 let returnPreview=false,returnHydrated=false,returnShownAt=0,returnLoadedAt=0;
 const startedAt=info.clock(),removed=new Set<string>();
 let paintWait:{frames:number;resolve:()=>void}|undefined;
 const afterPaint=()=>new Promise<void>(resolve=>{paintWait={frames:2,resolve};});
 const title=()=>{epoch();return current?.title||'新游戏';};
 const draftChanged=()=>{epoch();return current?hasDraftChanges(current):false;};
 const historyVersions=()=>{epoch();return current?.versions.slice().reverse()||[];};
 const playable=()=>{epoch();return !!current&&(current.revision>0||!!current.scaffoldReady);};
 const revision=()=>{epoch();return current?.revision||0;};
 const inputBusy=()=>{const state=voice().state;return holding||state==='recording'||state==='processing';};
 const canStop=()=>inputBusy()||agentRunning()||sharing();
 const updateFonts=()=>{const titles=projects().map(p=>p.title).join('')+(question()?.options.map(o=>clip(o.label,25)).join('')||'');if(titles!==fontTitles){fontTitles=titles;info.fonts(WORDS+titles);}};
 const say=(text:string)=>{statusJobs=statusJobs.catch(()=>{}).then(async()=>{await prepare(text);setStatus(text);});};
 const fail=(e:unknown)=>{failed=true;outcome='error';outcomeUntil=info.clock()+2400;say(e instanceof Error?e.message:'操作失败');};
 const cover=(p:Project)=>{
  const id=p.id,revision=p.revision,key=id+':'+revision;
  if((!revision&&!p.scaffoldReady)||covers()[id]?.revision===revision||coverPending.has(key))return;coverPending.add(key);
  coverJobs=coverJobs.catch(()=>{}).then(async()=>{try{await task({op:'cover',name:id}).promise;const image=info.image(id);setCovers(all=>{const next={...all};if(image)next[id]={...image,revision};else delete next[id];return next;});}finally{coverPending.delete(key);}});
 };
 const changed=()=>{setEpoch(v=>v+1);setProjects(all=>all.map(p=>p.id===current?.id?projectInfo(current!):p));updateFonts();if(current&&ready())cover(current);requestLayout();};
 function requestLayout(urgent=false){layoutWanted=true;urgentLayout=urgentLayout||urgent;}
 async function layout(){
  if(indexing||layoutRunning||!layoutWanted||!current||page()!=='conversation')return;layoutRunning=true;layoutWanted=false;urgentLayout=false;layoutTime=info.clock();
  const id=current.id,generation=layoutGeneration;
  transcript.sync(current.messages);transcript.setLive(streamingMessage);
  const details=expanded(),nav=navigation;navigation=undefined;
  const position=nav?.position??(followLatest?transcript.tail(details):visible?.start??transcript.oldest()),reverse=nav?.reverse??followLatest;
  const valid=()=>id===current?.id&&generation===layoutGeneration&&page()==='conversation';
  try{
   const next=await transcript.page(position,reverse,inputBusy()?352:468,details,active,valid);
   if(valid()&&next.rows.length){visible=next;setRows(next.rows);setRow(Math.max(0,next.start.block)*100000+Math.max(0,Math.min(99999,next.start.offset)));setHistoryBlock(Math.max(0,next.start.block));}
   if(valid()){setHistoryCount(transcript.length);layoutCount++;}
  }catch(e){if(valid())fail(new Error('工坊消息排版失败：'+(e instanceof Error?e.message:'显示服务错误')));}finally{layoutRunning=false;}
 }
 const open=async(p:Project,view?:ReturnView)=>{
  if(!busy()&&!inputBusy()){taskStarted=undefined;setTaskSeconds(undefined);}
  if(busy()||inputBusy())return;setBusy(true);failed=false;outcome='idle';current=p;lastProject=p;layoutGeneration++;active.clear();transcript.clear();visible=undefined;streamingMessage=undefined;followLatest=true;navigation=undefined;setExpanded(false);setQuestion(undefined);setQuestionDetails(false);setHistoryCount(0);setRows([]);setRow(0);setPage('conversation');changed();
  try{
   if(view){
    await prepare(view.rows.map(r=>r.text).join('\n')+' 正在恢复完整对话 语音 试玩 返回 详情');
    setRows(view.rows);setStatus('正在恢复完整对话');returnPreview=true;returnShownAt=info.clock()-startedAt;setReady(true);
    // Show a real cached frame before reading/parsing the full project.
    await afterPaint();
   }
   p=await loadProject(p);indexing=true;current=p;lastProject=p;
   try{await transcript.hydrate(p.messages,()=>delay(0));}finally{indexing=false;}
   if(view){returnHydrated=true;returnLoadedAt=info.clock()-startedAt;}
   changed();requestLayout(true);await layout();
   await recoverProject(p);await write('last-project.txt',p.id);await prepare(p.title);changed();requestLayout(true);if(p.question)await showQuestion(p.question);say(p.question?'等待你的选择或语音回答':p.revision?'按住 A，说说接下来想改什么':'按住 A，描述你想做的游戏');
  }
  catch(e){fail(e);}finally{setBusy(false);if(current?.question?.answer)void answerQuestion(current.question.answer);else if(!failed&&current&&canResumeProject(current))void send('恢复已保存的任务',true);}
 };
 const create=async()=>{
  if(busy()||inputBusy())return;failed=false;setBusy(true);const p=newProject();
  try{await initializeProject(p);setProjects(all=>[projectInfo(p),...all]);setIndex(0);setBusy(false);await open(p);}catch(e){fail(e);}finally{setBusy(false);}
 };
 const send=async(text:string,resuming=false)=>{
  if(!current||busy())return;text=text.trim();if(!text){say('没有听清，请再说一次');return;}
  if(current.summaryOnly){say('完整项目尚未恢复，请重新打开项目');return;}
  failed=false;promptCancelled=false;outcome='idle';setBusy(true);setPage('conversation');followLatest=true;active.clear();say('Agent 正在创作 · X 取消');
  try{
   const settings=config();
   if(!settings.key&&!await hasSharedCredential(settings.endpoint)){say('请在 ai-keys.txt 配置 '+(generationProvider(settings)==='bailian'?'DASHSCOPE_API_KEY':'DEEPSEEK_API_KEY'));return;}
   taskStarted=info.clock();setTaskSeconds(0);
   agent=projectAgent(current,settings,say,changed,(event)=>{
    if(event.type==='turn_start'){streamStarted=info.clock();streamCharacters=0;streamActivity=streamStarted;}
    if(event.type==='retry'){streamCharacters=0;streamStarted=info.clock();}
    if(event.type==='message_update'){
     const delta=event.assistantMessageEvent;streamActivity=info.clock();
     if(delta.type==='toolcall_delta'&&event.message.content[delta.contentIndex]?.type==='toolCall'&&(event.message.content[delta.contentIndex] as any).name==='write_game')streamCharacters+=delta.delta.length;
    }
    if(event.type==='tool_execution_start'){streamCharacters=0;}
    if(event.type==='tool_execution_start')active.add(event.toolCallId);
    if(event.type==='tool_execution_end')active.delete(event.toolCallId);
    if(event.type==='message_update')streamingMessage=event.message;
    if(event.type==='message_end')streamingMessage=undefined;
    if(event.type==='message_update'||event.type==='message_end'||event.type==='message_start'||event.type==='tool_execution_start'||event.type==='tool_execution_end')requestLayout();
   },text,waitForAnswer);setAgentRunning(true);if(resuming)await agent.resume();else await agent.prompt(text);outcome='done';outcomeUntil=info.clock()+1600;changed();
  }catch(e){if(promptCancelled){outcome='idle';say('已取消，进度已保存');}else if(e instanceof PublishedReplyError){outcome='done';outcomeUntil=info.clock()+1600;say(e.message);}else fail(e);}finally{if(taskStarted!==undefined){setTaskSeconds(Math.max(0,Math.floor((info.clock()-taskStarted)/1000)));taskStarted=undefined;}agent=undefined;streamingMessage=undefined;setAgentRunning(false);active.clear();setBusy(false);if(!promptCancelled&&current?.question&&!question())void showQuestion(current.question);requestLayout(true);}
 };
 async function showQuestion(q:UserQuestion){
  setQuestion(q);setQuestionChoice(0);setQuestionDetails(false);updateFonts();
  const page=await prepareWindow(q.question,3);
  if(question()?.id===q.id)setQuestionLines(page.lines.map((l:any)=>typeof l==='string'?l:l.text));
  await prepare(q.options.map(o=>clip(o.label,25)+clip(o.description||'',29)).join(''));
 }
 function waitForAnswer(q:UserQuestion,signal?:AbortSignal):Promise<QuestionAnswer>{
  void showQuestion(q);
  return new Promise((resolve,reject)=>{
   const abort=()=>{answerLive=undefined;reject(new Error('提问已取消，问题已保留'));};
   if(signal?.aborted){abort();return;}
   signal?.addEventListener('abort',abort,{once:true});
   answerLive=answer=>{signal?.removeEventListener('abort',abort);answerLive=undefined;setQuestion(undefined);resolve(answer);};
  });
 }
 async function answerQuestion(answer:QuestionAnswer){
  if(!question()||!current)return;
  if(answerLive){answerLive(answer);return;}
  try{setBusy(true);await answerSavedQuestion(current,answer);setQuestion(undefined);setBusy(false);await send('根据刚才对问题的实际回答继续。');}catch(e){setBusy(false);fail(e);}
 }
 function selectAnswer(){const q=question(),o=q?.options[questionChoice()];if(o)void answerQuestion({answer:o.label+(o.description?'：'+o.description:'')});else say('按住 A，用语音回答这个问题');}
 const play=async(exit=true,p:Project|undefined=current)=>{
  if(!p||busy()||inputBusy())return;failed=false;setBusy(true);
  try{
   p=current?.id===p.id&&!current.summaryOnly?current:await loadProject(p);
   const latest=current===p?transcript:new Transcript(prepareWindow);latest.sync(p.messages);
   const view=await latest.page(latest.tail(false),true,468,false,new Set());
   await playProject(p);
   // Optional display cache failure cannot prevent playing the durable game.
   await saveReturnView(p,view.rows).catch(()=>{});
   playRequests++;say('已保存，正在打开游戏');if(exit)reportAppAction('app.exit',1);
  }catch(e){fail(e);}finally{setBusy(false);}
 };
 const remove=async(p:Project)=>{
  if(busy()||inputBusy())return;setBusy(true);failed=false;
  try{
   await coverJobs;await deleteProject(p);
   removed.add(p.id);
   setProjects(all=>all.filter(value=>value.id!==p.id));setCovers(all=>{const next={...all};delete next[p.id];return next;});
   if(lastProject?.id===p.id)lastProject=undefined;
   if(current?.id===p.id){current=undefined;layoutGeneration++;layoutWanted=false;setRows([]);setEpoch(v=>v+1);}
   setIndex(v=>Math.max(0,Math.min(v,projects().length-1)));updateFonts();say('项目与游戏已删除');
  }catch(e){fail(e);}finally{setBusy(false);}
 };
 const share=async()=>{
  const selected=projects()[index()];if(!selected||busy()||inputBusy())return;
  resetDelete();setBusy(true);setSharing(true);setPage('share');setQr(undefined);shareAbort=new AbortController();
  try{
   const p=current?.id===selected.id&&!current.summaryOnly?current:await loadProject(selected);
   const url=await shareProject(p,config(),say,shareAbort.signal),matrix=qrMatrix(url);
   if(shareAbort.signal.aborted)throw new Error('分享已取消');
   const handle=info.qr(matrix.bits,matrix.size);if(handle<0)throw new Error('二维码生成失败，请重试');
   setQr({handle,pixels:matrix.pixels,url,revision:p.revision});say('手机扫码，直接在线游玩');
  }catch(e){fail(e);}finally{shareAbort=undefined;setSharing(false);setBusy(false);}
 };
 function openVersions(){
  if(page()!=='conversation'||busy()||inputBusy()||question()||!current)return;
  setVersionChoice(0);setPage('versions');say('选择历史版本 · A 恢复 · B 返回');
 }
 const restore=async()=>{
  const v=historyVersions()[versionChoice()];if(!current||!v||busy())return;setBusy(true);
  try{await restoreVersion(current,v.revision);changed();setPage('conversation');requestLayout(true);say('已恢复为第 '+current.revision+' 版 · SR 试玩');}catch(e){fail(e);}finally{setBusy(false);}
 };
 function resetDelete(){deleteOwner='';deleteStarted=0;setDeleteProgress(0);}
 function cancel(){
  outcome='idle';shareAbort?.abort();
  if(question()){setQuestion(undefined);setQuestionDetails(false);}
  if(agentRunning())promptCancelled=true;
  questionPress=0;holding=false;awaitingVoice=false;voiceOwner='';info.voice.command('cancel');setVoice({state:'idle'});
  if(busy()){agent?.abort();say('正在取消并保存进度');}else say('已取消，可以继续说话');
 }
 function back(){
  if(inputBusy()){cancel();return;}if(question()){void answerQuestion({answer:'用户跳过本次提问，请根据现有要求选择合理默认值。',skipped:true});return;}if(busy()){say('Agent 正在修改 · X 取消');return;}
  if(page()==='share'){info.qr('',0);setQr(undefined);setPage('list');say('选择项目');return;}
  if(page()==='versions'){setPage('conversation');requestLayout(true);return;}
  if(page()==='models'){setPage('list');say('当前模型 · '+modelLabel(config()));return;}
  if(page()==='conversation'){layoutGeneration++;setPage('list');setIndex(Math.max(0,projects().findIndex(p=>p.id===current?.id)));return;}reportAppAction('app.exit',1);
 }
 function openModels(){
  if(page()!=='list'||busy()||inputBusy())return;
  resetDelete();setChoice(Math.max(0,modelChoice(config())));setPage('models');say('上下选择 · A 保存 · B 返回');
 }
 async function saveModel(){
  if(busy())return;setBusy(true);failed=false;
  try{
   // Re-read before saving so external ASR/key changes are retained, too.
   const raw=await read('config.json'),latest=raw?loadGenerationConfig(JSON.parse(raw),info.existingKey):config();
   const next=selectGenerationModel(latest,choice());
   await write('config.json',JSON.stringify(next,null,2));setConfig(next);setPage('list');say('已启用 · '+modelLabel(next));
  }catch(e){fail(e);}finally{setBusy(false);}
 }
 function startVoice(){
  if(page()==='conversation'&&current?.question&&!question()&&!busy()){void showQuestion(current.question);say('等待你的选择或语音回答');return;}
  if(page()!=='conversation'||busy()&&!question()||inputBusy()||!current)return;
  if(current.summaryOnly){say('完整项目尚未恢复，请重新打开项目');return;}
  const s=info.voice.snapshot();if(s.error?.includes('语音服务未就绪')){info.voice.command('retry');say('正在重新启动语音服务');return;}
  holding=info.voice.command('start');if(holding){awaitingVoice=true;voiceOwner=current.id;seenSession=s.session??-1;setLevels(Array(24).fill(4));say('正在听你说话 · 松开 A 结束 · X 取消');}
  else say('语音服务未就绪，请稍后再试');
 }
 onFrame(buttons=>{
  frame++;if(paintWait&&!--paintWait.frames){const resolve=paintWait.resolve;paintWait=undefined;resolve();}
  const pressed=buttons&~old,released=old&~buttons;old=buttons;if(!ready())return;
  if(pressed&BTN.CROSS)back();
  else if(pressed&BTN.TRIANGLE){if(page()==='list')void create();else if(canStop())cancel();}
  else if(pressed&BTN.CIRCLE){if(page()==='models')void saveModel();else if(page()==='versions')void restore();else if(page()==='share'){if(!busy()){setPage('list');void share();}}else if(page()==='list'){const p=projects()[index()];if(p)void open(p);else void create();}else if(question()){questionPress=info.clock();}else startVoice();}
  if(questionPress&&buttons&BTN.CIRCLE&&info.clock()-questionPress>=280){questionPress=0;startVoice();}
  if(released&BTN.CIRCLE&&questionPress){questionPress=0;selectAnswer();}
  if(released&BTN.CIRCLE&&holding){holding=false;info.voice.command('stop');setVoice(s=>({...s,state:'processing'}));say('正在识别，完成后交给 Agent');}
  if(pressed&BTN.START&&(page()==='list'||page()==='conversation'))void play(!native.testing,page()==='list'?projects()[index()]:current);
  if(pressed&BTN.SELECT&&page()==='list')openModels();
  if(pressed&BTN.RTRIGGER&&page()==='list')void share();
  if(pressed&BTN.LTRIGGER&&page()==='conversation')openVersions();
  if(pressed&BTN.SQUARE&&page()==='list'&&!busy()&&!inputBusy()){const p=projects()[index()];if(p){deleteOwner=p.id;deleteStarted=info.clock();setDeleteProgress(.001);}}
  if(released&BTN.SQUARE)resetDelete();
  if(page()==='conversation'&&!inputBusy()&&question()&&pressed&BTN.SELECT){setQuestionDetails(v=>!v);const position=transcript.toolPosition(question()!.id);if(position){setExpanded(true);followLatest=false;navigation={position,reverse:false};layoutGeneration++;requestLayout(true);}}
  if(page()==='conversation'&&!busy()&&!inputBusy()&&!question()){
   if(pressed&BTN.SELECT){setExpanded(v=>!v);followLatest=false;navigation={position:{block:visible?.start.block??0,offset:-1},reverse:false};layoutGeneration++;requestLayout(true);}
  }
  const d=buttons&BTN.UP?-1:buttons&BTN.DOWN?1:buttons&BTN.LEFT?-9:buttons&BTN.RIGHT?9:0;
  if(d!==direction){direction=d;held=0;}else if(d)held++;
  if(d&&(held===0||held>18&&held%5===0)){
   if(page()==='models'){if(!busy())setChoice(v=>Math.max(0,Math.min(GENERATION_MODELS.length-1,v+(d<0?-1:1))));}
   else if(page()==='versions'){if(!busy())setVersionChoice(v=>Math.max(0,Math.min(historyVersions().length-1,v+(d<0?-1:1))));}
   else if(page()==='list')setIndex(v=>Math.max(0,Math.min(Math.max(0,projects().length-1),v+(d<0?-1:1))));
   else if(question()&&!questionDetails()){setQuestionChoice(v=>Math.max(0,Math.min(Math.max(0,question()!.options.length-1),v+(d<0?-1:1))));}
   else if(visible){
    if(d===9){followLatest=true;navigation=undefined;}
    else if(d===-9){followLatest=false;navigation={position:transcript.oldest(),reverse:false};}
    else if(d<0){followLatest=false;navigation={position:visible.start,reverse:true};}
    else{followLatest=transcript.atEnd(visible.end,expanded());navigation=followLatest?undefined:{position:visible.end,reverse:false};}
    layoutGeneration++;requestLayout(true);
   }
  }
  if(deleteOwner){
   const selected=projects()[index()];
   if(page()!=='list'||busy()||!(buttons&BTN.SQUARE)||selected?.id!==deleteOwner||d)resetDelete();
   else{const progress=Math.min(1,(info.clock()-deleteStarted)/1200);setDeleteProgress(progress);if(progress>=1){resetDelete();void remove(selected);}}
  }
  if(taskStarted!==undefined){const seconds=Math.max(0,Math.floor((info.clock()-taskStarted)/1000));if(seconds!==taskSeconds())setTaskSeconds(seconds);}
  if(agentRunning()&&streamCharacters>0&&info.clock()-streamStatusAt>=1000){streamStatusAt=info.clock();say('代码参数 '+Math.ceil(streamCharacters/1024)+'K字符 · '+Math.floor((info.clock()-streamStarted)/1000)+'秒 · 活动 '+Math.floor((info.clock()-streamActivity)/1000)+'秒前');}
  if(frame%6===0){
   const s=info.voice.snapshot();setVoice(s);
   if(s.state==='recording'){setLevels(v=>[...v.slice(1),4+Math.min(1,(s.rms||0)/6500)*56]);}
   if(awaitingVoice&&s.state==='error'){holding=false;awaitingVoice=false;voiceOwner='';fail(new Error(s.error||'语音识别失败，请再试一次'));}
   if(awaitingVoice&&s.state==='result'&&s.text&&s.session!==seenSession){
    seenSession=s.session??-1;holding=false;awaitingVoice=false;setVoice({state:'idle'});const owner=voiceOwner;voiceOwner='';
    if(owner===current?.id&&page()==='conversation')void (question()?answerQuestion({answer:s.text}):send(s.text));info.voice.command('cancel');
   }
  }
  if(inputBusy()!==lastInputBusy){lastInputBusy=inputBusy();requestLayout(true);}
  if(page()==='conversation'&&layoutWanted&&(urgentLayout||info.clock()-layoutTime>=180))void layout();
  if(avatarNodes.size){
   const now=info.clock();if(now>=outcomeUntil)outcome='idle';
   const state=avatarTestState??avatarState(holding?'recording':voice().state,agentRunning()&&!question(),question()?0:active.size,outcome);
   if(avatarTestState)avatarTestClock+=1000/24;
   const sample=portrait.tick(state,avatarTestState?avatarTestClock:now);
   if(sample){const handle=info.avatar(sample,accent,c.accent2,bg,false);if(handle>=0)for(const node of avatarNodes)getOps().setImage(node,handle);}
  }
 });
 void(async()=>{try{
  const [raw,idRaw]=await Promise.all([read('config.json'),read('last-project.txt')]);
  const id=idRaw.trim();
  if(raw)setConfig(loadGenerationConfig(JSON.parse(raw),info.existingKey));else await write('config.json',JSON.stringify(config(),null,2));
  portrait=createAvatar(config().reducedMotion===true);
  const cached=info.resume?await loadReturnView(id):undefined;
  if(cached){setProjects([cached.project]);updateFonts();await open(cached.project,cached.view);}
  await prepare(WORDS+'按住 A，描述你想做的游戏 按住 A，说说接下来想改什么 Agent 正在创作 · X 取消 正在识别，完成后交给 Agent');
  const all=await listProjects();
  setProjects(existing=>{
   const merged=new Map(all.filter(p=>!removed.has(p.id)).map(p=>[p.id,p]));
   for(const p of existing)if(!removed.has(p.id)&&(!merged.has(p.id)||p.updatedAt>=merged.get(p.id)!.updatedAt))merged.set(p.id,p);
   return [...merged.values()].sort((a,b)=>b.updatedAt-a.updatedAt);
  });updateFonts();lastProject=current??all.find(p=>p.id===id);setIndex(Math.max(0,projects().findIndex(p=>p.id===(current?.id??id))));
  if(!cached){if(info.resume&&lastProject)await open(lastProject);else say(all.length?'选择项目 · 长按 Y 删除':'按 X 新建，从一句想法开始');}
  // Returning to a conversation takes priority over unrelated cover jobs.
  for(const p of projects())cover(p);
  const error=await read('launch-error.txt');if(error){await write('launch-error.txt','');say(error);}
 }catch(e){fail(e);}finally{setReady(true);}})();
 let stressDone=false,stressOk=false,stressCount=0,stressLoadMs=0;
 async function historyStress(mib:number){
  try{
   let p=newProject();p.title='独立长历史测试';const count=5000,body='history '.repeat(Math.ceil(mib*1024*1024/count/8));
   p.messages=Array.from({length:count},(_,i)=>({role:'user' as const,content:i+':'+body,timestamp:Date.now()}));
   const started=Date.now();await saveProject(p);const first=JSON.parse(await read('projects/'+p.id+'.json'));
   if(first.messages.length||first.history.count!==count)throw Error('长历史未分段');
   const openStarted=Date.now();p=await loadProject(projectInfo(p));stressLoadMs=Date.now()-openStarted;
   if(p.messages.length!==count||p.messages[0].role!=='user'||p.messages[count-1].role!=='user')throw Error('历史恢复不完整');
   p.messages.push({role:'user',content:'APPEND-END',timestamp:Date.now()});await saveProject(p);
   const second=JSON.parse(await read('projects/'+p.id+'.json'));
   if(second.history.count!==count+1||second.history.pages.length!==first.history.pages.length+1||first.history.pages.some((v:any,i:number)=>v.hash!==second.history.pages[i].hash))throw Error('追加改写了旧历史');
   current=p;lastProject=p;setProjects([projectInfo(p)]);setPage('conversation');indexing=true;try{await transcript.hydrate(p.messages,()=>delay(0));}finally{indexing=false;}followLatest=true;navigation=undefined;requestLayout(true);stressCount=p.messages.length;
   const metrics=native.metrics();await write('history.json',JSON.stringify({mib,count:stressCount,pages:second.history.pages.length,elapsedMs:Date.now()-started,loadMs:stressLoadMs,...metrics}));
   stressOk=true;say('长历史保存、重开和追加通过');
  }catch(e){fail(e);}finally{stressDone=true;}
 }
 let terminalTestDone=false,terminalTestOk=false;
 async function terminalTest(){
  try{
   if(native.sha256?.('abc')!=='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')throw Error('原生SHA256校验失败');
   const diagnostic=await task({op:'system-read',name:'system/meminfo'}).promise;
   if(!diagnostic.text.includes('MemTotal:'))throw Error('诊断快照不完整');
   let refused=false;try{await task({op:'system-read',name:'../config.json'}).promise;}catch{refused=true;}
   if(!refused)throw Error('越界路径未拒绝');
   const test=await task({op:'terminal',text:JSON.stringify({lua:'x=1',files:{'system/meminfo':diagnostic.text},script:'print(readText("game.lua"),readText("system/meminfo").includes("MemTotal:"),typeof process,typeof fetch);writeText("test.json","ok");print(listFiles().includes("test.json"));'})}).promise;
   if(!test.text.includes('x=1 true undefined undefined')||!test.text.includes('true'))throw Error('脚本快照或权限异常');
   const job=task({op:'terminal',text:JSON.stringify({lua:'x=1',script:'while(true){}'})});await delay(150);job.cancel();
   let stopped=false;try{await job.promise;}catch{stopped=true;}if(!stopped)throw Error('脚本取消失败');
   terminalTestOk=true;say('安全脚本与只读系统快照通过');
  }catch(e){fail(e);}finally{terminalTestDone=true;}
 }
 (globalThis as Record<string,unknown>).__workshopAcceptance=(op:number)=>{
  if(op>=90&&op<=92){void historyStress([10,30,60][op-90]);return 1;}if(op===93)return stressDone?1:0;if(op===94)return stressOk?1:0;if(op===95)return stressCount;if(op===96)return stressLoadMs;
  if(op===84){console.log('share acceptance: qr='+!!qr()+' failed='+failed+' status='+status());return 1;}if(op===80){void share();return 1;}if(op===81)return qr()?1:0;if(op===82){openVersions();return 1;}if(op===83)return historyVersions().length;
  if(op===86){void terminalTest();return 1;}if(op===87)return terminalTestDone?1:0;if(op===88)return terminalTestOk?1:0;
  if(op===0)return ready()?1:0;if(op===1){void create();return 1;}if(op===2){void send('做一个用方向键移动圆点的游戏。');return 1;}
  if(op===3)return busy()?0:1;if(op===4)return current?.revision??-1;if(op===5){void play(false);return 1;}
  if(op===6){void send('继续修改这个游戏，把玩家移动速度加快。');return 1;}if(op===7)return current?1:0;if(op===8)return failed?1:0;
  if(op===9){if(!lastProject)return 0;void open(lastProject);return 1;}if(op===10)return page()==='list'?0:page()==='models'?2:1;
  if(op===11)return row();if(op===12)return historyCount();if(op===13)return layoutWanted||layoutRunning?0:1;
  if(op===14)return Object.keys(covers()).length;if(op===15)return awaitingVoice?1:0;if(op===16)return projects().length;if(op===17)return coverPending.size?0:1;if(op===18)return Math.round(deleteProgress()*100);if(op===19)return canStop()?1:0;if(op===20)return playRequests;if(op===21)return index();if(op===22)return choice();if(op===23)return modelChoice(config());
  if(op===24)return info.avatarFrames();if(op===25)return rows().filter(item=>item.kind==='heading').length;if(op===26)return config().reducedMotion===true?1:0;if(op===27)return avatarNodes.size;
  if(op===40)return transcript.stats().indexed;if(op===41)return transcript.stats().wrapJobs;if(op===42)return rows().length;if(op===43)return native.metrics().atlasUploads;if(op===44)return layoutCount;if(op===45)return transcript.stats().textChars;if(op===46)return transcript.stats().windows;if(op===47)return native.metrics().textJobs;
  if(op===50||op===51){const position=transcript.toolPosition('long-error');if(!position)return 0;setExpanded(op===51);followLatest=false;navigation={position,reverse:false};layoutGeneration++;requestLayout(true);return 1;}
  if(op===52)return native.metrics().atlasBuilds;if(op===53)return rows().map(r=>r.text).join('').includes('LONG-END')?1:0;if(op===54)return rows().map(r=>r.text).join('').includes('BEGIN00000')?1:0;
  if(op===55)return streamingMessage?1:0;if(op===56)return rows().map(r=>r.text).join('').includes('STREAM-BEGIN')?1:0;if(op===57)return transcript.stats().bodies;if(op===58)return transcript.stats().largeBodyChars;
  if(op===60)return question()?1:0;if(op===61){selectAnswer();return 1;}if(op===62)return questionChoice();
  if(op===70)return returnPreview?1:0;if(op===71)return returnHydrated?1:0;if(op===72)return Math.round(returnShownAt);if(op===73)return Math.round(returnLoadedAt);
  if(op>=30&&op<=35){if(!avatarTestState)avatarTestClock=info.clock();avatarTestState=(['idle','listening','thinking','working','done','error'] as AvatarState[])[op-30];return 1;}
  if(op===36){avatarTestState=undefined;portrait=createAvatar(config().reducedMotion===true);return 1;}return -1;
 };
 function Label(p:{children:JSX.Element;size?:number;color?:string;width?:number}){const size=p.size||30;return <Text style={{fontSlot:size===40?22:size===54?23:21,textColor:p.color||ink,width:p.width||912,height:size+22,lineHeight:size+16}}>{typeof p.children==='string'?displayText(p.children):p.children}</Text>;}
 function MessageRow(p:{item:Row}){
  const item=p.item,block=item.format?.block;
  const color=item.kind==='heading'||block==='heading'?accent:item.kind==='tool'||block==='quote'?muted:ink;
  return <View class="relative" style={{height:item.height,width:888,bgColor:block==='code'||block==='table'?soft:bg}}>
   <Show when={block==='quote'}><View class="absolute left-[0] top-[6] w-[3]" style={{height:item.height-12,bgColor:c.accent2}}/></Show>
   <Show when={block==='rule'}><View class="absolute left-[0] top-[6] w-[888] h-[1]" style={{bgColor:line}}/></Show>
   <Show when={block!=='rule'&&item.runs?.length} fallback={block==='rule'?undefined:<Text style={{fontSlot:20,width:888,height:item.height,lineHeight:40,textColor:color}}>{item.text}</Text>}>
    <For each={item.runs}>{run=>{
     const x=(item.indent||0)+run.x,runColor=run.flags&LINK?accent:run.flags&EMPHASIS?c.accent2:color;
     return <View class="absolute top-[0]" style={{insetL:x,width:run.width+2,height:item.height,bgColor:run.flags&CODE||block==='table'?soft:bg}}>
      <Text style={{fontSlot:20,width:run.width+2,height:item.height,lineHeight:40,textColor:runColor}}>{run.text}</Text>
      <Show when={!!(run.flags&STRONG)}><View class="absolute left-[1] top-[0]"><Text style={{fontSlot:20,width:run.width+2,height:item.height,lineHeight:40,textColor:runColor}}>{run.text}</Text></View></Show>
      <Show when={!!(run.flags&(LINK|EMPHASIS|STRIKE))}><View class="absolute left-[0] h-[1]" style={{insetT:run.flags&STRIKE?21:36,width:run.width,bgColor:runColor}}/></Show>
     </View>;
    }}</For>
   </Show>
  </View>;
 }
 function Icon(p:{name:string;size:number;color:string}){return <Image style={{width:p.size,height:p.size}} ref={node=>{const handle=info.icon(p.name,p.color);if(handle>=0)getOps().setImage(node.id,handle);}}/>;}
 function Avatar(p:{animated?:boolean;size:number}){
  const handle=info.avatar(portrait.staticFrame(),accent,c.accent2,bg,true);
  let nodeId:number|undefined;
  if(p.animated)onCleanup(()=>{if(nodeId!==undefined)avatarNodes.delete(nodeId);});
  return <Image style={{width:p.size,height:p.size}} ref={node=>{if(p.animated){nodeId=node.id;avatarNodes.add(node.id);}if(handle>=0)getOps().setImage(node.id,handle);}}/>;
 }
 function Key(p:{name:string;label:string;dim?:boolean;progress?:number}){
  function Ring(){let node:number|undefined;createRenderEffect(()=>{const step=Math.round((p.progress||0)*40);if(node!==undefined)getOps().setImage(node,info.ring(step,accent));});return <Image class="absolute left-[-4] top-[-4] w-[50] h-[50]" ref={value=>{node=value.id;getOps().setImage(node,info.ring(Math.round((p.progress||0)*40),accent));}}/>;}
  return <View class="flex-row items-center gap-[8]" style={{opacity:p.dim?.45:1}}><View class="relative h-[42] px-[12] rounded-[21]" style={{bgColor:accent}}><Show when={(p.progress||0)>0}><Ring/></Show><Text style={{fontSlot:21,textColor:selectedInk,height:42,lineHeight:38}}>{p.name}</Text></View><Text style={{fontSlot:21,textAlign:0,textColor:muted,height:50,lineHeight:46}}>{p.label}</Text></View>;
 }
 function Cover(p:{project:Project}){
  const value=()=>covers()[p.project.id];
  return <View class="w-[88] h-[88] rounded-[8] overflow-hidden items-center justify-center" style={{bgColor:line}}>
   <Show when={value()} fallback={<Icon name="folder-plus" size={40} color={muted}/>}>
    <Image class="w-[88] h-[88]" ref={node=>{const v=value();if(v)getOps().setImage(node.id,v.handle);}}/>
   </Show>
  </View>;
 }
 return <View class="relative w-full h-full overflow-hidden" style={{bgColor:bg}}>
  <View class="absolute left-[56] top-[28] flex-row items-center gap-[18]">
   <Avatar animated size={80}/>
   <Label size={40} width={page()==='conversation'?580:660}>{page()==='models'?'生成模型':page()==='share'?'在线分享':page()==='versions'?'历史版本':page()==='list'?'游戏生成器':clip(title(),14)}</Label>
  </View>
  <View class={page()==='conversation'?"absolute right-[56] top-[32]":"absolute right-[56] top-[50]"}><Label width={210} color={muted}>{page()==='models'?'全局设置':page()==='list'?projects().length+' 个项目':page()==='share'?qr()?'第 '+qr()!.revision+' 版':'正在分享':draftChanged()?'草稿改动':revision()?'第 '+revision()+' 版':'新项目'}</Label></View>
  <Show when={page()==='conversation'&&taskSeconds()!==undefined}><View class="absolute right-[56] top-[74]"><Text style={{fontSlot:20,width:210,height:40,lineHeight:38,textColor:muted}}>{taskElapsed()}</Text></View></Show>
  <View class="absolute left-[56] top-[119] w-[912] h-[1]" style={{bgColor:line}}/>
  <Show when={page()==='list'}>
   <Show when={projects().length} fallback={<View class="absolute left-[56] top-[252] flex-col gap-[26]"><Label size={54}>从一句想法开始</Label><Label color={muted}>按 X 创建你的第一个游戏</Label></View>}>
    <View class="absolute left-[56] top-[151] w-[912] flex-col gap-[12]">
     <For each={projects().slice(Math.max(0,index()-2),Math.max(0,index()-2)+3)}>{(p,i)=>{const selected=()=>index()===Math.max(0,index()-2)+i();return <View class="w-[912] h-[132] px-[14] rounded-[16] flex-row items-center gap-[24]" style={{bgColor:selected()?soft:bg}}>
      <Cover project={p}/><View class="flex-col gap-[5]"><Label size={40} width={730}>{clip(p.title,18)}</Label><Label width={730} color={muted}>{p.revision?'第 '+p.revision+' 版 · '+(p.draftChanged?'草稿有改动':'已发布，可试玩'):p.scaffoldReady?'基础脚手架 · 可试玩':'尚未生成 · 说出你的游戏想法'}</Label></View>
     </View>;}}</For>
    </View>
   </Show>
   <View class="absolute left-[56] top-[587]"><Text style={{fontSlot:20,width:912,height:40,lineHeight:38,textColor:accent}}>{'生成模型 · '+clip(modelLabel(config()),27)+'  ›'}</Text></View>
  </Show>
  <Show when={page()==='share'}>
   <Show when={qr()} fallback={<View class="absolute left-[56] top-[260] flex-col gap-[12]"><Label size={40}>{sharing()?'正在上传游戏':'分享未完成'}</Label><Label color={muted}>可以按 A 重试，B 返回</Label></View>}>
    <View class="absolute left-[0] top-[146] w-full items-center"><Image style={{width:qr()!.pixels,height:qr()!.pixels}} ref={node=>getOps().setImage(node.id,qr()!.handle)}/></View>
    <View class="absolute left-[56] top-[590]"><Label color={muted}>手机扫码，直接在线游玩</Label></View>
   </Show>
  </Show>
  <Show when={page()==='versions'}>
   <View class="absolute left-[56] top-[151] w-[912] flex-col gap-[12]">
    <Show when={historyVersions().length} fallback={<Label color={muted}>暂无历史版本</Label>}>
     <For each={historyVersions().slice(Math.max(0,versionChoice()-2),Math.max(0,versionChoice()-2)+3)}>{(v,i)=><View class="w-[912] h-[132] px-[20] rounded-[14] flex-col justify-center" style={{bgColor:versionChoice()===Math.max(0,versionChoice()-2)+i()?soft:bg}}><Label size={40}>{'第 '+v.revision+' 版 · '+clip(v.title,16)}</Label><Label color={muted}>{clip(v.summary||'已保存游戏',27)}</Label></View>}</For>
    </Show>
   </View>
  </Show>
  <Show when={page()==='models'}>
   <View class="absolute left-[56] top-[151] w-[912] flex-col gap-[12]">
    <For each={GENERATION_MODELS.slice(Math.max(0,choice()-3),Math.max(0,choice()-3)+4)}>{(option,i)=>{const absolute=()=>Math.max(0,choice()-3)+i();return <View class="w-[912] h-[102] px-[20] rounded-[14] flex-row items-center justify-between" style={{bgColor:choice()===absolute()?soft:bg,borderColor:choice()===absolute()?accent:line,borderWidth:1}}>
     <View class="flex-col"><Label size={40} width={680}>{option.name}</Label><Text style={{fontSlot:20,width:680,height:36,lineHeight:34,textColor:muted}}>{providerName(option.provider)+' · '+option.tier}</Text></View>
     <Label width={140} color={modelChoice(config())===absolute()?accent:muted}>{modelChoice(config())===absolute()?'已启用':choice()===absolute()?'待保存':''}</Label>
    </View>;}}</For>
   </View>
   <View class="absolute left-[56] top-[610]"><Text style={{fontSlot:20,width:912,height:32,lineHeight:30,textColor:muted}}>{'用于游戏生成与摘要 · '+(choice()+1)+'/'+GENERATION_MODELS.length}</Text></View>
  </Show>
  <Show when={page()==='conversation'}>
   <Show when={!question()||inputBusy()||questionDetails()}><Show when={rows().length} fallback={<View class="absolute left-[56] top-[236] flex-col gap-[24]"><Label size={54}>你的游戏，从这里开始</Label><Label color={muted}>按住 A 说话，松开后交给 Agent</Label></View>}>
    <View class="absolute left-[56] top-[151] w-[912] overflow-hidden flex-col" style={{height:inputBusy()?364:468}}>
     <For each={rows()}>{item=><MessageRow item={item}/>}</For>
    </View>
    <Show when={historyCount()>1&&!inputBusy()}><View class="absolute right-[56] top-[632]"><Text style={{fontSlot:20,width:330,height:32,lineHeight:30,textColor:muted,textAlign:2}}>{historyBlock()+1+'/'+historyCount()+' ↑↓'}</Text></View></Show>
   </Show>
   </Show>
   <Show when={question()&&!inputBusy()&&!questionDetails()}>
    <View class="absolute left-[56] top-[151] w-[912] flex-col gap-[12]">
     <For each={questionLines()}>{text=><Text style={{fontSlot:20,width:888,height:44,lineHeight:40,textColor:ink}}>{text}</Text>}</For>
     <For each={question()!.options.slice(Math.max(0,questionChoice()-2),Math.max(0,questionChoice()-2)+3)}>{(o,i)=><View class="w-[912] h-[90] px-[16] rounded-[12] flex-col justify-center" style={{bgColor:questionChoice()===Math.max(0,questionChoice()-2)+i()?soft:bg,borderColor:line,borderWidth:1}}><Label width={860}>{clip(o.label,25)}</Label><Text style={{fontSlot:20,width:860,height:32,lineHeight:30,textColor:muted}}>{clip(o.description||'',29)}</Text></View>}</For>
     <Show when={!question()!.options.length}><Label color={muted}>按住 A，用语音回答</Label></Show>
    </View>
   </Show>
   <Show when={inputBusy()}>
    <View class="absolute left-[56] top-[546] w-[912] h-[110] px-[20] rounded-[14] flex-row items-center gap-[22]" style={{bgColor:bg,borderColor:line,borderWidth:1}}>
     <Label width={260} color={accent}>{holding||voice().state==='recording'?'正在听 · '+Math.floor(voice().seconds||0)+' 秒':'正在识别'}</Label>
     <View class="w-[500] h-[70] flex-row items-center justify-between"><For each={levels()}>{level=><View class="w-[8] rounded-[4]" style={{height:holding||voice().state==='recording'?level:4,bgColor:accent}}/>}</For></View>
    </View>
   </Show>
  </Show>
  <Show when={!inputBusy()}><View class={page()==='models'?"absolute left-[56] top-[645]":"absolute left-[56] top-[632]"}><Text style={{fontSlot:20,width:page()==='conversation'&&historyCount()>1?530:912,height:32,lineHeight:30,textColor:muted}}>{deleteProgress()>0?'长按 Y 删除项目和游戏':clip(status(),page()==='conversation'&&historyCount()>1?17:29)}</Text></View></Show>
  <View class="absolute left-[56] top-[679] w-[912] h-[1]" style={{bgColor:line}}/>
  <Show when={page()==='list'}><View class="absolute left-[56] top-[704] w-[912] flex-row justify-start gap-[8]"><Key name="A" label="打开"/><Key name="X" label="新建"/><Key name="SR" label="试玩" dim={!(projects()[index()]?.revision||projects()[index()]?.scaffoldReady)||busy()}/><Key name="R" label="分享" dim={busy()||!projects().length}/><Key name="SE" label="模型" dim={busy()}/><Key name="Y" label="删除" progress={deleteProgress()} dim={busy()||!projects().length}/><Key name="B" label="退出"/></View></Show>
  <Show when={page()==='share'}><View class="absolute left-[56] top-[704] flex-row gap-[8]"><Show when={sharing()} fallback={<Key name="A" label="重试"/>}><Key name="X" label="停止"/></Show><Key name="B" label="返回" dim={busy()}/></View></Show>
  <Show when={page()==='versions'}><View class="absolute left-[56] top-[704] flex-row gap-[8]"><Key name="↑↓" label="选择"/><Key name="A" label="恢复" dim={busy()||!historyVersions().length}/><Key name="B" label="返回" dim={busy()}/></View></Show>
  <Show when={page()==='models'}><View class="absolute left-[56] top-[704] w-[912] flex-row justify-start gap-[8]"><Key name="↑↓" label="选择"/><Key name="A" label="保存" dim={busy()}/><Key name="B" label="返回" dim={busy()}/></View></Show>
  <Show when={page()==='conversation'}><View class="absolute left-[56] top-[704] w-[912] flex-row justify-start gap-[8]"><Key name="A" label={question()?"选/长按说":"语音"} dim={busy()&&!question()}/><Key name="SR" label="试玩" dim={!playable()||busy()||inputBusy()}/><Show when={canStop()}><Key name="X" label="停止"/></Show><Key name="L" label="版本" dim={busy()||inputBusy()}/><Key name="SE" label="详情"/><Key name="B" label={question()?"跳过":"返回"}/></View></Show>
 </View>;
}
mount(App);
