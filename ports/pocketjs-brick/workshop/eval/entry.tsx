/** Evaluation driver, deliberately separate from the handheld UI bundle.
 * All model, context, tool, storage and HTTP work uses production code. */
import {createSignal} from 'solid-js';
import {mount} from '@pocketjs/framework/solid';
import {View,Text} from '@pocketjs/framework/solid/components';
import {installServices,read,write,writeChunks,delay,native} from '../../app/services.ts';
import {newProject,initializeProject,recoverProject,saveProject,loadProject,projectInfo,projectAgent,STARTER} from '../model.ts';
import {loadGenerationConfig} from '../generation-models.ts';
import {contentHash} from '../project-state.ts';
import {pico8SystemPrompt} from '../skill.ts';
import type {GameAgentEvent} from '../agent.ts';

function Evaluation(){
 installServices();
 const [status,setStatus]=createSignal('Preparing evaluation');
 let done=false,ok=false;
 (globalThis as Record<string,unknown>).__workshopEvaluation=(op:number)=>op===0?(done?1:0):(ok?1:0);
 void(async()=>{
  const started=Date.now(),events:any[]=[],turns:any[]=[],questions:any[]=[];
  let spec:any,p:any,error='',active=0,firstOutput:number|undefined,historyBefore='',historyCount=0;
  try{
   spec=JSON.parse(await read('projects/game-eval-input.json'));
   const info=(native as any).workshop;
   const config=loadGenerationConfig(JSON.parse(await read('config.json')),info.existingKey);
   p=newProject();
   if(spec.seed){
    p.lua=spec.seed.lua;p.title=spec.seed.title||'Evaluation fixture';p.revision=1;p.scaffoldReady=true;
    if(spec.seed.gfx!==undefined)p.gfx=spec.seed.gfx;
    if(spec.seed.audio!==undefined)p.audio=spec.seed.audio;
    if(spec.seed.design!==undefined)p.design=spec.seed.design;
    p.pending={revision:1,title:p.title,summary:'Evaluation initial fixture',lua:p.lua,gfx:p.gfx,audio:p.audio};
    await saveProject(p);await recoverProject(p);
   }else await initializeProject(p);
   if(spec.history_fixture){
    const f=spec.history_fixture;
    if(!Number.isInteger(f.pairs)||f.pairs<1||f.pairs>5000||typeof f.user_text!=='string'||typeof f.assistant_text!=='string'||!f.user_text||!f.assistant_text||(f.user_text.length+f.assistant_text.length)*f.pairs>4000000)throw Error('Invalid synthetic stress history');
    for(let i=0;i<f.pairs;i++)p.messages.push({role:'user',content:'[synthetic '+i+'] '+f.user_text,timestamp:i*2},{role:'assistant',content:[{type:'text',text:f.assistant_text}],api:'openai-completions',provider:config.provider||'deepseek',model:config.model,stopReason:'stop',timestamp:i*2+1,usage:{input:0,output:0,cacheRead:0,cacheWrite:0,totalTokens:0,cost:{input:0,output:0,cacheRead:0,cacheWrite:0,total:0}}});
    historyCount=p.messages.length;historyBefore=contentHash(JSON.stringify(p.messages));await saveProject(p);
   }
   let answerIndex=0;
   for(const prompt of spec.turns){
    // Reopening between turns exercises exactly the saved project, not a new
    // evaluator-side session. Native publication and metadata stay enabled.
    p=await loadProject(projectInfo(p));
    active=turns.length;const begin=Date.now(),before=p.revision;
    const system=spec.system_prompt??(pico8SystemPrompt(p.revision>0||p.lua!==STARTER)+(spec.prompt_append?'\n\n'+spec.prompt_append:''));
    const event=(e:GameAgentEvent)=>{
     if(e.type==='message_update'){
      if(firstOutput===undefined&&['text_delta','thinking_delta','toolcall_delta'].includes(e.assistantMessageEvent.type))firstOutput=Date.now()-started;
      return;
     }
     if(['tool_execution_start','tool_execution_end','turn_start','turn_end','retry','error'].includes(e.type)){
      const x:any=e;
      events.push({type:e.type,ms:Date.now()-started,turn:active,...(x.toolName?{tool:x.toolName,id:x.toolCallId}:{}),...(x.args?{args:x.args}:{}),...(e.type==='tool_execution_end'?{error:x.isError,result:x.result}:{}),...(e.type==='retry'?{attempt:x.attempt,delayMs:x.delayMs,summary:x.summary}:{}),...(e.type==='error'?{message:x.message}:{})});
     }
    };
    const agent=projectAgent(p,config,setStatus,()=>{},event,prompt,async q=>{
     const answer=spec.answers?.[answerIndex++]??{answer:'跳过这个问题；仍按本轮原请求处理，没有新增方向选择或修改/发布授权。',skipped:true};
     questions.push({question:q,answer,policy:spec.answers?.[answerIndex-1]?'explicit-case-answer':'skip-without-new-authorization'});return answer;
    },spec.system_prompt||spec.prompt_append?system:undefined);
    try{await agent.prompt(prompt);turns.push({prompt,ms:Date.now()-begin,before,revision:p.revision,system_prompt:system});}
    catch(e){turns.push({prompt,ms:Date.now()-begin,before,revision:p.revision,system_prompt:system,error:String(e)});throw e;}
   }
   ok=true;
  }catch(e){error=String(e);setStatus(error);}
  try{
   const payload=JSON.stringify({schema:1,started,elapsed_ms:Date.now()-started,first_output_ms:firstOutput,ok,error,project_id:p?.id,revision:p?.revision,events,turns,questions,history_fixture:historyCount?{kind:'synthetic-stress-history',count:historyCount,sha256_before:historyBefore,sha256_after:contentHash(JSON.stringify(p.messages.slice(0,historyCount))),count_after:p.messages.length}:undefined,service_metrics:native.serviceMetrics?.()});
   await writeChunks('projects/game-eval-result.json',async append=>{for(let i=0;i<payload.length;){let end=Math.min(i+65536,payload.length);if(end<payload.length&&payload.charCodeAt(end-1)>=0xd800&&payload.charCodeAt(end-1)<=0xdbff)end--;append(payload.slice(i,end));i=end;await delay(0);}});
  }catch(e){ok=false;setStatus(String(e));}
  done=true;
 })();
 return <View class="w-[1024] h-[768] bg-[#f1ede4] p-[40]"><Text style={{width:900,height:90,textColor:'#283629'}}>Agent evaluation · {status()}</Text></View>;
}
mount(Evaluation);
