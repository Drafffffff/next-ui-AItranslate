import initialize from '/runtime/fake08.js';
let core:any,fps=30,loaded=false;
self.onmessage=async(event:MessageEvent)=>{
 try{
  const {type,cart,mask}=event.data;
  if(type==='load'){
   loaded=false;core??=await initialize({locateFile:(file:string)=>'/runtime/'+file});
   const bytes=new TextEncoder().encode(cart),pointer=core._malloc(bytes.length);
   if(!pointer)throw Error('游戏加载内存不足');
   try{core.HEAPU8.set(bytes,pointer);fps=core._web_load(pointer,bytes.length);}finally{core._free(pointer);}
   if(!fps)throw Error(core.UTF8ToString(core._web_error())||'游戏无法启动');
   loaded=true;postMessage({type:'ready',fps});
  }else if(type==='frame'&&loaded){
   const samples=core._web_step(mask&63);if(!samples)throw Error(core.UTF8ToString(core._web_error())||'游戏运行出错');
   const pixels=core.HEAPU8.slice(core._web_pixels(),core._web_pixels()+128*128*4),at=core._web_audio()/2;
   const audio=core.HEAP16.slice(at,at+samples*2);
   postMessage({type:'frame',pixels,audio,samples},[pixels.buffer,audio.buffer]);
  }
 }catch(error){loaded=false;postMessage({type:'error',error:error instanceof Error?error.message:'游戏运行失败'});}
};
