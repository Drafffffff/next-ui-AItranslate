/** Bounded model-transport retries. Game tools are never replayed here. */
export const MAX_NETWORK_RETRIES=3;
export function transientFailure(error:unknown){
 const message=error instanceof Error?error.message:String(error);
 const http=/^HTTP (\d+)/.exec(message);
 if(http)return [408,429,500,502,503,504].includes(Number(http[1]));
 if(/证书|certificate|TLS|取消|abort|cancel|响应内容过大|内存|参数|缺少 curl/i.test(message))return false;
 return /网络连接失败|域名解析失败|无法连接服务器|连接被重置|读取网络响应失败|请求超时|流式响应中断|ECONNRESET|ECONNREFUSED|ETIMEDOUT|EAI_AGAIN|socket hang up|network error/i.test(message);
}
export function timerDelay(ms:number,signal?:AbortSignal):Promise<void>{
 return new Promise((resolve,reject)=>{
  if(signal?.aborted){reject(new Error('Operation aborted'));return;}
  const stop=()=>{clearTimeout(timer);signal?.removeEventListener('abort',stop);reject(new Error('Operation aborted'));};
  const timer=setTimeout(()=>{signal?.removeEventListener('abort',stop);resolve();},ms);
  signal?.addEventListener('abort',stop,{once:true});
 });
}
