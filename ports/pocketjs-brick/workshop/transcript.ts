import {displayText} from '../app/display-text.ts';
import type {GameMessage} from './agent.ts';
import {TOOL_LABELS,toolText,messageText} from './conversation.ts';
import {markdown,rangeAt,blockWindow,type MarkdownDocument,type MarkdownStyle} from './markdown.ts';
type Kind='user'|'assistant'|'tool';
interface Entry {id:string;kind:Kind;message:GameMessage;call?:any;result?:any;version:number;block?:number}
export interface Position {block:number;offset:number}
export interface TranscriptRun {text:string;x:number;width:number;flags:number}
export interface TranscriptRow {text:string;kind:Kind|'heading'|'space';height:number;format?:MarkdownStyle;runs?:TranscriptRun[];indent?:number}
export interface TranscriptPage {rows:TranscriptRow[];start:Position;end:Position}
type Window={lines:string[];start:number;end:number;widths?:number[][]};
type Wrap=(text:string,maxLines:number,reverse:boolean,width?:number)=>Promise<Window>;
const CHUNK=2048,LINE=44,GAP=12;
/** Append-only metadata index. No historical bodies, JSON or glyphs are visited
 * on a new message. Text/line caches have a byte budget and visible-only scope. */
export class Transcript {
 private entries:Entry[]=[];private results=new Map<string,any>();private calls=new Map<string,Entry>();
 private messages:GameMessage[]=[];private indexed=0;private first?:GameMessage;private live?:Entry;
 private texts=new Map<string,MarkdownDocument>();private chars=0;private windows=new Map<string,Window>();
 private large?:{key:string;document:MarkdownDocument};
 private jobs=0;private bodies=0;private scanned=0;private generation=0;
 constructor(private wrap:Wrap){}
 get length(){return this.entries.length+(this.live?1:0);}
 clear(){this.entries=[];this.results.clear();this.calls.clear();this.indexed=0;this.first=undefined;this.live=undefined;this.messages=[];this.texts.clear();this.windows.clear();this.chars=0;this.large=undefined;this.generation++;}
 sync(messages:GameMessage[]){this.syncUntil(messages,messages.length);}
 async hydrate(messages:GameMessage[],yieldFrame:()=>Promise<void>){
  if(messages.length<this.indexed||messages[0]!==this.first&&this.indexed)this.clear();
  while(this.indexed<messages.length){this.syncUntil(messages,Math.min(messages.length,this.indexed+128));await yieldFrame();}
 }
 private syncUntil(messages:GameMessage[],end:number){
  if(messages.length<this.indexed||messages[0]!==this.first&&this.indexed)this.clear();
  this.messages=messages;this.first=messages[0];
  for(;this.indexed<end;this.indexed++){
   const m=messages[this.indexed];this.scanned++;
   if(m.role==='toolResult'){this.results.set(m.toolCallId,m);const e=this.calls.get(m.toolCallId);if(e){e.result=m;e.version++;}continue;}
   if(m.role==='user'){this.entries.push({id:'u'+this.indexed,kind:'user',message:m,version:0});continue;}
   if(m.role!=='assistant')continue;
   if(m.content.some(c=>c.type==='text'&&c.text))this.entries.push({id:'a'+this.indexed,kind:'assistant',message:m,version:0});
   for(const call of m.content.filter(c=>c.type==='toolCall')){const e:Entry={id:call.id,kind:'tool',message:m,call,result:this.results.get(call.id),version:0,block:this.entries.length};this.entries.push(e);this.calls.set(call.id,e);}
   if(m.stopReason==='error'||m.stopReason==='aborted')this.entries.push({id:'e'+this.indexed,kind:'assistant',message:m,version:0});
  }
 }
 setLive(message?:GameMessage){
  if(!message||message.role!=='assistant'||!message.content.some(c=>c.type==='text'&&c.text)){this.live=undefined;return;}
  this.live={id:'live',kind:'assistant',message,version:++this.generation};
 }
 private entry(n:number){return n===this.entries.length?this.live!:this.entries[n];}
 private heading(e:Entry,active:Set<string>){
  if(e.kind==='tool')return (TOOL_LABELS[e.call.name]||e.call.name)+' · '+(e.result?(e.result.isError?'失败':'完成'):active.has(e.id)?'进行中':'未完成');
  if(e.id[0]==='e'){const m=e.message as any;return m.stopReason==='aborted'?'已取消':'Agent 遇到问题';}
  return e.kind==='user'?messageText(e.message.content).startsWith('[宿主恢复中断任务]')?'恢复任务':'你':'Agent';
 }
 private document(n:number,details:boolean){
  const e=this.entry(n),key=e.id+':'+e.version+':'+Number(details);let doc=this.texts.get(key);
  if(this.large?.key===key)return this.large.document;
  if(doc!==undefined){this.texts.delete(key);this.texts.set(key,doc);return doc;}
  this.bodies++;
  const text=(e.kind==='tool'?toolText(e.call,e.result,details):e.id[0]==='e'?(e.message as any).errorMessage||'本次操作未完成，进度已保存':messageText(e.message.content)).trim();
  doc=markdown(displayText(text),e.kind!=='tool'&&e.id[0]!=='e');
  // One large visible body may be retained; never pretty-print it every page.
  if(text.length<=65536){this.texts.set(key,doc);this.chars+=doc.text.length+doc.ranges.length*48;
   while(this.chars>131072||this.texts.size>24){const k=this.texts.keys().next().value!,d=this.texts.get(k)!;this.chars-=d.text.length+d.ranges.length*48;this.texts.delete(k);}}
  else this.large={key,document:doc};
  return doc;
 }
 private body(n:number,details:boolean){return this.document(n,details).text;}
 oldest():Position{return {block:0,offset:-1};}
 tail(details=false):Position{return this.length?{block:this.length-1,offset:this.body(this.length-1,details).length}:{block:0,offset:-1};}
 atEnd(p:Position,details=false){return !this.length||p.block>=this.length||p.block===this.length-1&&p.offset>=this.body(p.block,details).length;}
 async page(from:Position,reverse:boolean,height:number,details:boolean,active:Set<string>,valid=()=>true):Promise<TranscriptPage>{
  let p={...from},budget=height;const rows:TranscriptRow[]=[];
  const start={...from};let steps=0;
  const add=(text:string,kind:TranscriptRow['kind'],format?:MarkdownStyle,runs?:TranscriptRun[],indent=0)=>{const row={text,kind,height:kind==='space'||format?.block==='rule'||!text&&format?.block!=='code'?GAP:LINE,format,runs,indent};budget-=row.height;reverse?rows.unshift(row):rows.push(row);};
  while(budget>=LINE&&p.block>=0&&p.block<this.length&&steps++<40){
   if(!valid())break;
   const e=this.entry(p.block),doc=this.document(p.block,details),body=doc.text;
   p.offset=Math.min(body.length,p.offset);
   if(reverse&&p.offset<0){p.block--;if(p.block>=0){p.offset=this.body(p.block,details).length;add('','space');}continue;}
   if(!reverse&&p.offset<0){add(this.heading(e,active),'heading');p.offset=0;continue;}
   if(reverse&&p.offset===0){add(this.heading(e,active),'heading');p.offset=-1;continue;}
   if(!reverse&&p.offset>=body.length){p.block++;p.offset=-1;if(p.block<this.length)add('','space');continue;}
   const span=blockWindow(doc,p.offset,reverse,CHUNK),format=span.style;
   const indent=format.block==='code'?16:format.block==='quote'?16+format.depth*12:format.block==='list'?12+format.depth*20:format.block==='table'?8:0;
   let a=span.start,b=span.end;
   if(reverse&&a>0&&(body.charCodeAt(a)>=0xdc00&&body.charCodeAt(a)<=0xdfff||body[a]==='\n'&&body[a-1]==='\r'))a--;
   if(!reverse&&b<body.length&&(body.charCodeAt(b-1)>=0xd800&&body.charCodeAt(b-1)<=0xdbff||body[b-1]==='\r'&&body[b]==='\n'))b--;
   const slice=body.slice(a,b),limit=Math.min(10,Math.floor(budget/LINE));
   const width=888-indent-(format.block==='code'?16:0);
   const key=e.id+':'+e.version+':'+Number(details)+':'+a+':'+b+':'+limit+':'+Number(reverse)+':'+width;
   let window=this.windows.get(key);
   if(window){this.windows.delete(key);this.windows.set(key,window);}else{
    this.jobs++;window=await this.wrap(slice,limit,reverse,width);if(!valid())break;
    this.windows.set(key,window);while(this.windows.size>64)this.windows.delete(this.windows.keys().next().value!);
   }
   let offset=a+window.start;
   const lineRows=window.lines.map((text,i)=>{
    const start=offset;offset+=text.length;if(body[offset]==='\n')offset++;
    const runs:TranscriptRun[]=[];const widths=window!.widths?.[i];
    if(widths){let at=start;while(at<start+text.length){const r=rangeAt(doc,at),end=Math.min(start+text.length,r?.end??start+text.length),lo=at-start,hi=end-start;runs.push({text:text.slice(lo,hi),x:widths[lo],width:widths[hi]-widths[lo],flags:r?.flags||0});at=end;}}
    return {text,runs};
   });
   if(reverse){for(let i=lineRows.length-1;i>=0;i--)add(lineRows[i].text,e.kind,format,lineRows[i].runs,indent);p.offset=a+window.start;}
   else{for(const row of lineRows)add(row.text,e.kind,format,row.runs,indent);p.offset=a+window.end;}
   if(!window.lines.length||(reverse?p.offset>=b:p.offset<=a))break;
  }
  if(reverse&&p.block<0)p=this.oldest();
  return reverse?{rows,start:p,end:start}:{rows,start,end:p};
 }
 toolPosition(id:string):Position|undefined{const e=this.calls.get(id);return e?{block:e.block!,offset:-1}:undefined;}
 stats(){return {indexed:this.scanned,blocks:this.length,bodies:this.bodies,wrapJobs:this.jobs,textChars:this.chars,largeBodyChars:this.large?.document.text.length||0,windows:this.windows.size};}
}
