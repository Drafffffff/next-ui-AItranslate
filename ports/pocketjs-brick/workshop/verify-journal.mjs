import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
const bundle=await build({entryPoints:[new URL('journal.ts',import.meta.url).pathname],bundle:true,write:false,format:'iife',platform:'neutral',globalName:'Journal'});
const ctx=vm.createContext({});vm.runInContext(bundle.outputFiles[0].text,ctx);const {saveJournal,hydrateHistory,resetHistory,validManifest}=ctx.Journal;
const files=new Map();let writes=[],failManifest=false,yields=0;
const writer=async(name,produce)=>{const chunks=[];await produce(s=>chunks.push(s));if(failManifest&&name.startsWith('projects/'))throw Error('simulated power loss before manifest commit');files.set(name,chunks.join(''));writes.push(name);};
const yieldFrame=async()=>{yields++;};
for(const size of [10,30,60]){
 const p={schema:1,id:'game-journal-'+size,title:'长对话',revision:1,updatedAt:1,lua:'source',summary:'',versions:[],messages:Array.from({length:5000},(_,i)=>({role:'user',content:i+':'+('历史abc'.repeat(Math.round(size*1024*1024/5000/7))),timestamp:i}))};
 writes=[];await saveJournal(p,{...p,messages:p.messages.slice()},writer,yieldFrame);
 const manifest=JSON.parse(files.get('projects/'+p.id+'.json'));assert.equal(manifest.messages.length,0);assert(validManifest(manifest.history,p.id));assert(files.get('projects/'+p.id+'.json').length<100000);
 const loaded=await hydrateHistory(manifest,async n=>files.get(n),yieldFrame);assert.equal(JSON.stringify(loaded.messages),JSON.stringify(p.messages));
 const base=files.get('projects/'+p.id+'.json'),committed=p.messages.length;
 p.messages.push({role:'user',content:'新需求',timestamp:Date.now()});writes=[];await saveJournal(p,{...p,messages:p.messages.slice()},writer,yieldFrame);
 assert.equal(writes.filter(n=>n.startsWith('history/')).length,1,'Appending must not rewrite committed history');
 const previous=files.get('projects/'+p.id+'.json');p.messages.push({role:'user',content:'取消前的需求',timestamp:1});failManifest=true;
 await assert.rejects(saveJournal(p,{...p,messages:p.messages.slice()},writer,yieldFrame),/power loss/);assert.equal(files.get('projects/'+p.id+'.json'),previous);
 failManifest=false;await saveJournal(p,{...p,messages:p.messages.slice()},writer,yieldFrame);
 const verified=JSON.parse(files.get('projects/'+p.id+'.json')),page=verified.history.pages[0],raw=files.get(page.file);files.set(page.file,raw+' ');
 await assert.rejects(hydrateHistory(verified,async n=>files.get(n),yieldFrame),/校验失败/);assert.equal(verified.messages.length,0,'Corrupt pages cannot hydrate a partial history');files.set(page.file,raw);
 // An answered cancelled question can remove a result in the middle of history.
 p.messages.splice(100,1);resetHistory(p);await saveJournal(p,{...p,messages:p.messages.slice()},writer,yieldFrame);
 const rebuilt=await hydrateHistory(JSON.parse(files.get('projects/'+p.id+'.json')),async n=>files.get(n),yieldFrame);assert.equal(JSON.stringify(rebuilt.messages),JSON.stringify(p.messages));
 console.log(`PASS ${size}MiB class: ${committed} messages, ${JSON.parse(base).history.pages.length} immutable pages; append one page, failed commit preserves history, corruption rejected, structural rewrite preserves exact messages.`);
 files.clear();
}
assert(yields>1000);assert(!validManifest({schema:1,count:1,pages:[{file:'../../secret',count:1,hash:'a'.repeat(64)}]},'game-test'));
console.log('PASS bounded-history hydration, cooperative yields, immutable page hashes and validated paths.');
