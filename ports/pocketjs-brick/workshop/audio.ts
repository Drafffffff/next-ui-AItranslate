import {PICO8_AUDIO_DATA,PICO8_AUDIO_CATALOG} from './skill.generated.ts';
export interface Sound {id:number;speed:number;loop_start:number;loop_end:number;notes:number[][]}
export interface Pattern {id:number;channels:number[];loop_start:boolean;loop_end:boolean;stop:boolean}
export interface GameAudio {sfx:Sound[];music:Pattern[]}
export const BUILTIN_AUDIO:GameAudio=PICO8_AUDIO_DATA;
export const AUDIO_CATALOG=PICO8_AUDIO_CATALOG;
const integer=(n:unknown,lo:number,hi:number)=>Number.isInteger(n)&&Number(n)>=lo&&Number(n)<=hi;
const keys=(o:unknown,expected:string)=>!!o&&typeof o==='object'&&Object.keys(o).sort().join(',')===expected;
function sound(value:any,min=0){
 if(!keys(value,'id,loop_end,loop_start,notes,speed')||!integer(value.id,min,63)||!integer(value.speed,1,255)||!integer(value.loop_start,0,32)||!integer(value.loop_end,0,32)||value.loop_end>0&&value.loop_end<=value.loop_start)throw new Error('音效 ID/速度/循环范围无效');
 if(!Array.isArray(value.notes)||value.notes.length!==32||!value.notes.every((n:any)=>Array.isArray(n)&&n.length===4&&integer(n[0],0,63)&&n.slice(1).every((v:any)=>integer(v,0,7))))throw new Error('音效需要 32 个 [音高0..63,波形0..7,音量0..7,效果0..7]');
}
function pattern(value:any,min=0){
 if(!keys(value,'channels,id,loop_end,loop_start,stop')||!integer(value.id,min,63)||!Array.isArray(value.channels)||value.channels.length!==4||!value.channels.every((n:any)=>integer(n,-1,63))||![value.loop_start,value.loop_end,value.stop].every(n=>typeof n==='boolean')||value.stop&&value.loop_end||value.channels.every((n:number)=>n===-1)&&!value.stop)throw new Error('音乐 pattern 的声道或循环/停止标记无效');
}
export function validateAudio(value:unknown):asserts value is GameAudio {
 if(!keys(value,'music,sfx'))throw new Error('音频资源格式异常');
 const audio=value as GameAudio;
 for(const [entries,check] of [[audio.sfx,sound],[audio.music,pattern]] as const){
  if(!Array.isArray(entries)||entries.length>64)throw new Error('音频资源最多 64 个槽位');
  const ids=new Set<number>();for(const entry of entries){check(entry);if(ids.has(entry.id))throw new Error('音频 ID 必须唯一');ids.add(entry.id);}
 }
 const ids=new Set(audio.sfx.map(s=>s.id));
 for(const p of audio.music){
  if(p.channels.some(id=>id>=0&&!ids.has(id)))throw new Error('音乐引用了不存在的音效');
  if(p.loop_end&&!audio.music.some(q=>q.id<=p.id&&q.loop_start))throw new Error('音乐循环缺少起点');
  if(!p.stop&&!p.loop_end&&!audio.music.some(q=>q.id===p.id+1))throw new Error('连续音乐 pattern 缺少下一段');
 }
}
export function mergeAudio(base:GameAudio,sfx:unknown=[],music:unknown=[]):GameAudio {
 validateAudio(base);
 for(const [entries,check,max,min] of [[sfx,sound,48,16],[music,pattern,56,8]] as const){
  if(!Array.isArray(entries)||entries.length>max)throw new Error('音频覆盖数量超出范围');
  const ids=new Set<number>();for(const e of entries){check(e,min);if(ids.has(e.id))throw new Error('音频覆盖 ID 必须唯一');ids.add(e.id);}
 }
 const patched=(original:any[],patches:any[])=>original.filter(e=>!patches.some(p=>p.id===e.id)).concat(patches).sort((a,b)=>a.id-b.id);
 const result={sfx:patched(base.sfx,sfx as any[]),music:patched(base.music,music as any[])};
 validateAudio(result);return JSON.parse(JSON.stringify(result));
}
const hex=(n:number)=>n.toString(16).padStart(2,'0');
export function audioSections(audio:GameAudio){
 validateAudio(audio);
 const sfx=Array.from({length:64},(_,id)=>{
  const s=audio.sfx.find(s=>s.id===id);
  return s?'00'+hex(s.speed)+hex(s.loop_start)+hex(s.loop_end)+s.notes.map(n=>hex(n[0])+n.slice(1).map(v=>v.toString(16)).join('')).join(''):'00100000'+'00000'.repeat(32);
 });
 const music=Array.from({length:64},(_,id)=>{
  const p=audio.music.find(p=>p.id===id);
  return p?hex((p.loop_start?1:0)|(p.loop_end?2:0)|(p.stop?4:0))+' '+p.channels.map((n,i)=>hex(n<0?65+i:n)).join(''):'04 41424344';
 });
 return '\n__sfx__\n'+sfx.join('\n')+'\n__music__\n'+music.join('\n')+'\n';
}
export const AUDIO_PATCH_PROPERTIES={
 sfx_overrides:{type:'array',maxItems:48,items:{type:'object',properties:{id:{type:'integer',minimum:16,maximum:63},speed:{type:'integer',minimum:1,maximum:255},loop_start:{type:'integer',minimum:0,maximum:32},loop_end:{type:'integer',minimum:0,maximum:32},notes:{type:'array',minItems:32,maxItems:32,items:{type:'array',minItems:4,maxItems:4,items:{type:'integer',minimum:0,maximum:63},description:'[pitch 0..63, waveform 0..7, volume 0..7, effect 0..7]; volume 0 is rest'}}},required:['id','speed','loop_start','loop_end','notes'],additionalProperties:false}},
 music_overrides:{type:'array',maxItems:56,items:{type:'object',properties:{id:{type:'integer',minimum:8,maximum:63},channels:{type:'array',minItems:4,maxItems:4,items:{type:'integer',minimum:-1,maximum:63},description:'SFX IDs for channels 0..3; -1 is silent'},loop_start:{type:'boolean'},loop_end:{type:'boolean'},stop:{type:'boolean'}},required:['id','channels','loop_start','loop_end','stop'],additionalProperties:false}}
};
