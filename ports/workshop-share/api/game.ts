import {head} from '@vercel/blob';
import type {VercelRequest,VercelResponse} from '@vercel/node';
export default async function handler(req:VercelRequest,res:VercelResponse){
 if(req.method!=='GET')return res.status(405).json({error:'仅支持读取'});
 const id=req.query.id;if(typeof id!=='string'||!/^[a-f0-9]{64}$/.test(id))return res.status(400).json({error:'分享地址无效'});
 try{
  const blob=await head('games/'+id+'.json');
  const response=await fetch(blob.url,{signal:AbortSignal.timeout(10000)});if(!response.ok)throw Error('blob unavailable');
  res.setHeader('Cache-Control','public, max-age=3600, s-maxage=31536000, immutable');
  res.setHeader('Content-Type','application/json; charset=utf-8');res.setHeader('X-Content-Type-Options','nosniff');
  return res.status(200).send(await response.text());
 }catch{return res.status(404).json({error:'游戏暂时无法读取，请检查地址或稍后重试'});}
}
