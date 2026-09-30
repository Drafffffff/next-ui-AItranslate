import {decodeProject,fill,paint,svg} from './model.ts';
const check=(ok:boolean)=>{if(!ok)throw new Error('model validation failed');};
check(fill('0'.repeat(4096),64,0,0,3)==='3'.repeat(4096));
check(paint('0'.repeat(256),16,16,0,3)==='0'.repeat(256));
const frame=Array.from({length:4096},(_,i)=>(i%16).toString(16)).join('');
const p=decodeProject(JSON.stringify({version:1,size:64,fps:24,frames:[frame]}));
check(new TextEncoder().encode(svg(p,0)).length<128*1024);
let rejected=false;try{decodeProject('{"version":1,"size":64,"fps":8,"frames":["f"]}');}catch{rejected=true;}
check(rejected);console.log('PASS: bounded fill, out-of-bounds paint, 64px SVG size, corrupt project rejected');
