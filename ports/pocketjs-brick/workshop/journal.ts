import type {Project} from './model.ts';
import type {GameMessage} from './agent.ts';
import {contentHash} from './project-state.ts';
import {streamProject} from './persistence.ts';

export interface HistoryPage {file:string;hash:string;count:number}
export interface HistoryManifest {schema:1;count:number;pages:HistoryPage[]}
type Commit={manifest:HistoryManifest;messages:GameMessage[]};
const commits=new WeakMap<Project,Commit>();
export function resetHistory(p:Project){commits.delete(p);}
export function validManifest(m:unknown,id:string):m is HistoryManifest {
 const x=m as HistoryManifest;
 if(!x||x.schema!==1||!Number.isInteger(x.count)||x.count<0||!Array.isArray(x.pages)||x.pages.length>100000)return false;
 let count=0;for(const page of x.pages){
  if(!page||!Number.isInteger(page.count)||page.count<1||page.count>128||!/^[a-f0-9]{64}$/.test(page.hash)||page.file!==`history/${id}-${page.hash.slice(0,24)}.json`)return false;
  count+=page.count;
 }
 return count===x.count;
}
/** Each immutable page is durable before the atomic project manifest points to
 * it. Power loss leaves the old committed manifest or the new complete one.
 * Unreferenced pages are harmless and can be reused by content hash. */
export async function saveJournal(owner:Project,p:Project,write:(name:string,produce:(append:(s:string)=>void)=>Promise<void>)=>Promise<unknown>,yieldFrame:()=>Promise<void>){
 const previous=commits.get(owner);
 // Append is fast for immutable Pi messages. Structural changes (answering a
 // cancelled question removes a result) explicitly reset this checkpoint.
 const append=previous&&p.messages.length>=previous.messages.length&&(!previous.messages.length||previous.messages[0]===p.messages[0]&&previous.messages[previous.messages.length-1]===p.messages[previous.messages.length-1]);
 const pages=append?previous.manifest.pages.slice():[];
 let offset=append?previous.messages.length:0;
 while(offset<p.messages.length){
  const batch:string[]=[];let bytes=0;
  while(offset<p.messages.length&&batch.length<128){
   const raw=JSON.stringify(p.messages[offset]);
   if(batch.length&&bytes+raw.length>262144)break;
   batch.push(raw);bytes+=raw.length;offset++;
  }
  const raw='['+batch.join(',')+']',hash=contentHash(raw),file=`history/${p.id}-${hash.slice(0,24)}.json`;
  await write(file,async push=>{for(let i=0;i<raw.length;){let end=Math.min(i+65536,raw.length);if(end<raw.length&&raw.charCodeAt(end-1)>=0xd800&&raw.charCodeAt(end-1)<=0xdbff)end--;push(raw.slice(i,end));i=end;await yieldFrame();}});
  pages.push({file,hash,count:batch.length});await yieldFrame();
 }
 const history:HistoryManifest={schema:1,count:p.messages.length,pages};
 await write('projects/'+p.id+'.json',push=>streamProject({...p,messages:[],history} as Project,push,yieldFrame));
 commits.set(owner,{manifest:history,messages:p.messages});
}
/** Parse only a bounded immutable page at a time; input/painting can run between
 * disk jobs. No giant full-history JSON parse is required after migration. */
export async function hydrateHistory(p:Project,read:(name:string)=>Promise<string>,yieldFrame:()=>Promise<void>){
 if(!p.history)return p;
 if(!validManifest(p.history,p.id)||p.messages.length)throw new Error('历史清单异常，未覆盖项目');
 const messages:GameMessage[]=[];
 for(const page of p.history.pages){
  const raw=await read(page.file);if(contentHash(raw)!==page.hash)throw new Error('历史片段缺失或校验失败，未覆盖项目');
  const value=JSON.parse(raw);if(!Array.isArray(value)||value.length!==page.count||value.some(m=>!m||!['user','assistant','toolResult'].includes(m.role)))throw new Error('历史片段格式异常');
  for(const message of value)messages.push(message);
  await yieldFrame();
 }
 p.messages=messages;commits.set(p,{manifest:p.history,messages:messages.slice()});delete p.history;
 return p;
}
