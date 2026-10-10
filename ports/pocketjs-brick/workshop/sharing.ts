import qrcode from 'qrcode-generator';
import {request,delay} from '../app/services.ts';
import {cartridge,recoverProject,saveProject,type Project,type Config} from './model.ts';
import {contentHash} from './project-state.ts';
import {transientFailure,MAX_NETWORK_RETRIES} from './retry.ts';
export function qrMatrix(url:string){
 if(!/^https:\/\/[^\s]+$/.test(url)||url.length>512)throw new Error('分享地址无效');
 const code=qrcode(0,'M');code.addData(url,'Byte');code.make();const size=code.getModuleCount();let bits='';
 for(let y=0;y<size;y++)for(let x=0;x<size;x++)bits+=code.isDark(y,x)?'1':'0';
 return {size,bits,pixels:384};
}
export async function shareProject(p:Project,config:Config,onStatus:(s:string)=>void,signal?:AbortSignal){
 const endpoint=typeof config.shareEndpoint==='string'?config.shareEndpoint.replace(/\/+$/,''):'';
 const key=typeof config.shareKey==='string'?config.shareKey:'';
 if(!/^https:\/\/[^\s/?#]+$/.test(endpoint)||key.length<32)throw new Error('在线分享服务尚未配置');
 if(p.pending)await recoverProject(p);
 const v=p.versions.find(v=>v.revision===p.revision);if(!v)throw new Error('先生成并发布游戏，再分享');
 const cart=cartridge(v.lua,v.gfx,v.audio),hash=contentHash(cart);
 if(p.share?.hash===hash&&p.share.revision===p.revision&&p.share.url.startsWith(endpoint+'/g/'))return p.share.url;
 const body=JSON.stringify({title:v.title,summary:v.summary,revision:v.revision,cart});
 for(let attempt=0;;attempt++){
  if(signal?.aborted)throw new Error('分享已取消');
  const task=request(endpoint+'/api/upload',key,body,30000),abort=()=>task.cancel();signal?.addEventListener('abort',abort,{once:true});
  try{
   onStatus(attempt?'分享中 · 自动重试 '+attempt+'/3':'正在上传已发布游戏');
   const response=await task.promise;
   if(signal?.aborted)throw new Error('分享已取消');
   if(response.status!==200)throw new Error('HTTP '+response.status+' 分享上传失败');
   const data=JSON.parse(response.text);if(typeof data.id!=='string'||!/^[a-f0-9]{64}$/.test(data.id)||data.path!=='/g/'+data.id)throw new Error('分享服务返回地址无效');
   const share={url:endpoint+data.path,revision:p.revision,hash};p.share=share;await saveProject(p);return share.url;
  }catch(error){
   if(signal?.aborted||attempt>=MAX_NETWORK_RETRIES||!transientFailure(error))throw error;
   signal?.removeEventListener('abort',abort);await delay(1000*2**attempt,signal);
  }finally{signal?.removeEventListener('abort',abort);}
 }
}
