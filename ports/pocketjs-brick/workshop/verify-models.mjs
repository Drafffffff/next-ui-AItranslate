import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
import {mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../../../',import.meta.url));
const bundle=await build({stdin:{contents:'export * from "./generation-models.ts"; export * from "./agent.ts";',resolveDir:fileURLToPath(new URL('.',import.meta.url))},bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Models',target:'es2020'});
const context=vm.createContext({});vm.runInContext(bundle.outputFiles[0].text,context);
const {GENERATION_MODELS,loadGenerationConfig,selectGenerationModel,modelChoice,generationProvider,createGameAgent}=context.Models;
const clone=v=>JSON.parse(JSON.stringify(v));
assert.equal(GENERATION_MODELS.length,6);
assert.deepEqual(clone(GENERATION_MODELS.map(x=>x.model)),['deepseek-v4-pro','deepseek-flash','qwen3.8-max','qwen3.8-flash','deepseek-v4.1-flash','claude-haiku-5-5']);
const regional='https://ws-123.ap-southeast-1.maas.aliyuncs.com/compatible-mode/v1';
let config=loadGenerationConfig({baseUrl:'https://api.deepseek.com/v1',key:'old-deepseek',model:'deepseek-flash',asrModel:'qwen-audio-3.1-asr-flash-message',asrEndpoint:'wss://ws-123.ap-southeast-1.maas.aliyuncs.com/api-ws/v1/inference',asrKey:'voice-only',contextWindowTokens:64000,untouched:{custom:true},generationChannels:{bailian:{endpoint:regional,key:'old-bailian'}}});
const original=JSON.stringify(config);
const bailian=selectGenerationModel(config,2);
assert.equal(bailian.key,'old-bailian');assert.equal(bailian.endpoint,regional);
const returned=selectGenerationModel(bailian,0);
assert.equal(returned.key,'old-deepseek');assert.equal(returned.endpoint,'https://api.deepseek.com/v1');
for(const value of [bailian,returned])for(const key of ['asrKey','asrModel','asrEndpoint','contextWindowTokens','untouched'])assert.deepEqual(clone(value[key]),clone(config[key]));
assert.equal(JSON.stringify(config),original,'Selection mutated unsaved settings');
const clean=selectGenerationModel(loadGenerationConfig({key:'deepseek-only'}),3);
assert.equal(clean.key,'');assert.equal(clean.endpoint,'https://dashscope.aliyuncs.com/compatible-mode/v1');
assert.equal(selectGenerationModel(clean,1).key,'deepseek-only');
assert.equal(loadGenerationConfig({endpoint:regional,model:'qwen3.8-max'},'deepseek-system-key').key,'');
assert.equal(modelChoice(loadGenerationConfig()),1);
assert.equal(modelChoice(loadGenerationConfig({model:'custom-model'})),-1);
assert.equal(generationProvider(loadGenerationConfig({baseUrl:regional,model:'qwen3.8-max'})),'bailian');
assert.throws(()=>selectGenerationModel(config,6),/请选择/);
const haikuBase=loadGenerationConfig({...config,generationChannels:{...config.generationChannels,teamo:{endpoint:'https://api.teamorouter.cn/v1',key:'fixture-haiku-only'}}});
const haiku=selectGenerationModel(haikuBase,5);
assert.equal(haiku.provider,'teamo');assert.equal(haiku.model,'claude-haiku-5-5');assert.equal(haiku.key,'fixture-haiku-only');
assert.equal(selectGenerationModel(haiku,1).key,'old-deepseek');
assert.equal(selectGenerationModel(config,5).key,'','Third-party channel must never receive the DeepSeek key');
assert.equal(loadGenerationConfig({endpoint:'https://api.teamorouter.cn/v1'},'system-deepseek-key').provider,'teamo');
assert.equal(loadGenerationConfig({endpoint:'https://api.teamorouter.cn/v1'},'system-deepseek-key').key,'');
const answer=(text,calls=[])=>({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:text,tool_calls:calls},finish_reason:calls.length?'tool_calls':'stop'}],usage:{prompt_tokens:100,completion_tokens:20,total_tokens:120}})});
// Use each fixed choice through the real Pi loop, including a tool and old-provider history.
let total=0;
for(let choice=0;choice<GENERATION_MODELS.length;choice++){
 const selected=selectGenerationModel(haikuBase,choice),calls=[],tool={id:'read',type:'function',function:{name:'read_game',arguments:'{}'}};
 const agent=createGameAgent({provider:selected.provider,modelId:selected.model,endpoint:selected.endpoint,key:selected.key,maxOutputTokens:65536,
  messages:[{role:'user',content:'旧需求',timestamp:1},{role:'assistant',content:[{type:'text',text:'旧模型回复'}],provider:'deepseek',model:'deepseek-flash',api:'openai-completions',stopReason:'stop',timestamp:1,usage:{input:0,output:0,cacheRead:0,cacheWrite:0,totalTokens:0,cost:{input:0,output:0,cacheRead:0,cacheWrite:0,total:0}}}],
  tools:[{name:'read_game',description:'Read exact source.',parameters:{type:'object',properties:{}},execute:async()=>({lua:'REAL_CURRENT_SOURCE'})}],
  request(url,key,body){const value=JSON.parse(body);calls.push(value);total++;
   assert.equal(url,selected.endpoint+'/chat/completions');assert.equal(key,selected.key);assert.equal(value.model,selected.model);assert.equal(value.stream,false);
   if(selected.provider==='bailian'){assert.equal(value.enable_thinking,true);assert.equal(value.thinking,undefined);}else if(selected.provider==='teamo'){assert.equal(value.reasoning_effort,'medium');assert.equal(value.thinking,undefined);assert.equal(value.enable_thinking,undefined);}else{assert.equal(value.thinking.type,'enabled');assert.equal(value.enable_thinking,undefined);}
   if(selected.provider==='bailian'&&selected.model.startsWith('qwen3')){assert.equal(value.max_tokens,undefined);assert.equal(value.max_completion_tokens,65536);assert.equal(value.thinking_budget,undefined);}else assert.equal(value.max_tokens,65536);
   return {promise:Promise.resolve(calls.length===1?answer(null,[tool]):answer('修改已完成')),cancel(){}};
  }});
 await agent.prompt('继续修改');assert.equal(calls.length,2);assert.equal(agent.messages.at(-1).provider,selected.provider);assert.equal(agent.messages.at(-1).model,selected.model);assert.equal(agent.messages.at(-1).stopReason,'stop');
 assert(calls[0].messages.some(m=>m.content==='旧模型回复'));assert.match(calls[1].messages.at(-1).content,/REAL_CURRENT_SOURCE/);
}
// Force actual Pi compaction on Bailian, and verify the summary and ordinary request use the same selection.
const selected=selectGenerationModel(config,3),history=[];
for(let i=0;i<20;i++)history.push({role:'user',content:'旧要求 '+i+' '+('history detail '.repeat(150)),timestamp:i},{role:'assistant',content:[{type:'text',text:'旧修改已保存'}],provider:'deepseek',model:'deepseek-flash',api:'openai-completions',stopReason:'stop',timestamp:i,usage:{input:0,output:0,cacheRead:0,cacheWrite:0,totalTokens:0,cost:{input:0,output:0,cacheRead:0,cacheWrite:0,total:0}}});
let summaries=0,ordinary=0,commits=0;
const agent=createGameAgent({provider:selected.provider,modelId:selected.model,endpoint:selected.endpoint,key:selected.key,messages:history,tools:[{name:'read_game',description:'Read current game.',parameters:{type:'object',properties:{}},execute:async()=>({lua:'CURRENT'})}],
 context:{contextWindow:8000,reserveTokens:2048,keepRecentTokens:512,onCommit(){commits++;}},
 request(url,key,body){const value=JSON.parse(body);assert.equal(value.model,'qwen3.8-flash');assert.equal(value.enable_thinking,true);assert.equal(value.thinking,undefined);assert.equal(url,regional+'/chat/completions');assert.equal(key,'old-bailian');
  assert.equal(value.max_tokens,undefined);
  if(!value.tools){summaries++;assert.equal(value.max_completion_tokens,16384);assert.equal(value.thinking_budget,4096);}else{ordinary++;assert.equal(value.thinking_budget,undefined);assert(value.messages.some(m=>typeof m.content==='string'&&m.content.includes('<summary>')));}
  return {promise:Promise.resolve(answer('## Goal\n保留原玩法。\n## Next Steps\n继续修改。')),cancel(){}};
 }});
