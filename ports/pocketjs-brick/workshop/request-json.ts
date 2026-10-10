/** JSON-equivalent encoding with bounded string work between native frames.
 * Request bodies contain only JSON values. The final encoded string is reused
 * verbatim for retries; source/history are never modified. */
export async function requestJson(value:unknown,yieldFrame?:()=>Promise<void>,signal?:AbortSignal){
 if(!yieldFrame)return JSON.stringify(value);
 const parts:string[]=[],ancestors=new Set<object>();let started=Date.now(),checks=0;
 const pause=async()=>{if(signal?.aborted)throw new Error('Operation aborted');if(++checks%16===0||Date.now()-started>=6){if(Date.now()-started>=6){await yieldFrame();started=Date.now();}if(signal?.aborted)throw new Error('Operation aborted');}};
 async function encode(v:any){
  if(typeof v==='string'&&v.length>32768){
   parts.push('"');for(let i=0;i<v.length;){let end=Math.min(i+32768,v.length);if(end<v.length&&v.charCodeAt(end-1)>=0xd800&&v.charCodeAt(end-1)<=0xdbff)end--;parts.push(JSON.stringify(v.slice(i,end)).slice(1,-1));i=end;await pause();}parts.push('"');
  }else if(v&&typeof v==='object'&&typeof v.toJSON!=='function'){
   if(ancestors.has(v))throw new TypeError('Converting circular structure to JSON');ancestors.add(v);
   if(Array.isArray(v)){
    parts.push('[');for(let i=0;i<v.length;i++){if(i)parts.push(',');const item=v[i];if(item===undefined||typeof item==='function'||typeof item==='symbol')parts.push('null');else await encode(item);await pause();}parts.push(']');
   }else{
    parts.push('{');let first=true;for(const key of Object.keys(v)){const item=v[key];if(item===undefined||typeof item==='function'||typeof item==='symbol')continue;if(!first)parts.push(',');first=false;parts.push(JSON.stringify(key),':');await encode(item);await pause();}parts.push('}');
   }
   ancestors.delete(v);
  }else parts.push(JSON.stringify(v));
 }
 await encode(value);if(signal?.aborted)throw new Error('Operation aborted');return parts.join('');
}
