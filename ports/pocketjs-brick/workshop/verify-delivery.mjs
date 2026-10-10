import assert from 'node:assert/strict';
import vm from 'node:vm';
import {build} from 'esbuild';
import {existsSync,readFileSync,readdirSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
const b=await build({entryPoints:[new URL('delivery.ts',import.meta.url).pathname],bundle:true,write:false,platform:'neutral',format:'iife',globalName:'Delivery'});
const ctx=vm.createContext({});vm.runInContext(b.outputFiles[0].text,ctx);const {deliveryDiagnostics}=ctx.Delivery;
const audio={sfx:[{id:0,notes:[[36,0,3,0]]}],music:[{id:6,channels:[0,-1,-1,-1]}]};
let d=deliveryDiagnostics('music(6) sfx(0,3)',audio);assert.equal(d.errors.length,0);assert(d.bgm_start&&d.operation_sound);
d=deliveryDiagnostics('sfx(16) sfx(16) music(8)',audio);assert.equal(d.errors.length,2);
d=deliveryDiagnostics('music(6)',{sfx:[{id:0,notes:[[36,0,0,0]]}],music:audio.music});assert.equal(d.errors.length,2,'Music with all silent voices is an empty reference');
d=deliveryDiagnostics('',audio);assert.equal(d.errors.length,2,'Both missing soundtrack and feedback block delivery');
d=deliveryDiagnostics('-- sfx(16)\n--[=[ music(8) ]=]\nprint("sfx(20)") music(-1) sfx(-2)',audio);assert.equal(d.errors.length,2);assert(!d.bgm_start&&!d.operation_sound);
d=deliveryDiagnostics('music(track) sfx(event) camera()',audio);assert.equal(d.errors.length,0);assert.equal(d.warnings.length,3);
d=deliveryDiagnostics('function sfx(i) return i end sfx(16)',audio);assert(d.errors.some(e=>e.includes('重定义')),'Shadowed playback APIs must be rejected');
for(const call of ['return table.remove(bag)','math.floor(x)','string.sub(text,1,2)']){
 d=deliveryDiagnostics('music(6) sfx(0) '+call,audio);assert.equal(d.errors.length,1,'Missing standard namespace must block delivery: '+call);assert(d.errors[0].includes('标准库'));
}
for(const source of ['-- table.remove(bag)\nprint("math.floor(x)")','--[=[ string.sub(text,1) ]=]','obj.table.remove(bag)','return deli(bag,#bag)']){d=deliveryDiagnostics('music(6) sfx(0) '+source,audio);assert.equal(d.errors.length,0,'Comments, strings, nested objects and P8 helpers are not standard-library dependencies');}
for(const source of ['local table={remove=function(t) return t[1] end} table.remove(bag)','function use(table) return table.remove(bag) end']){d=deliveryDiagnostics('music(6) sfx(0) '+source,audio);assert.equal(d.errors.length,0,'Custom bindings require scope review, not a false rejection');assert.equal(d.warnings.length,1);}
d=deliveryDiagnostics('cell=12\nfunction cell(x,y) return 1 end\nmusic(6) sfx(0)',audio);assert(d.warnings.some(w=>w.includes('全局 cell')));assert.equal(d.errors.length,0,'A static name hint must not claim full scope proof');
for(const source of ['tile_size=12\nfunction tile_at(x,y) return 1 end','local cell=12\nfunction cell(x,y) return 1 end','-- cell=12\nfunction cell(x,y) return 1 end']){d=deliveryDiagnostics(source+'\nmusic(6) sfx(0)',audio);assert.equal(d.warnings.length,0,'Distinct names, lexical locals and comments do not create a global numeric binding');}
const longSound={id:17,speed:10,loop_start:0,loop_end:0,notes:Array.from({length:32},(_,i)=>[24,0,i%4===0?2:0,0])};
d=deliveryDiagnostics('music(8,200,3) if btn(0) then sfx(17,3) end',{sfx:[longSound],music:[{id:8,channels:[17,-1,-1,-1]}]});
assert.equal(d.errors.length,0);assert.equal(d.warnings.length,1);assert(d.warnings[0].includes('SFX 17'),'Shared long track/feedback must be surfaced without blocking legal reuse');
d=deliveryDiagnostics('music(8) sfx(17,3)',{sfx:[{...longSound,speed:4,loop_start:2}],music:[{id:8,channels:[17,-1,-1,-1]}]});assert.equal(d.warnings.length,0,'Explicit short effective length is not a long feedback sound');
d=deliveryDiagnostics('music(8) -- sfx(17)\nsfx(0,3)',{sfx:[longSound,...audio.sfx],music:[{id:8,channels:[17,-1,-1,-1]}]});assert.equal(d.warnings.length,0,'Unplayed or commented-out shared slots must not warn');
const binary=process.env.WORKSHOP_SYNTAX_CHECK||new URL('../../../build/pocketjs-port/workshop-mac/workshop-check',import.meta.url).pathname;
if(existsSync(binary)){
 for(const [lua,valid] of [['function _init() end\nfunction _update() x+=1 end\nfunction _draw() ?"p8" end',true],['function _init( end',false],['while true do end',true]]){
  const result=spawnSync(binary,{input:lua,encoding:'utf8',timeout:5000});assert.equal(result.status===0,valid,result.stdout+result.stderr);
 }
 for(const [bytes,valid] of [[8*1024*1024,true],[8*1024*1024+1,false]]){const r=spawnSync(binary,{input:'--'+' '.repeat(bytes-2),timeout:5000});assert.equal(r.status===0,valid,'Parser input boundary');}
 const archive=new URL('../../../build/workshop-case-review-2026-10-02/',import.meta.url).pathname;
 if(existsSync(archive)){let count=0;for(const file of readdirSync(archive).filter(f=>f.startsWith('game-')&&f.endsWith('.lua'))){const r=spawnSync(binary,{input:readFileSync(archive+file),timeout:5000});assert.equal(r.status,0,file);count++;}console.log('PASS actual parser: '+count+' archived Lua revisions, P8 shorthand, invalid syntax rejected, infinite-loop source parsed without execution.');}
}
console.log('PASS: literal empty audio slots rejected, available resources, comment/string/control filtering, dynamic/shadowed API warnings; no gameplay-test claims.');
