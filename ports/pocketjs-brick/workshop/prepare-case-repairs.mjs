/** Prepare reviewed repairs with the production publication/recovery protocol.
 * Does not mutate the handheld. Deploy only after source SHA comparison. */
import assert from 'node:assert/strict';import vm from 'node:vm';import {build} from 'esbuild';
import {readFileSync,writeFileSync,existsSync} from 'node:fs';import path from 'node:path';
import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';
const root=path.resolve(new URL('../../..',import.meta.url).pathname),base=path.resolve(process.argv[2]??path.join(root,'build/workshop-case-repairs'));
const files=new Map(),roms=new Map();
const harness={write:async(n,t)=>files.set(n,t),task:input=>{
 if(input.op==='syntax'){const r=spawnSync(path.join(root,'build/pocketjs-port/mac-evaluation/workshop-check'),{input:input.text,encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);}
 else if(input.op==='publish')roms.set(input.name,input.text);else throw Error('Unexpected operation '+input.op);
 return {promise:Promise.resolve({text:'verified'})};
}};
const b=await build({entryPoints:[new URL('model.ts',import.meta.url).pathname,new URL('project-state.ts',import.meta.url).pathname],outdir:'/tmp/workshop-repair-bundle',bundle:true,write:false,platform:'neutral',format:'iife',globalName:'M',plugins:[{name:'services',setup(b){b.onResolve({filter:/services\.ts$/},()=>({path:'s',namespace:'mock'}));b.onLoad({filter:/.*/,namespace:'mock'},()=>({contents:'export const read=async()=>"";export const write=(...a)=>globalThis.h.write(...a);export const writeChunks=async(n,p)=>{const chunks=[];await p(s=>chunks.push(s));await write(n,chunks.join(""));};export const task=(...a)=>globalThis.h.task(...a);export const delay=async()=>{};export const request=()=>{};export const requestStream=undefined;'}));}}]});
const ctx=vm.createContext({h:harness});let M,state;for(const file of b.outputFiles){vm.runInContext(file.text,ctx);if(file.path.endsWith('model.js'))M=ctx.M;else state=ctx.M;}
const fixes={
 'game-muqioi9l-yczh':'修复世界镜头、室内碰撞和重复拾取，补齐背景音乐与操作音效',
 'game-muq0mtg0-ecul':'修复可达检查点、三圈计数与撞墙恢复，补齐背景音乐与操作音效',
 'game-muq16s15-5kr4':'恢复向下软降并加入水平长按重复，保留原有精灵、音乐和音效',
 'game-muq20c9n-huje':'修复3D地图边界、精灵缩放与墙壁遮挡，保留探索、战斗和原有声音',
 'game-muq26o90-sywc':'完成已保存的菜单修改，修复文字居中和面板边界，保留射击、升级、图像与声音'
};
const manifest=[];
for(const [id,summary] of Object.entries(fixes)){
 if(!existsSync(path.join(base,id+'.original.json')))continue;
 const original=readFileSync(path.join(base,id+'.original.json'),'utf8'),p=M.decodeProject(original);
 // This specific legacy draft is the user's unfinished menu request, blocked
 // by the former compaction failure. Other existing drafts are never replaced.
 assert(!p.pending&&(!M.hasDraftChanges(p)||id==='game-muq26o90-sywc'),'Pending drafts must be handled separately');
 assert(!['generating','awaiting_answer','publishing'].includes(p.work?.phase),'Active work cannot be replaced');
 const before=JSON.parse(original),lua=readFileSync(path.join(base,id+'.patched.lua'),'utf8');
 p.pending={revision:p.revision+1,title:p.title,summary,lua,gfx:p.gfx,audio:p.audio,design:p.design};
 p.work={schema:1,phase:'publishing',request:'案例复盘维护：'+summary,startedAt:Date.now(),updatedAt:Date.now(),revision:p.revision};
 await M.saveProject(p);writeFileSync(path.join(base,id+'.pending.json'),files.get('projects/'+id+'.json'));
 await M.recoverProject(p);p.work.phase='complete';p.work.updatedAt=Date.now();
 p.diagnostics=[...(p.diagnostics??[]),{kind:'case_repair',at:Date.now(),validator:state.VALIDATOR_VERSION,sourceHash:createHash('sha256').update(before.lua).digest('hex'),repairHash:createHash('sha256').update(lua).digest('hex')}];
 await M.saveProject(p);assert.equal(p.revision,before.revision+1);assert.equal(p.versions.length,before.versions.length+1);
 assert.equal(JSON.stringify(p.messages),JSON.stringify(before.messages));assert.equal(p.gfx,before.gfx);assert.equal(JSON.stringify(p.audio),JSON.stringify(before.audio));
 for(const [suffix,key] of [['final.json','projects/'+id+'.json'],['info.json','project-info/'+id+'.json']])writeFileSync(path.join(base,id+'.'+suffix),files.get(key));
 writeFileSync(path.join(base,id+'.game.p8'),roms.get(id));
 const published=before.versions.find(v=>v.revision===before.revision)??before;
 manifest.push({id,title:p.title,revision:p.revision,originalHash:createHash('sha256').update(original).digest('hex'),originalCartHash:createHash('sha256').update(M.cartridge(published.lua,published.gfx,published.audio)).digest('hex'),finalHash:createHash('sha256').update(files.get('projects/'+id+'.json')).digest('hex'),cartHash:createHash('sha256').update(roms.get(id)).digest('hex')});
 console.log('Prepared '+p.title+' v'+p.revision+'; full history/assets preserved; native parser + production recovery checked');
}
writeFileSync(path.join(base,'deployment-manifest.json'),JSON.stringify(manifest,null,2));
