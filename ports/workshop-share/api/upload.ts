import {put} from '@vercel/blob';
import {createHash,timingSafeEqual} from 'node:crypto';
import type {VercelRequest,VercelResponse} from '@vercel/node';
export const config={maxDuration:30};
export function authorized(header:unknown,secret:string){
 return typeof header==='string'&&secret.length>=32&&timingSafeEqual(new Uint8Array(createHash('sha256').update(header).digest()),new Uint8Array(createHash('sha256').update('Bearer '+secret).digest()));
}
export function validateUpload(value:unknown){
 const b=value as any;
 if(!b||typeof b.title!=='string'||!b.title.trim()||b.title.length>120||typeof b.cart!=='string'||Buffer.byteLength(b.cart)>3*1024*1024||!b.cart.startsWith('pico-8 cartridge // http://www.pico-8.com\nversion 42\n__lua__\n')||!b.cart.includes('\n__gfx__\n')||!Number.isInteger(b.revision)||b.revision<0)throw Error('invalid upload');
 // No generated HTML/JS and no project conversations are uploaded.
 const payload={schema:1,title:b.title.trim(),revision:b.revision,summary:typeof b.summary==='string'?b.summary.slice(0,240):'',cart:b.cart};
 const raw=JSON.stringify(payload),id=createHash('sha256').update(raw).digest('hex');return {raw,id};
}
export default async function handler(req:VercelRequest,res:VercelResponse){
 res.setHeader('Cache-Control','no-store');
 if(req.method!=='POST')return res.status(405).json({error:'仅支持上传'});
 if(!authorized(req.headers.authorization,process.env.SHARING_UPLOAD_KEY||''))return res.status(401).json({error:'分享凭据无效'});
 let data:ReturnType<typeof validateUpload>;try{data=validateUpload(req.body);}catch{return res.status(400).json({error:'游戏格式无效或超过 3MiB 分享预算'});}
 try{
  await put('games/'+data.id+'.json',data.raw,{access:'public',addRandomSuffix:false,allowOverwrite:true,contentType:'application/json',cacheControlMaxAge:31536000});
  return res.status(200).json({id:data.id,path:'/g/'+data.id});
 }catch{return res.status(503).json({error:'上传暂时失败，请重试'});}
}
