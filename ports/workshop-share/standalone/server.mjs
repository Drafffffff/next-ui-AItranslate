import {createServer} from 'node:http';
import {readFile, mkdir, open, rename, unlink} from 'node:fs/promises';
import {createHash, randomUUID} from 'node:crypto';
import {join, resolve} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {authorized, validateUpload} from '../api/upload.ts';

const MAX_BODY = 6 * 1024 * 1024;
const CSP = "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'";
const ASSETS = new Map([
  ['/index.html','text/html; charset=utf-8'], ['/player.js','text/javascript; charset=utf-8'],
  ['/worker.js','text/javascript; charset=utf-8'], ['/player.css','text/css; charset=utf-8'],
  ['/fonts/workshop-pixel.woff2','font/woff2'], ['/fonts/LICENSE.txt','text/plain; charset=utf-8'],
  ['/runtime/fake08.js','text/javascript; charset=utf-8'], ['/runtime/fake08.wasm','application/wasm'],
  ...['FAKE08-LICENSE.md','Z8LUA-LICENSE.txt','ERIS-LICENSE.txt','MINIZ-LICENSE.txt','LODEPNG-LICENSE.txt'].map(x=>['/runtime/'+x,'text/plain; charset=utf-8']),
]);
class HttpError extends Error {constructor(status,message){super(message);this.status=status;}}
function send(res,status,body,type='application/json; charset=utf-8',head=false,cache='no-store') {
  const bytes=Buffer.isBuffer(body)?body:Buffer.from(typeof body==='string'?body:JSON.stringify(body));
  res.writeHead(status,{'Content-Type':type,'Content-Length':bytes.length,'Cache-Control':cache,
    'Content-Security-Policy':CSP,'X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer'});
  res.end(head?undefined:bytes);
}
function receive(req) {
  if(req.headers['content-length'] && (!/^\d+$/.test(req.headers['content-length']) || Number(req.headers['content-length'])>MAX_BODY)) {
    req.resume(); return Promise.reject(new HttpError(413,'分享请求过大'));
  }
  return new Promise((resolveBody,reject)=>{
    let bytes=0,finished=false; const chunks=[];
    const fail=error=>{if(finished)return;finished=true;chunks.length=0;reject(error);};
    req.on('data',chunk=>{
      if(finished)return; bytes+=chunk.length;
      if(bytes>MAX_BODY){fail(new HttpError(413,'分享请求过大'));return;}
      chunks.push(chunk);
    });
    req.on('end',()=>{if(finished)return;finished=true;resolveBody(Buffer.concat(chunks,bytes));});
    req.on('aborted',()=>fail(new HttpError(400,'分享上传已中断')));
    req.on('error',()=>fail(new HttpError(400,'分享上传已中断')));
  });
}
async function saveCart(dir,id,raw) {
  const temp=join(dir,'.'+id+'.'+randomUUID()+'.tmp');let handle;
  try {
    handle=await open(temp,'wx',0o600);await handle.writeFile(raw);await handle.sync();await handle.close();handle=undefined;
    await rename(temp,join(dir,id+'.json'));
    const directory=await open(dir,'r');try{await directory.sync();}finally{await directory.close();}
  } finally {await handle?.close();await unlink(temp).catch(error=>{if(error.code!=='ENOENT')throw error;});}
}

export async function createService({gameDir,publicDir,uploadKey}) {
  if(typeof uploadKey!=='string'||uploadKey.length<32)throw Error('上传密钥未配置');
  gameDir=resolve(gameDir);publicDir=resolve(publicDir);
  await mkdir(gameDir,{recursive:true,mode:0o700});
  const server=createServer(async(req,res)=>{
    const head=req.method==='HEAD';
    try {
      const url=new URL(req.url,'http://workshop.local');
      if(url.pathname==='/api/upload') {
        if(req.method!=='POST'){req.resume();throw new HttpError(405,'仅支持上传');}
        if(!authorized(req.headers.authorization,uploadKey)){req.resume();throw new HttpError(401,'分享凭据无效');}
        if(url.search){req.resume();throw new HttpError(400,'分享地址无效');}
        if(req.headers['content-type']?.split(';')[0].trim().toLowerCase()!=='application/json'){req.resume();throw new HttpError(415,'仅支持 JSON 游戏数据');}
        const body=await receive(req);let data;
        try {data=validateUpload(JSON.parse(body.toString('utf8')));}catch{throw new HttpError(400,'游戏格式无效或超过 3MiB 分享预算');}
        await saveCart(gameDir,data.id,data.raw);
        send(res,200,{id:data.id,path:'/g/'+data.id});return;
      }
      if(req.method!=='GET'&&!head){req.resume();throw new HttpError(405,'仅支持读取');}
      if(url.pathname==='/health'){send(res,200,{ok:true,service:'workshop-native-share'},undefined,head);return;}
      if(url.pathname==='/api/game') {
        const ids=url.searchParams.getAll('id');
        if(ids.length!==1||!/^[a-f0-9]{64}$/.test(ids[0])||[...url.searchParams.keys()].some(x=>x!=='id'))throw new HttpError(400,'分享地址无效');
        let raw;
        try {raw=await readFile(join(gameDir,ids[0]+'.json'));}
        catch(error){if(error.code==='ENOENT')throw new HttpError(404,'游戏不存在');throw error;}
        if(createHash('sha256').update(raw).digest('hex')!==ids[0])throw Error('卡带存储损坏');
        send(res,200,raw,undefined,head,'public, max-age=3600, immutable');return;
      }
      if(url.search)throw new HttpError(400,'分享地址无效');
      const path=url.pathname==='/'||/^\/g\/[a-f0-9]{64}$/.test(url.pathname)?'/index.html':url.pathname;
      const type=ASSETS.get(path)||(/^\/player\.[a-f0-9]{16}\.(css|js)$/.test(path)?(path.endsWith('.css')?'text/css; charset=utf-8':'text/javascript; charset=utf-8'):undefined);if(!type)throw new HttpError(404,'分享地址不存在');
      send(res,200,await readFile(join(publicDir,path.slice(1))),type,head,path==='/index.html'?'public, max-age=0, must-revalidate':'public, max-age=300');
    } catch(error) {
      if(res.destroyed)return;
      const status=error instanceof HttpError?error.status:503;
      send(res,status,{error:error instanceof HttpError?error.message:'分享服务暂时失败，请重试'},undefined,head);
    }
  });
  server.requestTimeout=30000;server.headersTimeout=15000;server.timeout=30000;
  return server;
}

if(process.argv[1]&&pathToFileURL(resolve(process.argv[1])).href===import.meta.url) {
  try {
    const server=await createService({gameDir:process.env.WORKSHOP_GAME_DIR||'/data/games',
      publicDir:process.env.WORKSHOP_PUBLIC_DIR||fileURLToPath(new URL('./public/',import.meta.url)),
      uploadKey:process.env.SHARING_UPLOAD_KEY});
    server.listen(Number(process.env.PORT||3000),'0.0.0.0',()=>console.log('游戏分享服务已启动'));
    const stop=()=>server.close(()=>process.exit(0));process.on('SIGTERM',stop);process.on('SIGINT',stop);
  } catch {console.error('分享服务启动失败，请检查上传密钥及持久存储权限');process.exitCode=1;}
}
