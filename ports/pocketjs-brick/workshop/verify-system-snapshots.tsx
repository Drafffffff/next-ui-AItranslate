/** Native allowlist boundary QA. Uses only a synthetic SD fixture. */
import {mount} from '@pocketjs/framework/solid';import {View,Text} from '@pocketjs/framework/solid/components';
import {installServices,task,write} from '../app/services.ts';
function Verify(){installServices();let ready=false,failed=false;
 (globalThis as any).__workshopAcceptance=(op:number)=>op===0?(ready?1:0):op===8?(failed?1:0):op===17?1:0;
 void(async()=>{const checks:string[]=[];try{
  const read=async(name:string)=>(await task({op:'system-read',name}).promise).text;
  for(const name of ['system/meminfo','system/cpuinfo','system/version','system/uptime','system/loadavg','system/app-status','runtime/README.md','runtime/launch.sh','runtime/build-receipt.txt','runtime/P8-launch.sh','skill/SKILL.md']){const text=await read(name);if(!text||text.length>65536)throw Error('Bad snapshot '+name);checks.push(name);}
  for(const name of ['config.json','runtime/config.json','logs/config.json','/proc/meminfo','../config.json','skill/../../config.json','skill/outside.md','skill/linked/config.md']){let denied=false;try{await read(name);}catch{denied=true;}if(!denied)throw Error('Not denied '+name);checks.push('denied '+name);}
  const log=await read('logs/workshop.log');if(!log.includes('normal-diag')||log.includes('__TEST_SECRET__')||log.includes('sk-fixture-private'))throw Error('Log redaction failed');
  const tail=await read('logs/launch-error.txt');if(!tail.endsWith('FINAL_DIAGNOSTIC\n')||tail.length>65536||tail.includes('FIRST_LINE'))throw Error('Tail budget failed');
  await write('prompt.txt','PASS system snapshot allowlist, proc/runtime/Skill reads, denied credentials/traversal/symlink escapes, credential-line redaction and 64KiB log tail. '+checks.join('; '));
 }catch(e){failed=true;await write('prompt.txt','FAIL '+String(e));}finally{ready=true;}})();
 return <View class="w-[1024] h-[768]"><Text>System snapshot QA</Text></View>;}
mount(Verify);
