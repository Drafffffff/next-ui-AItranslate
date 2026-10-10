import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
const b=await build({entryPoints:[new URL('agent.ts',import.meta.url).pathname],bundle:true,write:false,format:'iife',platform:'neutral',globalName:'Agent'});
const ctx=vm.createContext({});vm.runInContext(b.outputFiles[0].text,ctx);const {createGameAgent}=ctx.Agent;
const wire=(delta,reason=null)=>'data: '+JSON.stringify({choices:[{delta,finish_reason:reason}]})+'\n\n';
const response=(message,reason='stop')=>({status:200,text:JSON.stringify({choices:[{message,finish_reason:reason}]})});
const base={endpoint:'https://example.invalid',modelId:'deepseek-flash',key:'fixture-secret',wait:async()=>{},tools:[]};
let requests=0,executions=0,starts=0;const bodies=[],retryEvents=[];
const a=createGameAgent({...base,tools:[{name:'save',description:'Save once.',parameters:{type:'object',properties:{},additionalProperties:false},execute:async()=>{executions++;return 'saved';}}],
 request(){throw Error('unexpected');},requestStream(url,key,body,timeout,feed){
  requests++;bodies.push(JSON.parse(body));assert.equal(bodies.at(-1).thinking.type,'enabled');
  if(requests===1){feed(wire({content:'discard this attempt',reasoning_content:'discard reasoning',tool_calls:[{index:0,id:'stale',function:{name:'save',arguments:'{}'}}]}));return {promise:Promise.reject(Error('连接被重置（curl 56）')),cancel(){}};}
  if(requests===2){const text=wire({reasoning_content:'real reasoning'})+wire({tool_calls:[{index:0,id:'real',function:{name:'save',arguments:'{}'}}]},'tool_calls')+'data: [DONE]\n\n';feed(text);return {promise:Promise.resolve({status:200,text}),cancel(){}};}
  assert.equal(bodies.at(-1).messages.find(m=>m.tool_calls)?.reasoning_content,'real reasoning');
  if(requests===3)return {promise:Promise.resolve({status:503,text:'temporary'}),cancel(){}};
  return {promise:Promise.resolve(response({content:'done',reasoning_content:'final reasoning'})),cancel(){}};
 },onEvent(e){if(e.type==='retry')retryEvents.push(e);if(e.type==='message_start'&&e.message.role==='assistant')starts++;}});
await a.prompt('create');assert.equal(executions,1);assert.equal(requests,4);assert.equal(starts,2);assert.equal(retryEvents.length,2);
assert.equal(a.messages.length,4);assert(!JSON.stringify(a.messages).includes('discard'));assert(!JSON.stringify(a.messages).includes('stale'));
assert.equal(a.messages.at(-1).stopReason,'stop');
assert.deepEqual(bodies[0],bodies[1],'retry must send the exact pre-tool request');assert.deepEqual(bodies[2],bodies[3],'post-tool retry must not replay the tool');
for(const [error,total] of [[Error('网络连接失败'),4],[Error('请求超时'),4],[{status:429,text:''},4],[{status:401,text:''},1],[{status:400,text:''},1],[Error('证书校验失败（curl 60）'),1],[Error('响应内容过大'),1]]){
 let n=0;const work=createGameAgent({...base,request(){n++;return {promise:error instanceof Error?Promise.reject(error):Promise.resolve(error),cancel(){}};}});
 await work.prompt('x');assert.equal(n,total);assert.equal(work.messages.at(-1).stopReason,'error');
}
let waiting,transportCalls=0;const cancel=createGameAgent({...base,wait:(ms,signal)=>new Promise((resolve,reject)=>{waiting=()=>reject(Error('Operation aborted'));signal.addEventListener('abort',waiting);}),request(){transportCalls++;return {promise:Promise.reject(Error('网络连接失败')),cancel(){}};}});
const task=cancel.prompt('x');for(let i=0;i<100&&!waiting;i++)await Promise.resolve();assert(waiting);cancel.abort();await task;assert.equal(transportCalls,1);assert.equal(cancel.messages.at(-1).stopReason,'aborted');
console.log('PASS: thinking round-trip, bounded transient retries, unchanged request replay, partial SSE reset, tools execute once, nonretryable errors, abort during backoff.');
