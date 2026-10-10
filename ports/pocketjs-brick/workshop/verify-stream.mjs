import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
const bundle=await build({entryPoints:[new URL('agent.ts',import.meta.url).pathname],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Agent'});
const ctx=vm.createContext({});vm.runInContext(bundle.outputFiles[0].text,ctx);const {createGameAgent}=ctx.Agent;
const sse=value=>'data: '+(typeof value==='string'?value:JSON.stringify(value))+'\r\n\r\n';
const chunk=(delta,finish_reason=null)=>({choices:[{index:0,delta,finish_reason}]});
const base={wait:async()=>{},modelId:'deepseek-flash',endpoint:'https://example.invalid/v1',key:'fixture-secret'};
let executed=0,requests=0,live=0,settled=false;
const text='正在编写小猫游戏。🎮';const args=JSON.stringify({speed:2});
const frames=[chunk({content:text.slice(0,4)}),chunk({content:text.slice(4)}),chunk({tool_calls:[{index:0,id:'call-1',type:'function',function:{name:'save_game',arguments:args.slice(0,4)}}]}),chunk({tool_calls:[{index:0,function:{arguments:args.slice(4)}}]}),chunk({},'tool_calls'),{choices:[],usage:{prompt_tokens:20,completion_tokens:10,total_tokens:30}}];
const wire=frames.map(sse).join('')+sse('[DONE]');
const requestStream=(url,key,body,timeout,onChunk)=>{
 const b=JSON.parse(body);assert.equal(b.stream,true);requests++;
 if(requests===2)return {promise:Promise.resolve({status:200,text:JSON.stringify({choices:[{message:{content:'已保存。'},finish_reason:'stop'}]})}),cancel(){}};
 let cancel=false;return {promise:(async()=>{for(let i=0;i<wire.length;i+=7){if(cancel)throw new Error('Cancelled');onChunk(wire.slice(i,i+7));if(i%49===0)await new Promise(r=>setTimeout(r,1));}settled=true;return {status:200,text:wire};})(),cancel(){cancel=true;}};
};
const agent=createGameAgent({...base,request:()=>{throw new Error('Unexpected nonstream request');},requestStream,tools:[{name:'save_game',description:'Save.',parameters:{type:'object',properties:{speed:{type:'number'}},required:['speed']},execute:async a=>{assert(settled,'Partial tool executed before HTTP completion');assert.equal(a.speed,2);executed++;return 'saved';}}],onEvent:e=>{if(e.type==='message_update'&&!settled)live++;}});
await agent.prompt('Create');assert.equal(executed,1);assert.equal(requests,2);assert(live>0,'No text events before response settled');assert.equal(agent.messages[1].content.find(p=>p.type==='text').text,text);assert.equal(agent.messages[1].usage.totalTokens,30);
// Native SSE bodies reclaim delivered bytes. Completion contains only the
// undelivered tail, which can start mid-frame or be empty after [DONE].
for(const split of [0,7,wire.length-9,wire.length]){
 let calls=0,turn=0;
 const tailed=createGameAgent({...base,request:()=>{throw Error('unexpected');},requestStream:(_u,_k,_b,_t,feed)=>{
  turn++;
  const data=turn===1?wire:sse(chunk({content:'已保存。'}))+sse(chunk({},'stop'))+sse('[DONE]');
  const cut=turn===1?split:data.length;
  feed(data.slice(0,cut));return {promise:Promise.resolve({status:200,text:data.slice(cut),streamTail:true}),cancel(){}};
 },tools:[{name:'save_game',description:'Save.',parameters:{type:'object',properties:{speed:{type:'number'}},required:['speed']},execute:async a=>{assert.equal(a.speed,2);calls++;return 'saved';}}]});
 await tailed.prompt('Create');assert.equal(calls,1);assert.equal(tailed.messages[1].content.find(p=>p.type==='text').text,text);assert.equal(tailed.messages[1].usage.totalTokens,30);
}
// Real parser, a coalesced delivery of many short lines. The time budget detects
// accidental per-line copies of the entire remaining 3 MiB suffix.
const comments=(': '+ 'x'.repeat(1020)+'\n\n').repeat(3072);
const batchStarted=performance.now();
const batched=createGameAgent({...base,request:()=>{throw Error('unexpected');},requestStream:(_u,_k,_b,_t,feed)=>{
 const complete=sse(chunk({content:'批量解析完成 🎮'}))+sse(chunk({},'stop'))+sse('[DONE]');
 feed(comments+complete);return {promise:Promise.resolve({status:200,text:'',streamTail:true}),cancel(){}};
},tools:[]});
await batched.prompt('Parse');assert.equal(batched.messages.at(-1).content[0].text,'批量解析完成 🎮');assert(performance.now()-batchStarted<1500,'Coalesced SSE parsing exceeded 1.5s on the desktop test host');
// A complete-looking argument fragment is still unsafe after a disconnect.
let unsafe=0;const incomplete=sse(chunk({content:'已开始，连接随后中断。'}))+sse(chunk({tool_calls:[{index:0,id:'bad',type:'function',function:{name:'save_game',arguments:args}}]}));
const broken=createGameAgent({...base,request:requestStream,requestStream:(_u,_k,_b,_t,feed)=>{feed(incomplete);return {promise:Promise.resolve({status:200,text:incomplete}),cancel(){}};},tools:[{name:'save_game',description:'Save.',parameters:{type:'object',properties:{}},execute:async()=>{unsafe++;return 'bad';}}]});
await broken.prompt('Create');assert.equal(unsafe,0);assert.equal(broken.messages.at(-1).stopReason,'error');assert.match(broken.messages.at(-1).errorMessage,/中断/);assert(broken.messages.at(-1).content.some(p=>p.type==='text'));
let stop=false;const cancelled=createGameAgent({...base,request:requestStream,requestStream:(_u,_k,_b,_t,feed)=>{feed(sse(chunk({content:'保留已生成的部分。'})));return {promise:new Promise(()=>{}),cancel(){stop=true;}};},tools:[]});
const work=cancelled.prompt('Create');for(let i=0;i<20;i++)await Promise.resolve();cancelled.abort();await work;assert(stop);assert.equal(cancelled.messages.at(-1).stopReason,'aborted');assert(cancelled.messages.at(-1).content.some(p=>p.type==='text'));
for(const streaming of [false,true]){
 const payload={choices:[{message:{content:null,reasoning_content:'仍在推演'},finish_reason:'length'}],usage:{prompt_tokens:10,completion_tokens:64,total_tokens:74}};
 const body=sse(chunk({reasoning_content:'仍在推演'}))+sse(chunk({},'length'))+sse({choices:[],usage:payload.usage})+sse('[DONE]');
 const capped=createGameAgent({...base,maxOutputTokens:64,tools:[],request:()=>({promise:Promise.resolve({status:200,text:JSON.stringify(payload)}),cancel(){}}),...(streaming?{requestStream:(_u,_k,_b,_t,feed)=>{feed(body);return {promise:Promise.resolve({status:200,text:'',streamTail:true}),cancel(){}};}}:{})});
 await capped.prompt('Create');const last=capped.messages.at(-1);assert.equal(last.stopReason,'error');assert.match(last.errorMessage,/64 token 输出上限/);assert.equal(last.usage.output,64);assert.equal(last.usage.totalTokens,74);
}
console.log('PASS: real Pi streaming text before HTTP completion, 7-character SSE splits/CRLF/Chinese/emoji, fragmented tool JSON, usage, JSON fallback, disconnect never executes partial tools, cancellation retains text.');
