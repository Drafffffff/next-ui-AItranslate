import assert from 'node:assert/strict';
import {build} from 'esbuild';
import vm from 'node:vm';
import {performance} from 'node:perf_hooks';
const bundle=await build({stdin:{contents:"export {Transcript} from './transcript.ts';export {conversation} from './conversation.ts';export * from './markdown.ts';",resolveDir:new URL('.',import.meta.url).pathname},bundle:true,write:false,platform:'neutral',format:'iife',globalName:'View'});
const ctx=vm.createContext({});vm.runInContext(bundle.outputFiles[0].text,ctx);const {Transcript,conversation,markdown,STRONG,CODE,LINK}=ctx.View;
let jobs=0,chars=0;
const wrap=async(text,maxLines,reverse,maxWidth=888)=>{
 jobs++;chars+=text.length;const lines=[];let start=0,width=0;
 for(let i=0;i<text.length;i++){
  if(text[i]==='\n'){lines.push({text:text.slice(start,i),start,end:i+1});start=i+1;width=0;}
  else{const advance=text.charCodeAt(i)>127?2:1;if(width+advance>56*maxWidth/888){lines.push({text:text.slice(start,i),start,end:i});start=i;width=0;}width+=advance;}
 }
 if(start<text.length||!lines.length)lines.push({text:text.slice(start),start,end:text.length});
 const page=reverse?lines.slice(-maxLines):lines.slice(0,maxLines);
 return {lines:page.map(p=>p.text),widths:page.map(p=>{let x=0;return [0,...Array.from(p.text,c=>x+=(c.charCodeAt(0)>127?2:1)*15)];}),start:page[0].start,end:page.at(-1).end};
};
const assistant=body=>({role:'assistant',content:[{type:'text',text:body}],stopReason:'stop'});
const history=Array.from({length:20000},(_,i)=>i%2?assistant('历史回复 '+i+'：方向键移动，O 重开。\n\n'):{role:'user',content:'历史需求 '+i+'：继续完善玩法。'});
const before=performance.now();const blocks=conversation(history,new Set());for(const b of blocks)if(b.text)await wrap(b.text,100000,false);const baseline={ms:performance.now()-before,jobs,chars};
jobs=0;chars=0;const transcript=new Transcript(wrap),begin=performance.now();transcript.sync(history);let page=await transcript.page(transcript.tail(),true,468,false,new Set());
const optimized={ms:performance.now()-begin,jobs,chars};assert(jobs<12);assert(page.rows.length<=20);assert.equal(transcript.stats().indexed,20000);
const initialJobs=jobs;await transcript.page(transcript.tail(),true,468,false,new Set());assert.equal(jobs,initialJobs,'Cached viewport rewrapped');
history.push({role:'user',content:'新增需求'},assistant('最新回复'));transcript.sync(history);assert.equal(transcript.stats().indexed,20002,'Append rescanned history');
page=await transcript.page(transcript.tail(),true,468,false,new Set());assert(page.rows.some(r=>r.text==='最新回复'));
for(let i=0;i<160;i++)page=await transcript.page(page.start,true,468,false,new Set());
assert(transcript.stats().windows<=64);assert(transcript.stats().textChars<=131072);
const huge='LONG-BEGIN-'+('abcdefgh'.repeat(9000))+'-LONG-END',single=new Transcript(wrap);single.sync([assistant(huge)]);
let cursor=single.oldest(),collected='',count=0;
while(cursor.block===0&&count++<1000){const p=await single.page(cursor,false,468,false,new Set());collected+=p.rows.filter(r=>r.kind==='assistant').map(r=>r.text).join('');assert(JSON.stringify(p.end)!==JSON.stringify(cursor));cursor=p.end;}
assert.equal(collected,huge,'Pagination lost or duplicated long-message content');
const tail=await single.page(single.tail(),true,468,false,new Set());assert(tail.rows.some(r=>r.text.includes('LONG-END')),'Long message was truncated at 16000 characters');
const error='Validation failed for tool "write_game":\n - notes: fewer than 32 items\n\nReceived arguments:\n'+('  [0,0,0,0],\n'.repeat(1400));
const fail=new Transcript(wrap);fail.sync([{role:'assistant',content:[{type:'toolCall',id:'bad',name:'write_game',arguments:{}}],stopReason:'toolUse'},{role:'toolResult',toolCallId:'bad',isError:true,content:[{type:'text',text:error}]}]);
const summary=await fail.page(fail.oldest(),false,468,false,new Set());assert(summary.rows.some(r=>r.text.includes('完整错误')));assert(!summary.rows.some(r=>r.text.includes('[0,0,0,0]')));
const details=await fail.page(fail.tail(true),true,468,true,new Set());assert(details.rows.length<=20);
// Formatting is display-only. Native advances determine inline positions,
// including CJK text; both page directions retain code/list/quote styles.
const rich='# 游戏计划\n\n先做 **手感**，再加 `btn(0)` 和 [参考](https://example.invalid/game)。\n\n- [x] 已完成移动\n- [ ] 添加敌人\n  - 子任务\n\n> 保留当前规则\n\n```lua\nfunction _draw()\n  print("**literal**", 10, 20)\nend\n```\n\n| 功能 | 状态 |\n| --- | --- |\n| 玩法 | 完成 |\n\n---\n\n结束。';
const parsed=markdown(rich);assert(!parsed.text.includes('# 游戏'));assert(!parsed.text.includes('```'));assert(parsed.text.includes('**literal**'));assert(parsed.text.includes('https://example.invalid/game'));assert(parsed.ranges.some(r=>r.flags&STRONG));assert(parsed.ranges.some(r=>r.flags&LINK));assert(parsed.ranges.some(r=>r.flags&CODE));assert(parsed.ranges.some(r=>r.block==='table'));assert(parsed.ranges.some(r=>r.block==='quote'));
assert.equal(markdown('use _update and _draw, a_b_c').text,'use _update and _draw, a_b_c');
assert.equal(markdown('\\*literal\\* and `**code**`').text,'*literal* and **code**');
assert.equal(markdown('<script>alert(1)</script>').text,'<script>alert(1)</script>');
assert.equal(markdown('**still streaming').text,'**still streaming');assert.equal(markdown('```lua\nprint("ok")').text,'lua\nprint("ok")');
const formatted=new Transcript(wrap),saved=assistant(rich);formatted.sync([saved]);let pos=formatted.oldest(),richRows=[];
for(let i=0;i<40&&pos.block===0;i++){const next=await formatted.page(pos,false,176,false,new Set());assert.notDeepEqual(next.end,pos);assert(next.rows.reduce((n,r)=>n+r.height,0)<=176);richRows.push(...next.rows);pos=next.end;}
assert(richRows.some(r=>r.format?.block==='heading'));assert(richRows.some(r=>r.format?.block==='code'));assert(richRows.some(r=>r.format?.block==='list'));assert(richRows.some(r=>r.runs?.some(s=>s.flags&STRONG)));assert.equal(saved.content[0].text,rich,'Rendering modified model history');
for(const row of richRows)for(const run of row.runs||[])assert(run.x>=0&&run.width>=0&&run.x+run.width+(row.indent||0)<=890,'Inline run exceeds viewport');
pos=formatted.tail();let backwards=[];
for(let i=0;i<40&&pos.offset!==-1;i++){const next=await formatted.page(pos,true,176,false,new Set());assert.notDeepEqual(next.start,pos);backwards.unshift(...next.rows);pos=next.start;}
assert.equal(backwards.filter(r=>r.kind==='assistant').map(r=>r.text).join(''),richRows.filter(r=>r.kind==='assistant').map(r=>r.text).join(''),'Markdown pagination lost content');
const codeText='```lua\n'+Array.from({length:2000},(_,i)=>'-- tile '+i+' **literal**').join('\n')+'\n```';
const codePages=new Transcript(wrap);codePages.sync([assistant(codeText)]);const codeTail=await codePages.page(codePages.tail(),true,468,false,new Set());assert(codeTail.rows.some(r=>r.text.includes('tile 1999 **literal**')));assert(codeTail.rows.filter(r=>r.kind==='assistant').every(r=>r.format.block==='code'));assert(markdown(codeText).ranges.length<=2,'Code block created per-line metadata');
const rawTools=new Transcript(wrap);rawTools.sync([{role:'assistant',content:[{type:'toolCall',id:'raw',name:'other',arguments:{}}],stopReason:'toolUse'},{role:'toolResult',toolCallId:'raw',content:[{type:'text',text:'**raw**\n# code\n```'}]}]);const rawPage=await rawTools.page(rawTools.oldest(),false,468,false,new Set());assert(rawPage.rows.some(r=>r.text==='**raw**'));
const live=new Transcript(wrap);live.setLive(assistant('## 正在生成\n```lua\nprint("A")'));let livePage=await live.page(live.tail(),true,468,false,new Set());assert(livePage.rows.some(r=>r.format?.block==='code'));live.setLive(assistant('## 正在生成\n```lua\nprint("A")\n```\n**完成**'));livePage=await live.page(live.tail(),true,468,false,new Set());assert(livePage.rows.some(r=>r.text==='完成'&&r.runs.some(s=>s.flags&STRONG)));
const stats=transcript.stats();console.log(JSON.stringify({baseline,optimized,historyMessages:history.length,longMessageChars:huge.length,completePages:count,stats},null,2));
console.log('PASS: Markdown blocks/inline runs/CJK/streaming, 2000-line literal code, display-only formatting; 20,000-message index, visible-only wrapping, bounded caches, complete 72KB pagination, 1400-line error summary/details.');
