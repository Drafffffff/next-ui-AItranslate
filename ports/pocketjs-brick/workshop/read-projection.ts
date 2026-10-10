import type {GameMessage} from './agent.ts';
const projected=new WeakMap<object,GameMessage>();
const projectedWrites=new WeakMap<object,{latestWrite:string;message:GameMessage}>();
const argumentObject=(value:unknown):value is Record<string,unknown>=>value!==null&&typeof value==='object'&&!Array.isArray(value);
const parsed=new WeakMap<object,Record<string,unknown>|null>();
function value(m:GameMessage){
 if(parsed.has(m))return parsed.get(m);
 let data=null;try{const text=typeof m.content==='string'?m.content:m.content.filter(c=>c.type==='text').map(c=>c.text).join('\n');const raw=JSON.parse(text);data={project_id:raw.project_id,revision:raw.revision,module:raw.module,path:raw.path};}catch{}
 parsed.set(m,data);return data;
}
/** Preserve tool IDs and pairing. Superseded source payloads are omitted only
 * in provider projections; the original local history remains untouched. */
export function projectReads(messages:GameMessage[]){
 const newest=new Map<string,GameMessage>();
 let evidence=-1,latestWrite='';
 for(let i=messages.length-1;i>=0;i--){const m=messages[i];if(m.role!=='toolResult'||m.isError)continue;
  if(evidence<0&&(m.toolName==='read_game'||m.toolName==='write_game')){evidence=i;latestWrite=m.toolName==='write_game'?m.toolCallId:'';}
  const key=m.toolName==='read_game'?'game':m.toolName==='read_skill'?'skill:'+String(value(m)?.module):'';
  if(key&&!newest.has(key))newest.set(key,m);
 }
 return messages.map((m,index)=>{
  if(m.role==='assistant'&&index<evidence){
   const stale=m.content.some(c=>c.type==='toolCall'&&c.name==='write_game'&&c.id!==latestWrite&&argumentObject(c.arguments)&&['lua','edits','sprite_overrides','sfx_overrides','music_overrides'].some(k=>k in c.arguments));
   if(!stale)return m;
   const previous=projectedWrites.get(m);if(previous?.latestWrite===latestWrite)return previous.message;
   const content=m.content.map(c=>{
    if(c.type!=='toolCall'||c.name!=='write_game'||c.id===latestWrite||!argumentObject(c.arguments))return c;
    const args={...c.arguments};let removed=false;
    for(const key of ['lua','edits','sprite_overrides','sfx_overrides','music_overrides'])if(key in args){delete args[key];removed=true;}
    if(!removed)return c;
    args.summary=(typeof args.summary==='string'?args.summary.slice(0,120)+'；':'')+'较早的源码和素材写入已被后续成功操作覆盖；完整原始参数保留在本地历史，当前源码通过 read_game 获取。';
    return {...c,arguments:args};
   });
   const replacement={...m,content};projectedWrites.set(m,{latestWrite,message:replacement});return replacement;
  }
  if(m.role!=='toolResult'||m.isError)return m;
  const key=m.toolName==='read_game'?'game':m.toolName==='read_skill'?'skill:'+String(value(m)?.module):'';
  if(!key||newest.get(key)===m)return m;
  const data=value(m);if(!data)return m;
  const previous=projected.get(m);if(previous)return previous;
  const text=JSON.stringify({superseded_read:true,project_id:data.project_id,revision:data.revision,module:data.module,path:data.path,note:'过时的读取正文已从请求投影去重。最新成功读取保留完整数据；当前真实源码通过 read_game 获取。'});
  const replacement={...m,content:[{type:'text' as const,text}]};projected.set(m,replacement);return replacement;
 });
}
