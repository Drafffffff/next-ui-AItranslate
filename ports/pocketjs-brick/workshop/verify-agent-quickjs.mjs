// Uses the same pinned QuickJS C sources and stack budget as the PocketJS host.
import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { build } from "esbuild";

const directory = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(directory, "../../..");
const quickjs = process.env.WORKSHOP_QUICKJS_SOURCE || path.join(root, "build/pocketjs-port/quickjs/libquickjs-sys/embed/quickjs");
assert.equal((await readFile(path.join(quickjs, "VERSION"), "utf8")).trim(), "2026-06-04", "Run build-app.sh to acquire the pinned QuickJS sources.");
const temporary = await mkdtemp(path.join(tmpdir(), "pocketjs-agent-quickjs-"));
try {
  const bundled = await build({ entryPoints: [path.join(directory, "agent.ts")], bundle: true, write: false,
    platform: "neutral", target: "es2020", format: "iife", globalName: "WorkshopAgent" });
  const editing = await build({entryPoints:[path.join(directory,'editing.ts')],bundle:true,write:false,platform:'neutral',target:'es2020',format:'iife',globalName:'WorkshopEditing'});
  const avatar = await build({entryPoints:[path.join(directory,'avatar.ts')],bundle:true,write:false,platform:'neutral',target:'es2020',format:'iife',globalName:'WorkshopAvatar'});
  const fixture = `
(async function () {
 function check(value,message) { if(!value) throw new Error(message); }
 var face=WorkshopAvatar.createAvatar(),states=['idle','listening','thinking','working','done','error'];
 for(var f=0;f<5000;f++){
  var frame=face.tick(states[Math.floor(f/31)%6],f*42);
  check(frame&&frame.bodyPath[0]==='M','original bloub frame in QuickJS');
  if(f%31===0)check(!/NaN|Infinity/.test(JSON.stringify(frame)),'finite avatar morph');
 }
 var still=WorkshopAvatar.createAvatar(true);check(still.tick('idle',0),'reduced motion first frame');check(!still.tick('idle',1000),'reduced motion stays still');check(still.tick('working',1100),'reduced motion state change');
 var game='speed=2\\n'+('-- keep trailing spaces  \\n'.repeat(900))+'function _init() end\\nfunction _update() end\\nfunction _draw() cls(0) end\\n';
 var benchmarkStart=Date.now();
 for(var trial=0;trial<50;trial++){
  var edited=WorkshopEditing.updateLua(game,{edits:[{oldText:'speed=2',newText:'speed=1.5'}]});
  check(edited.lua===game.replace('speed=2','speed=1.5'),'unchanged code preservation');
  var difference=WorkshopEditing.codeDifference(game,edited.lua);
  check(difference.firstChangedLine===1&&!difference.diff_truncated&&difference.diff.indexOf('speed=1.5')>=0,'actual numbered diff');
 }
 var fuzzy=WorkshopEditing.updateLua('name="cat"  \\nkeep=1  \\n',{edits:[{oldText:'name=“cat”\\n',newText:'name="dog"\\n'}]});
 check(fuzzy.lua==='name="dog"\\nkeep=1  \\n','Pi fuzzy match without unrelated formatting changes');
 var batch=WorkshopEditing.updateLua('a=1\\nb=2\\n',{edits:[{oldText:'b=2',newText:'c=3'},{oldText:'a=1',newText:'b=2'}]});
 check(batch.lua==='b=2\\nc=3\\n','same-original batch replacement');
 var big=Array.from({length:2500},function(_,i){return 'old='+i;}).join('\\n');
 check(WorkshopEditing.codeDifference(big,big.replaceAll('old','new')).diff_truncated,'bounded large diff');
 globalThis.__editingBenchmark='Pi editing on '+game.length+' chars: 50 edit/diff operations in '+(Date.now()-benchmarkStart)+' ms (local CPU, excludes model/network).';
 var requests=0, executions=0, events=[];
 var agent=WorkshopAgent.createGameAgent({
  endpoint:'https://example.invalid/v1',modelId:'deepseek-flash',key:'private-fixture-key',
  systemPrompt:'Create one game.',tools:[{name:'save_game',description:'Save Lua.',parameters:{
   type:'object',properties:{lua:{type:'string'}},required:['lua'],additionalProperties:false
  },execute:async function(args){check(args.lua==='function _draw() cls(1) end','tool arguments');executions++;return {saved:true};}}],
  request:function(url,key,body){
   var parsed=JSON.parse(body);requests++;
   check(parsed.stream===false,'nonstream request');
   if(requests===2){check(parsed.messages[parsed.messages.length-1].role==='tool','tool result transcript');}
   var message=requests===1?{role:'assistant',content:null,tool_calls:[{id:'save-1',type:'function',function:{
    name:'save_game',arguments:JSON.stringify({lua:'function _draw() cls(1) end'})
   }}]}:{role:'assistant',content:'Ready.'};
   return {promise:Promise.resolve({status:200,text:JSON.stringify({choices:[{message:message,finish_reason:requests===1?'tool_calls':'stop'}]})}),cancel:function(){}};
  },onEvent:function(event){events.push(event.type);}
 });
 await agent.prompt('Build a game.');
 check(requests===2 && executions===1,'real Pi two-turn execution');
 check(agent.messages.length===4 && !agent.busy,'persistable transcript and idle state');
 check(events.indexOf('tool_execution_end')>=0 && events[events.length-1]==='agent_end','Pi lifecycle');
 check(JSON.stringify(agent.messages).indexOf('private-fixture-key')<0,'credential exclusion');
 check(Array.from(new TextEncoder().encode('猫🎮')).join(',')==='231,140,171,240,159,142,174','UTF-8 shim');
 // Exercise actual Pi compaction in the same constrained QuickJS runtime.
 var old=[],usage={input:0,output:0,cacheRead:0,cacheWrite:0,totalTokens:0,cost:{input:0,output:0,cacheRead:0,cacheWrite:0,total:0}};
 for(var i=0;i<18;i++){
  old.push({role:'user',content:'保留三条命。'+i,timestamp:1});
  old.push({role:'assistant',content:[{type:'text',text:'旧代码 '+('x=1;'.repeat(2500))}],api:'openai-completions',provider:'deepseek',model:'deepseek-flash',stopReason:'stop',timestamp:1,usage:usage});
 }
 var summaries=0,normal=0,checkpoint;
 var compressed=WorkshopAgent.createGameAgent({
  endpoint:'https://example.invalid/v1',modelId:'deepseek-flash',key:'private-fixture-key',systemPrompt:'Keep PICO-8 Skill.',messages:old,
  tools:[{name:'read_game',description:'Read current source.',parameters:{type:'object',properties:{}},execute:async function(){return {lua:'current'};}}],
  context:{contextWindow:16000,reserveTokens:4096,keepRecentTokens:2200,onCommit:async function(record,messages){checkpoint=record;check(messages.length===37,'full log at checkpoint');}},
  request:function(url,key,body){
   var parsed=JSON.parse(body),summary=!parsed.tools;
   if(summary){summaries++;check(parsed.max_tokens>=16384&&parsed.max_tokens<=32768,'summary output limit');}
   else {normal++;check(parsed.messages[0].content==='Keep PICO-8 Skill.','fixed Skill');check(JSON.stringify(parsed.messages).indexOf('<summary>')>=0,'projected summary');check(parsed.messages[parsed.messages.length-1].content==='降低速度','latest request');}
   return {promise:Promise.resolve({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:summary?'保留三条命；最新要求降低速度。':'已修改。'},finish_reason:'stop'}]})}),cancel:function(){}};
  }
 });
 await compressed.prompt('降低速度');
 check(summaries>0 && normal===1 && !!checkpoint,'automatic compaction');
 check(compressed.messages.length===38 && compressed.messages[37].stopReason==='stop','complete uncompacted local log');
 globalThis.__verificationDone=true;
})().catch(function(error){globalThis.__verificationError=String(error.stack||error);});
`;
  const source = path.join(temporary, "verify.js");
  await writeFile(source, bundled.outputFiles[0].text + editing.outputFiles[0].text + avatar.outputFiles[0].text + fixture);
  const c = path.join(temporary, "verify.c");
  await writeFile(c, `
#include <stdio.h>
#include <stdlib.h>
#include "quickjs.h"
static void exception(JSContext *context) {
 JSValue error=JS_GetException(context); const char *text=JS_ToCString(context,error);
 fprintf(stderr,"QuickJS: %s\\n",text?text:"exception"); JS_FreeCString(context,text); JS_FreeValue(context,error);
}
int main(int argc,char **argv) {
 if(argc!=2) return 2;
 FILE *file=fopen(argv[1],"rb"); if(!file) return 2;
 fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
 char *source=malloc(size+1); if(!source) return 2;
 if(fread(source,1,size,file)!=(size_t)size) return 2; fclose(file); source[size]=0;
 JSRuntime *runtime=JS_NewRuntime(); JS_SetMaxStackSize(runtime,256*1024); JS_SetMemoryLimit(runtime,32*1024*1024);
 JSContext *context=JS_NewContext(runtime);
 JSValue result=JS_Eval(context,source,size,argv[1],JS_EVAL_TYPE_GLOBAL); free(source);
 if(JS_IsException(result)) { exception(context); return 1; } JS_FreeValue(context,result);
 JSContext *job_context; int count=0,status;
 while((status=JS_ExecutePendingJob(runtime,&job_context))>0) { if(++count>100000) return 1; }
 if(status<0) { exception(job_context); return 1; }
 JSValue global=JS_GetGlobalObject(context),error=JS_GetPropertyStr(context,global,"__verificationError");
 if(!JS_IsUndefined(error)) { const char *text=JS_ToCString(context,error); fprintf(stderr,"%s\\n",text); JS_FreeCString(context,text); return 1; }
 JSValue done=JS_GetPropertyStr(context,global,"__verificationDone");
 if(JS_ToBool(context,done)!=1) { fprintf(stderr,"QuickJS test did not complete\\n"); return 1; }
 JSMemoryUsage memory; JS_ComputeMemoryUsage(runtime,&memory);
 JSValue benchmark=JS_GetPropertyStr(context,global,"__editingBenchmark"); const char *benchmark_text=JS_ToCString(context,benchmark);
 if(benchmark_text) printf("%s\\n",benchmark_text); JS_FreeCString(context,benchmark_text); JS_FreeValue(context,benchmark);
 printf("PASS: QuickJS 2026-06-04, 256 KiB stack / 32 MiB heap limit, 5000 bloub morph frames/reduced motion, real Pi tool round-trip and automatic compaction; live heap %lld bytes; %d jobs.\\n",(long long)memory.malloc_size,count);
 JS_FreeValue(context,done); JS_FreeValue(context,error); JS_FreeValue(context,global); JS_FreeContext(context); JS_FreeRuntime(runtime); return 0;
}
`);
  const binary = path.join(temporary, "verify-agent");
  const compile = spawnSync("clang", ["-std=gnu11", "-O1", "-D_GNU_SOURCE", '-DCONFIG_VERSION="2026-06-04"', "-I" + quickjs,
    c, ...["quickjs", "cutils", "libregexp", "libunicode", "dtoa"].map(name => path.join(quickjs, name + ".c")), "-lm", "-lpthread", "-o", binary], { encoding: "utf8", timeout: 120000 });
  assert.equal(compile.status, 0, compile.stderr || compile.error?.message);
  const run = spawnSync(binary, [source], { encoding: "utf8", timeout: 30000 });
  assert.equal(run.status, 0, run.stderr || run.error?.message);
  process.stdout.write(run.stdout);
} finally {
  await rm(temporary, { recursive: true, force: true });
}