await agent.prompt('新的修改要求');assert(summaries>0,JSON.stringify(agent.messages.at(-1)));assert(commits>0);assert.equal(ordinary,1);assert.equal(agent.messages.at(-1).stopReason,'stop');
// The shared native bridge must choose public keys only for exact official authorities.
const temp=mkdtempSync(join(tmpdir(),'workshop-models-'));
try{
 writeFileSync(join(temp,'test.c'),`#include <assert.h>\n#include "${root}workspace/all/common/ai_credentials.h"\nint main(void){
 const char *good[]={"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions","https://dashscope-intl.aliyuncs.com/compatible-mode/v1","${regional}/chat/completions"};
 for(int i=0;i<3;i++)assert(!strcmp(nextui_ai_provider_for_url(good[i]),"bailian"));
 assert(!strcmp(nextui_ai_provider_for_url("https://api.deepseek.com/chat/completions"),"deepseek"));
 const char *bad[]={"https://dashscope.aliyuncs.com.evil/x","https://evil@dashscope.aliyuncs.com/x","http://dashscope.aliyuncs.com/x","https://api.deepseek.com.evil/x","https://evil.ws.cn-beijing.maas.aliyuncs.com/x","https://ws.cn-beijing.maas.aliyuncs.com.evil/x","https://ws.unknown.maas.aliyuncs.com/x"};
 for(int i=0;i<7;i++)assert(!nextui_ai_provider_for_url(bad[i]));return 0;}`);
 execFileSync('cc',['-Wall','-Wextra','-Werror',join(temp,'test.c'),'-o',join(temp,'test')]);execFileSync(join(temp,'test'));
}finally{rmSync(temp,{recursive:true,force:true});}
console.log(`PASS: six pinned choices including Pi Haiku, legacy/regional config preservation, isolated credentials, ${total} real Pi model/tool calls, Bailian compaction (${summaries} summaries), native official-host credential routing.`);
