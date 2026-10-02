import type {MicState} from './model.ts';
export type Page='main'|'hub'|'edit'|'read'|'approval'|'connections';
export interface Entry {id:string;label:string;detail?:string;targetID?:string;unread?:number}
export const edits:Entry[]=[
 {id:'all',label:'全选'},{id:'undo',label:'撤销'},{id:'redo',label:'重做'},
 {id:'copy',label:'复制'},{id:'paste',label:'粘贴'},{id:'newline',label:'换行'},
 {id:'read',label:'查看最近口述'},{id:'delete',label:'删除文字'},
];
export function hubEntries(s:MicState):Entry[]{
 const c=s.control;if(!c?.remoteAllowed||c.mode!=='codex')return [];
 const entries:Entry[]=[];
 if(c.approval)entries.push({id:'approval',label:'待确认的操作',detail:c.approval.title});
 if(c.codexAvailable)entries.push(...c.tasks.map(t=>({id:t.id,targetID:t.id,label:t.title,detail:t.status==='running'?'运行中':t.status==='waiting'?'待确认':t.status==='completed'?'已回复':'',unread:t.unread||0})));
 return entries;
}
export function backPage(page:Page):Page {
 if(page==='connections')return 'main';
 return page==='approval'?'hub':page==='read'?'edit':'main';
}
export function windowStart(index:number,count:number,visible=5){return Math.max(0,Math.min(Math.max(0,count-visible),index-2));}
export function readRow(row:number,delta:number,count:number,visible=7){return Math.max(0,Math.min(Math.max(0,count-visible),row+delta));}
export function ellipsis(text:string,count:number){return text.length>count?text.slice(0,count-1)+'…':text;}

// Solid For tracks object identity. Reuse unchanged entries across cloned IPC snapshots.
export function reuseEntries(next:Entry[],previous:Entry[]):Entry[]{
 const existing=new Map(previous.map(entry=>[entry.id,entry]));
 const values=next.map(entry=>{const old=existing.get(entry.id);return old&&old.label===entry.label&&old.detail===entry.detail&&old.targetID===entry.targetID&&old.unread===entry.unread?old:entry;});
 return values.length===previous.length&&values.every((entry,i)=>entry===previous[i])?previous:values;
}
export function reuseActionRows(next:[string,string][][],previous:[string,string][][]):[string,string][][]{
 const rows=next.map((row,i)=>{const old=previous[i];return old&&old.length===row.length&&row.every((action,j)=>action[0]===old[j][0]&&action[1]===old[j][1])?old:row;});
 return rows.length===previous.length&&rows.every((row,i)=>row===previous[i])?previous:rows;
}

// Pagination belongs to the same reply revision; a new turn may have identical text.
export function replyRevision(reply:NonNullable<NonNullable<MicState['control']>['reply']>){return reply.revision||reply.text;}
export function replyReaderKey(reply:NonNullable<NonNullable<MicState['control']>['reply']>){return 'reply:'+reply.task+':'+replyRevision(reply)+':'+reply.page+':'+reply.text;}

export function hostEntries(s:MicState):Entry[]{
 const h=s.hosts;
 return [...(h?.known||[]).map(v=>({id:'host:'+v.id,label:v.name,detail:v.id===h?.active?'已连接':v.id===h?.selected?'等待连接':'',targetID:v.id})),
  {id:h?.discovering?'stop-discovery':'discover',label:h?.discovering?'结束查找':'查找电脑',detail:h?.discovering?'正在查找 · 30 秒':'在电脑上运行 Brick Mic'}];
}

export function initialHostIndex(s:MicState):number {
 const id=s.hosts?.selected||s.hosts?.active;
 return Math.max(0,(s.hosts?.known||[]).findIndex(h=>h.id===id));
}
export function receiverName(s:MicState):string {
 const id=s.connected?s.hosts?.active||s.hosts?.selected:s.hosts?.selected;
 return s.hosts?.known.find(h=>h.id===id)?.name||'电脑';
}
