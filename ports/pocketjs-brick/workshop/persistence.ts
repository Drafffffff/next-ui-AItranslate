import type {Project} from './model.ts';
// Completed Pi messages are immutable. Weak keys let an unloaded project's
// serialized log be collected; later saves encode only newly appended messages.
const serialized=new WeakMap<object,string>();
const weights=new WeakMap<object,number>();
/** Large messages also switch storage format, even in a short conversation. */
export async function largeHistory(messages:Project['messages'],yieldFrame:()=>Promise<void>){
 if(messages.length>=2048)return true;
 let bytes=0,started=Date.now();
 for(const m of messages){let size=weights.get(m);if(size===undefined){size=JSON.stringify(m).length*3;weights.set(m,size);}bytes+=size;
  if(bytes>=2*1024*1024)return true;
  if(Date.now()-started>=6){await yieldFrame();started=Date.now();}
 }return false;
}
/** Freeze at the save boundary, before another turn can append or mutate a question. */
export function projectSnapshot(p:Project):Project {
 return {...p,messages:p.messages.slice(),versions:p.versions.slice(),
  ...(p.question?{question:{...p.question}}:{}),
  ...(p.work?{work:{...p.work,...(p.work.check?{check:{...p.work.check}}:{})}}:{}),
  ...(p.design?{design:{...p.design,preserve:p.design.preserve.slice(),unresolved:p.design.unresolved.slice()}}:{}),
  ...(p.compactions?{compactions:p.compactions.slice()}:{}),
  ...(p.diagnostics?{diagnostics:p.diagnostics.slice()}:{})};
}
/** Serialize immutable messages in bounded chunks and small time slices.
 * Queued saves hold references to frozen message arrays. */
export async function streamProject(p:Project,append:(chunk:string)=>void,yieldFrame:()=>Promise<void>):Promise<void>{
 let parts:string[]=[],size=0;let first=true,started=Date.now();
 const flush=()=>{if(size){append(parts.join(''));parts=[];size=0;}};
 const push=(text:string)=>{for(let i=0;i<text.length;){let end=Math.min(text.length,i+65536);if(end<text.length&&text.charCodeAt(end-1)>=0xd800&&text.charCodeAt(end-1)<=0xdbff)end--;const piece=text.slice(i,end);if(size+piece.length>65536)flush();parts.push(piece);size+=piece.length;i=end;}};
 push('{');
 for(const key of Object.keys(p) as (keyof Project)[]){
  const value=p[key];if(value===undefined)continue;
  if(!first)push(',');first=false;push(JSON.stringify(key));push(':');
  if(key!=='messages'){push(JSON.stringify(value));continue;}
  push('[');
  for(let i=0;i<p.messages.length;i++){
   const message=p.messages[i];let json=serialized.get(message);if(json===undefined){json=JSON.stringify(message);serialized.set(message,json);}
   if(i)push(',');push(json);
   if(i%32===31&&Date.now()-started>=6){await yieldFrame();started=Date.now();}
  }
  push(']');
 }
 push('}');flush();
}
export async function serializeProject(p:Project,yieldFrame:()=>Promise<void>):Promise<string>{
 const parts:string[]=[];await streamProject(p,part=>parts.push(part),yieldFrame);return parts.join('');
}
