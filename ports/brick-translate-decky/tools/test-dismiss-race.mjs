import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const src=fs.readFileSync(new URL('../dist/index.js',import.meta.url),'utf8');
const start=src.slice(src.indexOf('async function start() {'),src.indexOf('// Steam requires'));
const c={inFlight:false,generation:0,alive:true,view:{stage:'idle'},hideTimer:undefined,
 getSettings:async()=>({has_key:true,has_ocr_key:true,pipeline:'ocr',hold_seconds:15}),
 DFL:{Navigation:{CloseSideMenus(){}},Router:{}},capture:async()=>({image:'x',token:'t'}),
 modelImage:async()=>'',translate:async()=>({items:[]}),clearTimeout(){},notify(){},errorText:String,
 setTimeout(fn,ms){ if(ms===500) fn();else throw Error('dismissed translation must not arm a timer'); }};
c.update=v=>{c.view=v};c.armDismiss=async enabled=>{if(enabled&&c.view.stage==='done'){c.generation++;c.update({...c.view,stage:'idle'})}};
vm.createContext(c);vm.runInContext(start,c);await c.start();
assert.equal(c.view.stage,'idle');assert.equal(c.inFlight,false);
console.log('PASS: key arriving during arm RPC dismisses immediately and is not overwritten.');
