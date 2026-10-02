import {getOps,registerTexture} from '@pocketjs/framework/solid';
import {microphoneAlpha} from './microphone-mask.ts';

// One 40px texture per application launch, tinted with the NextUI text color.
export function microphoneTexture(color:string):string {
 // The native texture upload requires power-of-two dimensions; crop the padding.
 const rgba=new Uint8Array(64*64*4),rgb=[1,3,5].map(i=>parseInt(color.slice(i,i+2),16));
 for(let i=0;i<40*40;i++){
  const at=(Math.floor(i/40)*64+i%40)*4;
  rgba[at]=rgb[0];rgba[at+1]=rgb[1];rgba[at+2]=rgb[2];
  rgba[at+3]=parseInt(microphoneAlpha.slice(i*2,i*2+2),16);
 }
 const handle=getOps().uploadTexture(rgba,64,64,3);
 if(handle<0)throw new Error('Microphone icon upload failed');
 registerTexture('brick-microphone',handle);
 return 'brick-microphone';
}
