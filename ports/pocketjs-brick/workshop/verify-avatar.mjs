import assert from 'node:assert/strict';
import {build} from 'esbuild';
import vm from 'node:vm';
import {readFileSync,writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
const dir=fileURLToPath(new URL('.',import.meta.url));
const result=await build({entryPoints:[dir+'avatar.ts'],bundle:true,write:false,platform:'neutral',target:'es2020',format:'iife',globalName:'Avatar'});
const context=vm.createContext({});vm.runInContext(result.outputFiles[0].text,context);
const {createAvatar,avatarState,AVATAR_STATES}=context.Avatar;
const hash=x=>createHash('sha256').update(JSON.stringify(x)).digest('hex');
assert.equal(avatarState('recording',true,2,'error'),'listening');
assert.equal(avatarState('processing',false,0,'idle'),'thinking');
assert.equal(avatarState('idle',true,0,'idle'),'thinking');
assert.equal(avatarState('idle',true,1,'idle'),'working');
assert.equal(avatarState('idle',false,0,'done'),'done');
assert.equal(avatarState('idle',false,0,'error'),'error');
const avatar=createAvatar();const still=hash(avatar.staticFrame());
assert(avatar.tick('idle',0));assert.equal(avatar.tick('idle',10),undefined);
assert.equal(AVATAR_STATES.thinking,'orbit');assert.equal(AVATAR_STATES.working,'orbit');
const poses=new Set();let t=100;
for(const state of ['idle','listening','thinking','working','done','error']){
 let first,last;
 for(let i=0;i<72;i++){
  const frame=avatar.tick(state,t);t+=1000/24+.01;
  assert(frame);assert(frame.bodyPath.startsWith('M'));assert(!/NaN|Infinity/.test(JSON.stringify(frame)));
  if(i===24)first=hash(frame);if(i===48)last=hash(frame);
 }
 const isolated=createAvatar();isolated.tick('idle',0);isolated.tick(state,100);
 poses.add(hash(isolated.tick(state,1100)));assert.notEqual(first,last,state+' did not animate');
}
assert.equal(poses.size,5);assert.equal(hash(avatar.staticFrame()),still,'Animated state mutated initial portrait');
// A state change during an existing morph starts from its actual current pose.
const morph=createAvatar();morph.tick('idle',0);const before=morph.tick('thinking',100);
const after=morph.tick('working',100);assert.equal(after.bodyPath,before.bodyPath);
// Long model calls keep replaying orbit instead of resting without its rings.
const busy=createAvatar();busy.tick('idle',0);busy.tick('thinking',100);let activeFrames=0;
for(let i=1;i<=720;i++){
 const frame=busy.tick(i<360?'thinking':'working',100+i*42);assert(frame);
 assert(!frame.dots.length,'Thinking unexpectedly returned to the three dots');
 if(i>600&&frame.arcs.some(arc=>arc.opacity>.1))activeFrames++;
}
assert(activeFrames>30,'Orbit stopped during a long model call');
const reduced=createAvatar(true);assert(reduced.tick('idle',0));assert.equal(reduced.tick('idle',5000),undefined);
assert(reduced.tick('working',5100));assert.equal(reduced.tick('working',10000),undefined);
const app=readFileSync(dir+'app.tsx','utf8');assert(!app.includes('<Key name="START"'));assert.equal((app.match(/<Key name="SR"/g)||[]).length,2);
// Guard the pinned engine files from accidental in-house changes.
for(const name of ['engine','shape','math','decor','expressions','face','profiles','states']){
 const expected=readFileSync(dir+'third-party/bloub/hashes.json','utf8');
 assert.equal(createHash('sha256').update(readFileSync(dir+'third-party/bloub/'+name+'.ts')).digest('hex'),JSON.parse(expected)[name]);
}
console.log(`PASS: original pinned bloub geometry; thinking/working share continuous orbit; 30s busy animation; 24Hz throttling; mid-morph continuity; reduced-motion alternative; SR display only. Bundle ${result.outputFiles[0].text.length} bytes.`);
