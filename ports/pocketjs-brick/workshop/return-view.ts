import {displayText} from '../app/display-text.ts';
import type {Project} from './model.ts';
import type {TranscriptRow} from './transcript.ts';

/** A display-only, already wrapped page. Never used as Agent history or source. */
export interface ReturnView {
 schema:1;layout:1;projectId:string;revision:number;updatedAt:number;rows:TranscriptRow[];
}
const LIMIT=65536;
const kinds=new Set(['user','assistant','tool','heading','space']);
export function returnView(value:unknown,p:Pick<Project,'id'|'revision'|'updatedAt'>):ReturnView|undefined {
 const v=value as ReturnView;
 if(!v||v.schema!==1||v.layout!==1||v.projectId!==p.id||v.revision!==p.revision||v.updatedAt!==p.updatedAt||!Array.isArray(v.rows)||!v.rows.length||v.rows.length>24)return;
 let height=0;
 for(const row of v.rows){
  if(!row||typeof row.text!=='string'||row.text.length>4096||!kinds.has(row.kind)||![12,44].includes(row.height))return;
  if(displayText(row.text)!==row.text)return;
  height+=row.height;
  if(row.indent!==undefined&&(!Number.isFinite(row.indent)||row.indent<0||row.indent>888))return;
  if(row.format!==undefined){
   if(!row.format||typeof row.format!=='object')return;
   if(!['paragraph','heading','code','quote','list','table','rule'].includes(row.format.block)||!Number.isInteger(row.format.depth)||row.format.depth<0||row.format.depth>8||!Number.isInteger(row.format.flags)||row.format.flags<0||row.format.flags>31)return;
  }
  if(row.runs!==undefined&&(!Array.isArray(row.runs)||row.runs.length>256||row.runs.some(r=>!r||typeof r.text!=='string'||r.text.length>4096||!Number.isFinite(r.x)||r.x<0||r.x>912||!Number.isFinite(r.width)||r.width<0||r.width>912||!Number.isInteger(r.flags)||r.flags<0||r.flags>31)))return;
 }
 if(height>468||JSON.stringify(v).length>LIMIT)return;
 return v;
}
export function makeReturnView(p:Project,rows:TranscriptRow[]):ReturnView|undefined {
 if(p.pending||p.question)return;
 return returnView({schema:1,layout:1,projectId:p.id,revision:p.revision,updatedAt:p.updatedAt,rows},p);
}
