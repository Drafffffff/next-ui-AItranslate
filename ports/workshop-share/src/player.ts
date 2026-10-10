const element=<T extends HTMLElement>(id:string)=>document.getElementById(id) as T;
const canvas=element<HTMLCanvasElement>('screen'),context=canvas.getContext('2d')!,overlay=element('overlay'),status=element('status'),start=element<HTMLButtonElement>('start');
const pause=element<HTMLButtonElement>('pause'),restart=element<HTMLButtonElement>('restart'),sound=element<HTMLButtonElement>('sound');
let cart='',worker:Worker|undefined,fps=30,ready=false,running=false,pending=false,last=0,muted=false,audio:AudioContext|undefined,audioTime=0,watchdog:number|undefined;
const keyboard=new Set<number>(),pointers=new Map<number,number>(),taps=new Set<number>();
const keys:Record<string,number>={ArrowLeft:0,ArrowRight:1,ArrowUp:2,ArrowDown:3,z:4,Z:4,x:5,X:5,c:4,C:4};
function clearInputs(){keyboard.clear();pointers.clear();taps.clear();for(const button of document.querySelectorAll('[data-key]'))button.classList.remove('active');}
function mask(){let n=0;for(const key of keyboard)n|=1<<key;for(const key of pointers.values())n|=1<<key;
 // A quick down/up between VM frames must still reach one update; held keys remain continuous.
 for(const key of taps)n|=1<<key;taps.clear();
 const pad=navigator.getGamepads?.()[0];if(pad){if(pad.axes[0]<-.3||pad.buttons[14]?.pressed)n|=1;if(pad.axes[0]>.3||pad.buttons[15]?.pressed)n|=2;if(pad.axes[1]<-.3||pad.buttons[12]?.pressed)n|=4;if(pad.axes[1]>.3||pad.buttons[13]?.pressed)n|=8;if(pad.buttons[0]?.pressed)n|=16;if(pad.buttons[1]?.pressed)n|=32;}return n;}
function show(message:string,label?:string){overlay.hidden=false;status.textContent=message;status.hidden=!message;start.hidden=!label;if(label)start.textContent=label;}
function failure(message:string){ready=false;running=false;pending=false;document.body.dataset.state='error';worker?.terminate();clearTimeout(watchdog);show(message,'重新加载');audio?.suspend();}
function launch(){
 worker?.terminate();clearTimeout(watchdog);ready=false;running=false;pending=false;document.body.dataset.state='loading';clearInputs();pause.disabled=restart.disabled=sound.disabled=true;show('正在启动游戏');
 worker=new Worker('/worker.js',{type:'module'});
 watchdog=window.setTimeout(()=>failure('游戏启动时间过长，请重新加载'),15000);
 worker.onerror=()=>failure('运行器加载失败，请检查网络后重试');
 worker.onmessage=event=>{
  const value=event.data;
  if(value.type==='ready'){
   clearTimeout(watchdog);fps=value.fps;ready=true;document.body.dataset.state='ready';pause.textContent='暂停';pause.disabled=true;restart.disabled=sound.disabled=false;show('','开始游玩');
   // One quiet preview frame, before the user enables gameplay and sound.
   pending=true;worker!.postMessage({type:'frame',mask:0});watchdog=window.setTimeout(()=>failure('游戏停止响应，可以重新加载'),5000);
  }
  else if(value.type==='error')failure('游戏运行失败：'+String(value.error).slice(0,160));
  else if(value.type==='frame'){
   clearTimeout(watchdog);pending=false;context.putImageData(new ImageData(new Uint8ClampedArray(value.pixels),128,128),0,0);
   if(audio?.state==='running'&&!muted&&running){
    const buffer=audio.createBuffer(2,value.samples,22050),pcm=value.audio as Int16Array;
    for(let channel=0;channel<2;channel++){const out=buffer.getChannelData(channel);for(let i=0;i<value.samples;i++)out[i]=pcm[i*2+channel]/32768;}
    audioTime=Math.max(audioTime,audio.currentTime+.03);
    // Drop a backlog when background throttling or a slow frame catches up.
    if(audioTime-audio.currentTime<.25){const source=audio.createBufferSource();source.buffer=buffer;source.connect(audio.destination);source.start(audioTime);audioTime+=buffer.duration;}
   }
  }
 };
 worker.postMessage({type:'load',cart});
}
async function begin(){
 if(!cart){await load();return;}if(!ready){launch();return;}
 try{audio??=new AudioContext({latencyHint:'interactive'});await audio.resume();}catch{muted=true;sound.textContent='声音关';sound.setAttribute('aria-pressed','false');}
 audioTime=audio?.currentTime||0;running=true;last=0;document.body.dataset.state='playing';pause.textContent='暂停';pause.disabled=false;overlay.hidden=true;canvas.focus({preventScroll:true});
}
function setPaused(){running=false;document.body.dataset.state='paused';clearInputs();audio?.suspend();pause.textContent='继续';show('','继续游玩');}
start.onclick=()=>void begin();pause.onclick=()=>running?setPaused():void begin();restart.onclick=launch;
sound.onclick=()=>{muted=!muted;sound.textContent=muted?'声音关':'声音开';sound.setAttribute('aria-pressed',String(!muted));};
element('fullscreen').onclick=()=>{if(document.fullscreenElement)void document.exitFullscreen();else void document.querySelector('main')?.requestFullscreen().catch(()=>{if(running)setPaused();show('当前浏览器不支持全屏','继续游玩');});};
for(const button of document.querySelectorAll<HTMLButtonElement>('[data-key]')){
 button.addEventListener('pointerdown',event=>{event.preventDefault();button.setPointerCapture(event.pointerId);const key=Number(button.dataset.key);pointers.set(event.pointerId,key);if(running)taps.add(key);button.classList.add('active');});
 const release=(event:PointerEvent)=>{pointers.delete(event.pointerId);if(![...pointers.values()].includes(Number(button.dataset.key)))button.classList.remove('active');};
 button.addEventListener('pointerup',release);button.addEventListener('pointercancel',release);button.addEventListener('lostpointercapture',release);
}
window.addEventListener('keydown',event=>{if(keys[event.key]!==undefined){event.preventDefault();keyboard.add(keys[event.key]);if(running&&!event.repeat)taps.add(keys[event.key]);}});
window.addEventListener('keyup',event=>{if(keys[event.key]!==undefined){event.preventDefault();keyboard.delete(keys[event.key]);}});
window.addEventListener('blur',()=>{if(running)setPaused();else clearInputs();});
document.addEventListener('visibilitychange',()=>{if(document.hidden&&running)setPaused();});
function frame(now:number){
 if(ready&&running&&!pending&&now-last>=1000/fps-1){last=now;pending=true;worker!.postMessage({type:'frame',mask:mask()});watchdog=window.setTimeout(()=>failure('游戏停止响应，可以重新加载'),5000);}
 requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
async function load(){
 const id=location.pathname.match(/^\/g\/([a-f0-9]{64})$/)?.[1];
 if(!id){show('在掌机的项目列表中分享，然后扫码开玩。');return;}
 try{
  show('正在读取游戏');const response=await fetch('/api/game?id='+id,{signal:AbortSignal.timeout(15000)});
  if(!response.ok)throw Error('游戏读取失败，请稍后重试');const game=await response.json();
  if(game.schema!==1||typeof game.cart!=='string')throw Error('分享数据无效');cart=game.cart;
  canvas.setAttribute('aria-label',game.title+'，游戏画面');document.title=game.title+' · 游戏生成器';launch();
 }catch(error){failure(error instanceof Error?error.message:'网络连接失败，请重试');}
}
void load();
