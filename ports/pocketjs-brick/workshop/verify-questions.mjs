import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
import {fileURLToPath} from 'node:url';
const dir=fileURLToPath(new URL('.',import.meta.url));
const files=new Map(),roms=new Map();let replies=[],callId=0,requests=[];
const result=(name,args={})=>({status:200,text:JSON.stringify({choices:[{message:{role:'assistant',content:name?null:'依据你的选择继续。',...(name?{tool_calls:[{id:'question-'+(++callId),type:'function',function:{name,arguments:JSON.stringify(args)}}]}:{})},finish_reason:name?'tool_calls':'stop'}]})});
const harness={read:async n=>files.get(n),write:async(n,s)=>files.set(n,s),task:i=>{if(i.op==='syntax')return {promise:Promise.resolve({text:'syntax_checked'})};roms.set(i.name,i.text);return {promise:Promise.resolve({text:''})};},request:(u,k,b)=>{requests.push(JSON.parse(b));assert(replies.length);return {promise:Promise.resolve(replies.shift()()),cancel(){}};}};
const bundled=await build({entryPoints:[dir+'model.ts'],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Model',plugins:[{name:'native',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'native',namespace:'native'}));b.onLoad({filter:/.*/,namespace:'native'},()=>({contents:'export const read=(...a)=>globalThis.__harness.read(...a);export const write=(...a)=>globalThis.__harness.write(...a);export const task=(...a)=>globalThis.__harness.task(...a);export const writeChunks=async(n,produce)=>{const parts=[];await produce(p=>parts.push(p));return write(n,parts.join(""));};export const delay=async()=>{};export const requestStream=undefined;export const request=(...a)=>globalThis.__harness.request(...a);'}));}}]});
const ctx=vm.createContext({__harness:harness});vm.runInContext(bundled.outputFiles[0].text,ctx);
const {newProject,initializeProject,decodeProject,answerSavedQuestion,projectAgent,STARTER}=ctx.Model;
const config={key:'fixture',endpoint:'https://example.invalid',model:'deepseek-flash'};
const p=newProject();await initializeProject(p);assert(p.scaffoldReady);assert.equal(p.revision,0);assert(roms.get(p.id).includes(STARTER));assert.deepEqual(Array.from(p.versions,v=>v.revision),[0]);
// Genuine Pi pauses before another request, only actual answer is returned.
let release,asked;replies=[()=>result('ask_user_question',{question:'地牢用实时操作还是回合制？',options:[{label:'实时'},{label:'回合制'}]}),()=>result()];
const work=projectAgent(p,config,()=>{},()=>{},undefined,'',async(q)=>{asked=q;return new Promise(r=>release=r);}).prompt('帮我完善玩法');
for(let i=0;i<100&&!release;i++)await new Promise(r=>setTimeout(r,1));assert(release);assert(p.question);assert.equal(requests.length,1);const saved=decodeProject(files.get('projects/'+p.id+'.json'));assert.equal(saved.question.id,asked.id);
release({answer:'我想要走一步敌人动一步，并保留灯火系统'});await work;assert(!p.question);assert.equal(p.revision,0);assert(requests.at(-1).messages.some(m=>m.role==='tool'&&m.content.includes('保留灯火系统')));
// Power-loss checkpoint can be answered without running any fabricated tool.
await answerSavedQuestion(saved,{answer:'回合制'});assert(!saved.question);assert(saved.messages.some(m=>m.role==='toolResult'&&m.content[0].text.includes('回合制')));await assert.rejects(answerSavedQuestion(saved,{answer:'再答一次'}),/没有/);
// Abort leaves the pending question reopenable; failed result is replaced by real answer.
replies=[()=>result('ask_user_question',{question:'玩法选什么？'})];let waiting=false;
const interrupted=projectAgent(p,config,()=>{},()=>{},undefined,'',async(q,signal)=>{waiting=true;return new Promise((r,j)=>signal.addEventListener('abort',()=>j(new Error('cancelled')),{once:true}));});
const task=interrupted.prompt('新方向');for(let i=0;i<100&&!waiting;i++)await new Promise(r=>setTimeout(r,1));assert(waiting);interrupted.abort();await assert.rejects(task,/aborted|取消/);
const reopened=decodeProject(files.get('projects/'+p.id+'.json'));assert(reopened.question);await answerSavedQuestion(reopened,{answer:'跳过',skipped:true});assert.equal(reopened.messages.filter(m=>m.role==='toolResult'&&m.toolCallId===p.question.id).length,1);
// First AI publication updates the same scaffold ROM to revision 1.
replies=[()=>result('read_game'),()=>result('write_game',{edits:[{oldText:'speed=2',newText:'speed=3'}]}),()=>result('check_game'),()=>result('publish_game'),()=>result()];
await projectAgent(reopened,config,()=>{},()=>{}).prompt('加快移动');assert.equal(reopened.revision,1);assert.equal(roms.size,1);assert.deepEqual(Array.from(reopened.versions,v=>v.revision),[0,1]);
console.log('PASS: playable scaffold, real Pi question suspension/free-form answer, durable reopen, abort/skip pairing, discussion without fabricated publication, same-game revision1.');
