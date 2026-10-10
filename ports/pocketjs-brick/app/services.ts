import {displayText,emojiCodepoint} from './display-text.ts';
import { onFrame } from "@pocketjs/framework/solid/lifecycle";
interface Result { id: number; error: string; text: string; status: number; lines: string[];widths?:number[][];progress?:boolean;streamTail?:boolean;start:number;end:number }
interface Native { serviceMetrics?():{httpReceivedBytes:number;httpReclaimedBytes:number;httpPeakBufferBytes:number;httpMaxProgressBytes:number;frameMaxMs:number;peakRssKiB:number} }
interface Native { sha256?(text:string):string;start(json: string): number; startWrite?(name:string,text:string):number; beginWrite?(name:string):number;appendWrite?(id:number,chunk:string):void;finishWrite?(id:number):number;cancel(id: number): void; poll(): Result[];metrics():{textJobs:number;atlasBuilds:number;atlasUploads:number;glyphs:number;frameMaxMs:number;peakRssKiB:number}; testing?: boolean; testUrl?: string; testTlsUrl?: string }
export const native = (globalThis as unknown as { brick: Native }).brick;
const waiting = new Map<number, { resolve: (value: Result) => void; reject: (error: Error) => void;onChunk?:(chunk:string)=>void }>();
const delays=new Set<{until:number;finish:()=>void}>();
/** Uses frames rather than timers or native workers; abort also wakes immediately. */
export function delay(ms=0,signal?:AbortSignal):Promise<void>{
 return new Promise((resolve,reject)=>{
  const abort=()=>{delays.delete(item);signal?.removeEventListener('abort',abort);reject(new Error('Operation aborted'));};
  const item={until:Date.now()+ms,finish:()=>{delays.delete(item);signal?.removeEventListener('abort',abort);resolve();}};
  if(signal?.aborted){reject(new Error('Operation aborted'));return;}
  delays.add(item);signal?.addEventListener('abort',abort,{once:true});
 });
}
export function installServices() {
  onFrame(() => {
    const now=Date.now();for(const item of delays)if(now>=item.until)item.finish();
    if (!waiting.size) return;
    for (const result of native.poll()) {
      const task = waiting.get(result.id); if (!task) continue;
      if(result.progress){task.onChunk?.(result.text);continue;}
      waiting.delete(result.id);
      if (result.error) task.reject(new Error(result.error)); else task.resolve(result);
    }
  });
}
export function task(request: Record<string, unknown>,onChunk?:(chunk:string)=>void) {
  const id = native.start(JSON.stringify(request));
  return {
    id,
    cancel: () => native.cancel(id),
    promise: new Promise<Result>((resolve, reject) => waiting.set(id, { resolve, reject,onChunk })),
  };
}
export const read = async (name: string) => (await task({ op: "read", name }).promise).text;
export const write = (name: string, text: string) => {
 if(!native.startWrite)return task({op:'write',name,text}).promise;
 const id=native.startWrite(name,text);
 return new Promise<Result>((resolve,reject)=>waiting.set(id,{resolve,reject}));
};
/** Assemble bounded UTF-8 chunks natively, then atomically commit one disk job.
 * No full project JSON string or JSON request envelope is built on the UI thread. */
export async function writeChunks(name:string,produce:(append:(chunk:string)=>void)=>Promise<void>){
 if(!native.beginWrite||!native.appendWrite||!native.finishWrite){const parts:string[]=[];await produce(part=>parts.push(part));return write(name,parts.join(''));}
 const id=native.beginWrite(name);
 try{await produce(chunk=>native.appendWrite!(id,chunk));native.finishWrite(id);return await new Promise<Result>((resolve,reject)=>waiting.set(id,{resolve,reject}));}
 catch(error){native.cancel(id);throw error;}
}
export const cleanText = (text: string) => Array.from(displayText(text).replace(/\r\n?/g, "\n").replace(/\t/g, "  "))
  .map(c => { const n = c.codePointAt(0)!; return n === 10 || (n >= 32 && n < 0xfffd && !(n >= 0xfe00 && n <= 0xfe0f)) ? c : "?"; }).join("");
export const prepare = async (text: string) => (await task({ op: "text", text: cleanText(text).slice(0, 16000) }).promise).lines;
/** Bounded visible slice, with source offsets preserved across CRLF/tabs/emoji. */
export async function prepareWindow(raw:string,maxLines:number,reverse=false,width=888){
  if(!/[\u0000-\u0009\u000b-\u001f\ud800-\udfff\ufffd-\uffff]/.test(raw)&&displayText(raw)===raw){
   const result=await task({op:'text-page',text:raw,maxLines,reverse,width}).promise;
   return {lines:result.lines,widths:result.widths,start:result.start,end:result.end};
  }
  const parts:string[]=[];let source=0;const map=[0];
  for(const character of raw){
    const cp=character.codePointAt(0)!,length=character.length;
    if(emojiCodepoint(cp)){source+=length;map[map.length-1]=source;continue;}
    if(character==='\n'&&source>0&&raw[source-1]==='\r'){source+=length;map[map.length-1]=source;continue;}
    const value=character==='\r'?'\n':character==='\t'?'  ':cp===10||(cp>=32&&cp<0xfffd&&!(cp>=0xfe00&&cp<=0xfe0f))?character:'?';
    source+=length;parts.push(value);for(let i=0;i<value.length;i++)map.push(source);
  }
  const result=await task({op:'text-page',text:parts.join(''),maxLines,reverse,width}).promise;
  return {lines:result.lines,widths:result.widths,start:map[result.start]??0,end:map[result.end]??raw.length};
}
export function request(url: string, key = "", body = "", timeoutMs = 30000) {
  return task({ op: "http", url, key, text: body, method: body ? "POST" : "GET", timeoutMs });
}
export function requestStream(url:string,key:string,body:string,timeoutMs:number,onChunk?:(chunk:string)=>void){
  return task({op:'http',url,key,text:body,method:'POST',timeoutMs,stream:true},onChunk);
}

export const hasSharedCredential = async (url: string) => (await task({ op: "credentials", url }).promise).text === "yes";
