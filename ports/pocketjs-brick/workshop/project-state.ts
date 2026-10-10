import './agent-runtime.ts';
import {sha256} from '@noble/hashes/sha256';
import {bytesToHex} from '@noble/hashes/utils';

// Bump when the parser or delivery policy changes. Old proofs never authorize
// publication under a different validator.
export const VALIDATOR_VERSION='p8-z8lua-814991a-delivery-v5';
export function contentHash(text:string){const native=(globalThis as any).brick?.sha256;return typeof native==='function'?native(text):bytesToHex(sha256(text));}
export interface DesignRecord {
 core_action:string;goal:string;risk:string;controls:string;scenes:string;
 audiovisual:string;preserve:string[];unresolved:string[];
}
export const DESIGN_PROPERTIES={type:'object',properties:{
 core_action:{type:'string',maxLength:500},goal:{type:'string',maxLength:500},risk:{type:'string',maxLength:500},
 controls:{type:'string',maxLength:500},scenes:{type:'string',maxLength:500},audiovisual:{type:'string',maxLength:500},
 preserve:{type:'array',maxItems:24,items:{type:'string',minLength:1,maxLength:300}},
 unresolved:{type:'array',maxItems:24,items:{type:'string',minLength:1,maxLength:300}}
},required:['core_action','goal','risk','controls','scenes','audiovisual','preserve','unresolved'],additionalProperties:false};
export function validDesign(value:unknown):value is DesignRecord {
 const d=value as DesignRecord;
 return !!d&&['core_action','goal','risk','controls','scenes','audiovisual'].every(k=>typeof (d as any)[k]==='string'&&(d as any)[k].length<=500)&&
 ['preserve','unresolved'].every(k=>Array.isArray((d as any)[k])&&(d as any)[k].length<=24&&(d as any)[k].every((s:unknown)=>typeof s==='string'&&s.length>0&&s.length<=300))&&Object.keys(d).length===8;
}
export interface CheckProof {hash:string;validator:string;checkedAt:number}
export interface Diagnostic {kind:string;at:number;model?:string;provider?:string;summary?:boolean;attempt?:number;elapsedMs?:number;firstByteMs?:number;firstTokenMs?:number;frameMaxMs?:number;peakRssKiB?:number;requestChars?:number;validator?:string;skillHash?:string;httpPeakBufferBytes?:number;status?:number;tool?:string;error?:string}
export interface WorkState {
 schema:1;phase:'generating'|'draft_saved'|'checked'|'publishing'|'pending_reply'|'complete'|'interrupted'|'cancelled';
 request:string;startedAt:number;updatedAt:number;revision:number;check?:CheckProof;error?:string;
}
export function validWork(value:unknown):value is WorkState {
 const s=value as WorkState;
 return !!s&&s.schema===1&&['generating','draft_saved','checked','publishing','pending_reply','complete','interrupted','cancelled'].includes(s.phase)&&typeof s.request==='string'&&s.request.length<=16000&&Number.isFinite(s.startedAt)&&Number.isFinite(s.updatedAt)&&Number.isInteger(s.revision)&&s.revision>=0&&
 (!s.check||/^[a-f0-9]{64}$/.test(s.check.hash)&&typeof s.check.validator==='string'&&Number.isFinite(s.check.checkedAt));
}
