import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
const dir=path.dirname(fileURLToPath(import.meta.url)),files=new Map(),reads=[];
const bundle=await build({entryPoints:[path.join(dir,'model.ts')],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Model',plugins:[{name:'storage',setup(b){
 b.onResolve({filter:/services\.ts$/},()=>({path:'native',namespace:'native'}));
 b.onLoad({filter:/.*/,namespace:'native'},()=>({contents:`export const read=async n=>{globalThis.reads.push(n);return globalThis.files.get(n)||'';};export const write=async(n,t)=>{if(n.startsWith('projects/'))globalThis.files.delete('project-info/'+n.slice(9));globalThis.files.set(n,t);};export const task=()=>({promise:Promise.resolve({text:''})});export const request=()=>{};export const writeChunks=async(n,produce)=>{const parts=[];await produce(p=>parts.push(p));return write(n,parts.join(""));};export const delay=async()=>{};export const requestStream=undefined;`}));
}}]});
const ctx=vm.createContext({files,reads});vm.runInContext(bundle.outputFiles[0].text,ctx);
const {newProject,saveProject,saveReturnView,loadReturnView}=ctx.Model;
const p=newProject(),row={text:'最后结果：游戏已发布',kind:'assistant',height:44,format:{block:'paragraph',depth:0,flags:0},runs:[{text:'最后结果：游戏已发布',x:0,width:400,flags:0}]};
await saveProject(p);await saveReturnView(p,[row]);reads.length=0;
const cached=await loadReturnView(p.id);assert.equal(cached.project.summaryOnly,true);assert.equal(cached.project.messages.length,0);assert.equal(cached.view.rows[0].text,row.text);
assert.deepEqual(reads,['project-info/'+p.id+'.json'],'Fast return must not read full history');
const full=files.get('projects/'+p.id+'.json');assert.equal(JSON.parse(full).messages.length,0,'Display cache must not enter the Agent log');
for(const patch of [{schema:2},{layout:2},{projectId:'game-other'},{revision:8},{updatedAt:0},{rows:[{...row,height:10000}]},{rows:[{...row,runs:[{text:'bad',x:Infinity,width:44,flags:0}]}]},{rows:Array(24).fill(row)}]){
 const info=JSON.parse(files.get('project-info/'+p.id+'.json'));info.returnView={...cached.view,...patch};files.set('project-info/'+p.id+'.json',JSON.stringify(info));assert.equal(await loadReturnView(p.id),undefined);
}
await saveReturnView(p,[row]);await saveProject(p);assert.equal(await loadReturnView(p.id),undefined,'Every project write must invalidate cached page');
await saveReturnView(p,[row]);files.set('project-info/'+p.id+'.json','corrupt');assert.equal(await loadReturnView(p.id),undefined);
await saveReturnView(p,[row]);files.delete('project-info/'+p.id+'.json');assert.equal(await loadReturnView(p.id),undefined,'Deleted metadata cannot restore a project');
p.pending={};await saveReturnView(p,[row]);assert(!files.has('project-info/'+p.id+'.json'),'Pending publication cannot cache a completed page');
console.log('PASS: small display-only return cache; no full-history reads, project-write invalidation, revision/layout/schema/row validation, corrupt/missing cache fallback, pending publication excluded.');
