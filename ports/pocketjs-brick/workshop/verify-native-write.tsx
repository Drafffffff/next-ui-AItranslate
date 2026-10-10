// Dedicated offline test entry. Not imported by the product bundle.
import {mount} from '@pocketjs/framework/solid';
import {View,Text} from '@pocketjs/framework/solid/components';
import {installServices,native,read,write,writeChunks,delay} from '../app/services.ts';
import {streamProject} from './persistence.ts';
mount(()=>{
 installServices();let ready=0,failed=0;
 (globalThis as any).__workshopAcceptance=(op:number)=>op===0?ready:op===8?failed:1;
 void(async()=>{const name='projects/game-write-native.json';
  const require=(value:unknown)=>{if(!value)throw Error('Native chunk write invariant failed');};
  try{
   await write(name,'original');
   let id=native.beginWrite!(name);native.appendWrite!(id,'incomplete');native.cancel(id);require(await read(name)==='original');
   id=native.beginWrite!(name);let rejected=false;try{native.appendWrite!(id,'x'.repeat(512*1024+1));}catch{rejected=true;}finally{native.cancel(id);}require(rejected&&await read(name)==='original');
   const slots:number[]=[];try{for(let i=0;i<8;i++)slots.push(native.beginWrite!(name));rejected=false;try{native.beginWrite!(name);}catch{rejected=true;}require(rejected);}finally{for(const slot of slots)native.cancel(slot);}
   rejected=false;try{await writeChunks(name,async append=>{append('incomplete');throw Error('producer failure');});}catch{rejected=true;}require(rejected&&await read(name)==='original');
   id=native.beginWrite!(name);try{const chunk='x'.repeat(65536);for(let i=0;i<256;i++){native.appendWrite!(id,chunk);if(i%8===7)await delay(0);}rejected=false;try{native.appendWrite!(id,'x');}catch{rejected=true;}require(rejected);}finally{native.cancel(id);}require(await read(name)==='original');
   const value:any={schema:1,id:'game-write-native',title:'分块保存',revision:1,updatedAt:1,lua:'ascii',summary:'',messages:[{role:'user',content:'界面😀'.repeat(60000),timestamp:1}],versions:[]};
   await writeChunks(name,append=>streamProject(value,append,()=>delay(0)));require(await read(name)===JSON.stringify(value));
   await write('prompt.txt','PASS atomic chunked write: UTF-8/surrogate boundary, cancellation, producer error, 8-slot exhaustion/recovery, 16MiB budget; existing file preserved until commit');
  }catch(e){failed=1;await write('prompt.txt',String(e));}finally{ready=1;}
 })();
 return <View style={{width:960,height:720}}><Text>Storage verification</Text></View>;
});
