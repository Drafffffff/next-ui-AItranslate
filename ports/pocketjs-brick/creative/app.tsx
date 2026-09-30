import {createSignal,For,Show} from 'solid-js';
import {mount} from '@pocketjs/framework/solid';
import {View,Text} from '@pocketjs/framework/solid/components';
import {onFrame} from '@pocketjs/framework/solid/lifecycle';
import {BTN} from '@pocketjs/framework/input';
import {installServices,read,write,native} from '../app/services.ts';
import {PALETTE,demo,fill,paint,svg,decodeProject,type Project} from './model.ts';
const hw=native as typeof native & {canvas(state:unknown):void;feedback(color:number,ms:number):void;metrics():{fps:number;p95:number;peak:number;present:number;battery:number;temperature:number;hardware:boolean;ledError:boolean;motorError:boolean}};
function App(){
  installServices();
  let project=demo(),oldButtons=0,tick=0,held=0,direction=0,playClock=0,dirty=true,saveBusy=false;
  const undo:Project[]=[],redo:Project[]=[];
  const [page,setPage]=createSignal(0),[frame,setFrame]=createSignal(0),[color,setColor]=createSignal(2),[tool,setTool]=createSignal(0),[x,setX]=createSignal(15),[y,setY]=createSignal(15),[playing,setPlaying]=createSignal(false),[onion,setOnion]=createSignal(true),[version,setVersion]=createSignal(0),[status,setStatus]=createSignal('正在读取作品…'),[ready,setReady]=createSignal(false),[count,setCount]=createSignal(64),[motion,setMotion]=createSignal(0),[phase,setPhase]=createSignal(0),[metrics,setMetrics]=createSignal(hw.metrics());
  const snapshot=()=>JSON.parse(JSON.stringify(project)) as Project;
  const push=()=>{undo.push(snapshot());if(undo.length>24)undo.shift();redo.length=0;};
  const refresh=()=>{dirty=true;setVersion(v=>v+1);};
  const feedback=(ms=0)=>hw.feedback(parseInt(PALETTE[color()].slice(1),16),ms);
  const save=async(exporting=false)=>{if(saveBusy)return;saveBusy=true;try{await write(exporting?('pixel-export.'+'svg'):'pixel-project.json',exporting?svg(project,frame()):JSON.stringify(project));setStatus(exporting?'已导出 pixel-export.svg':'作品已保存');feedback(30);}catch(e){setStatus(e instanceof Error?e.message:'保存失败');}finally{saveBusy=false;}};
  const edit=()=>{const i=frame(),source=project.frames[i];project.frames[i]=tool()===1?fill(source,project.size,x(),y(),color()):paint(source,project.size,x(),y(),tool()===2?0:color());refresh();};
  const restore=(source:Project[],target:Project[])=>{const p=source.pop();if(!p)return;target.push(snapshot());project=p;setFrame(Math.min(frame(),p.frames.length-1));setX(Math.min(x(),p.size-1));setY(Math.min(y(),p.size-1));refresh();feedback(18);};
  const selectFrame=(delta:number)=>{setFrame(i=>(i+delta+project.frames.length)%project.frames.length);refresh();feedback(15);};
  onFrame(buttons=>{
    tick++;const pressed=buttons&~oldButtons,released=oldButtons&~buttons;oldButtons=buttons;
    if(!ready())return;
    if(tick%60===0)setMetrics(hw.metrics());
    const l=!!(buttons&BTN.LTRIGGER),r=!!(buttons&BTN.RTRIGGER);
    if(pressed&BTN.SELECT){setPage(p=>(p+1)%4);setPlaying(false);dirty=true;feedback(20);}
    if(pressed&BTN.START){setPlaying(p=>!p);setStatus(playing()?'播放中，Start 暂停':'编辑中');}
    if(page()<2 && (pressed&BTN.CIRCLE) && (l||r))void save(!l&&r);
    if(page()<2 && (pressed&BTN.CROSS) && (l||r)){if(l && project.frames.length<8){push();project.frames.splice(frame()+1,0,project.frames[frame()]);setFrame(i=>i+1);refresh();setStatus('已复制当前帧');}else if(r && project.frames.length>1){push();project.frames.splice(frame(),1);setFrame(i=>Math.min(i,project.frames.length-1));refresh();setStatus('已删除当前帧');}
    }
    if(page()===0){
      if((pressed&BTN.CIRCLE)&&!l&&!r&&!playing()){push();edit();}
      if((pressed&BTN.CROSS)&&!l&&!r)restore(undo,redo);
      if(pressed&BTN.SQUARE)restore(redo,undo);
      if(pressed&BTN.TRIANGLE)setTool(t=>(t+1)%3);
      if((pressed&BTN.LTRIGGER)&&!(buttons&BTN.CIRCLE)){setColor(c=>(c+15)%16);feedback();}
      if((pressed&BTN.RTRIGGER)&&!(buttons&BTN.CIRCLE)){setColor(c=>(c+1)%16);feedback();}
      const d=buttons&(BTN.LEFT|BTN.RIGHT|BTN.UP|BTN.DOWN);if(d!==direction){held=0;direction=d;}else held++;
      if(d && ((pressed&d)||held>=18&&held%4===0)&&!playing()){
        if(l){project.fps=Math.max(2,Math.min(24,project.fps+(d&(BTN.UP|BTN.RIGHT)?1:-1)));refresh();}
        else{setX(v=>Math.max(0,Math.min(project.size-1,v+(d&BTN.RIGHT?1:d&BTN.LEFT?-1:0))));setY(v=>Math.max(0,Math.min(project.size-1,v+(d&BTN.DOWN?1:d&BTN.UP?-1:0))));dirty=true;if(buttons&BTN.CIRCLE)edit();}
      }
    }else if(page()===1){
      if(pressed&BTN.LEFT)selectFrame(-1);if(pressed&BTN.RIGHT)selectFrame(1);
      if((pressed&BTN.CIRCLE)&&!l&&!r){setOnion(v=>!v);dirty=true;}
      if(pressed&BTN.TRIANGLE){push();project.frames=project.frames.map(f=>f.split('').reverse().join(''));refresh();setStatus('已旋转所有帧 180 度');}
      if((pressed&BTN.CROSS)&&!l&&!r){setPage(0);dirty=true;}
    }else if(page()===2){
      if(pressed&BTN.RIGHT)setCount(v=>Math.min(4096,v*4));if(pressed&BTN.LEFT)setCount(v=>Math.max(64,v/4));
      if(pressed&BTN.CIRCLE){feedback(40);setStatus('已请求颜色灯光与短震动');}
      if(pressed&BTN.TRIANGLE){push();project={version:1,size:project.size===64?16:project.size*2,fps:8,frames:[]};project.frames=['0'.repeat(project.size*project.size)];setFrame(0);setX(0);setY(0);refresh();setStatus('已新建画布，B 可撤销');}
      if(pressed&BTN.CROSS){setPage(0);dirty=true;}
    }else {if(pressed&BTN.LEFT)setMotion(v=>v-15);if(pressed&BTN.RIGHT)setMotion(v=>v+15);if(pressed&BTN.CROSS){setPage(0);dirty=true;}}
    if(playing()&&page()===3)setMotion(v=>v+2);
    if(playing()&&page()===2)setPhase(v=>(v+1)%16);
    if(playing()&&page()<2){playClock++;if(playClock>=60/project.fps){playClock=0;setFrame(i=>(i+1)%project.frames.length);dirty=true;}}
    if(released&BTN.CIRCLE)dirty=true;
    if(dirty){hw.canvas({visible:page()<2,size:project.size,x:x(),y:y(),pixels:project.frames[frame()],previous:project.frames[(frame()+project.frames.length-1)%project.frames.length],onion:onion()&&!playing()});dirty=false;}
  });
  void (async()=>{try{const raw=await read('pixel-project.json');if(raw)project=decodeProject(raw);setX(Math.min(15,project.size-1));setY(Math.min(15,project.size-1));setStatus(raw?'已恢复作品':'从机器人动画开始创作');}catch(e){setStatus(e instanceof Error?e.message:'读取失败');}setReady(true);refresh();})();
  (globalThis as Record<string,unknown>).__creativeAcceptance=(op:number)=>{
    if(op===0)return ready()?1:0;
    if(op===1){push();project.frames[0]=fill(project.frames[0],project.size,0,0,3);refresh();return 1;}
    if(op===2){void save();return 1;}
    if(op===3)return saveBusy?0:1;
    if(op===4)return parseInt(project.frames[0][0],16);
    if(op===5){restore(undo,redo);return parseInt(project.frames[0][0],16);}
    if(op===6){setPage(2);dirty=true;setCount(1024);return 1;}
    if(op===7){setPage(3);dirty=true;setMotion(35);return 1;}
    if(op===8){void save(true);return 1;}
    if(op===9)return project.frames.length;
    if(op===10){setPage(0);dirty=true;return 1;}
    if(op>=11&&op<=14){setPage(2);dirty=true;setCount([64,256,1024,4096][op-11]);setPlaying(true);return 1;}
    if(op===15){setPage(3);dirty=true;setPlaying(true);return 1;}
    if(op===16){feedback(40);return 1;}
    if(op===17){const m=hw.metrics();return m.hardware&&!m.ledError&&!m.motorError&&m.battery>=0&&m.temperature>0?1:0;}
    return -1;
  };
  return <View class="w-full h-full bg-[#191d28]">
    <View class="absolute left-[40] top-[22] w-[944] flex-row justify-between items-center"><Text class="text-5xl font-bold text-[#f1e8ce]">像素工坊</Text><Text class="text-2xl text-[#e8b54a]">BRICK / CREATIVE LAB</Text></View>
    <View class="absolute left-[40] top-[98] flex-row gap-[24]"><For each={['画布','动画','性能与硬件','3D 透视']} >{(name,i)=><Text class={page()===i()?'text-2xl text-[#e8b54a]':'text-2xl text-[#9ba7b5]'}>{name}</Text>}</For></View>
    <Show when={page()<2}>
      <View class="absolute left-[590] top-[146] w-[394] flex-col gap-[12]">
        <Text class="text-4xl text-[#f1e8ce]">{page()===0?['铅笔','填充','橡皮'][tool()]:'帧动画'}</Text>
        <Text class="text-2xl text-[#9ba7b5]">{version()>=0?`${project.size} × ${project.size}  /  ${project.fps} FPS`:''}</Text>
        <Text class="text-4xl text-[#e8b54a]">{frame()+1} / {version()>=0?project.frames.length:0} 帧</Text>
        <Text class="text-2xl text-[#9ba7b5]">{playing()?'动画播放中':`光标 ${x()+1}, ${y()+1}`}</Text>
        <View class="w-[352] h-[96] relative"><For each={PALETTE}>{(c,i)=><View class="absolute w-[40] h-[40]" style={{insetL:(i()%8)*44,insetT:Math.floor(i()/8)*48,bgColor:c}}><Show when={color()===i()}><View class="absolute inset-[8] bg-[#f1e8ce]" /></Show></View>}</For></View>
        <Text class="text-2xl text-[#f1e8ce]">{page()===0?'A 绘画 · X 换工具':'左右切帧 · A 洋葱皮'}</Text>
        <Text class="text-2xl text-[#9ba7b5]">{page()===0?'L / R 换颜色':'X 旋转所有帧'}</Text>
        <Text class="text-2xl text-[#9ba7b5]">{page()===0?'B 撤销 · Y 重做':onion()?'洋葱皮已开启':'洋葱皮已关闭'}</Text>
        <Text class="text-2xl text-[#e8b54a]">L+A 保存 · R+A 导出</Text>
        <Text class="text-2xl text-[#9ba7b5]">L+B 加帧 · R+B 删帧</Text>
      </View>
    </Show>
    <Show when={page()===2}>
      <View class="absolute left-[40] top-[146] w-[512] h-[512] overflow-hidden"><For each={Array.from({length:count()},(_,i)=>i)}>{i=><View class="absolute" style={{insetL:(i%Math.sqrt(count()))*512/Math.sqrt(count()),insetT:Math.floor(i/Math.sqrt(count()))*512/Math.sqrt(count()),width:512/Math.sqrt(count())-1,height:512/Math.sqrt(count())-1,bgColor:PALETTE[(i+phase())%16]}} />}</For></View>
      <View class="absolute left-[590] top-[146] w-[394] flex-col gap-[12]">
        <Text class="text-4xl text-[#f1e8ce]">{count()} 个 UI 节点</Text>
        <Text class="text-2xl text-[#9ba7b5]">左右调整 · Start 动态重绘</Text>
        <Text class="text-4xl text-[#e8b54a]">{metrics().fps.toFixed(1)} FPS</Text>
        <Text class="text-2xl text-[#9ba7b5]">绘制 P95 {metrics().p95.toFixed(2)} ms</Text>
        <Text class="text-2xl text-[#9ba7b5]">绘制峰值 {metrics().peak.toFixed(2)} ms</Text>
        <Text class="text-2xl text-[#9ba7b5]">呈现 {metrics().present.toFixed(2)} ms</Text>
        <Text class="text-2xl text-[#f1e8ce]">{metrics().hardware?`电量 ${metrics().battery}%  温度 ${(metrics().temperature/1000).toFixed(1)}℃`:'Mac 硬件模拟'}</Text>
        <Text class="text-2xl text-[#e8b54a]">A 灯光与震动 · X 换尺寸</Text>
        <Text class="text-2xl text-[#9ba7b5]">{metrics().ledError?'灯光接口未通过':metrics().motorError?'震动接口未通过':'退出时恢复硬件设置'}</Text>
      </View>
    </Show>
    <Show when={page()===3}>
      <View class="absolute left-[40] top-[146] w-[512] h-[512] perspective-[800]">
        <View class="absolute left-[146] top-[146] w-[220] h-[220]" style={{rotateX:-25,rotateY:motion()}}>
          <View class="absolute inset-0 bg-[#e8b54a] translate-z-[110]" />
          <View class="absolute inset-0 bg-[#b94749] translate-z-[-110] rotate-y-[180]" />
          <View class="absolute inset-0 bg-[#639caa] translate-x-[110] rotate-y-[90]" />
          <View class="absolute inset-0 bg-[#385e48] translate-x-[-110] rotate-y-[-90]" />
          <View class="absolute inset-0 bg-[#f1e8ce] translate-y-[-110] rotate-x-[90]" />
          <View class="absolute inset-0 bg-[#8f5377] translate-y-[110] rotate-x-[-90]" />
        </View>
      </View>
      <View class="absolute left-[590] top-[146] w-[394] flex-col gap-[24]"><Text class="text-4xl text-[#f1e8ce]">透视立方体</Text><Text class="text-2xl text-[#9ba7b5]">左右旋转 · Start 自动转动</Text><Text class="text-4xl text-[#e8b54a]">{metrics().fps.toFixed(1)} FPS</Text><Text class="text-2xl text-[#9ba7b5]">软件渲染 UI 透视变换</Text><Text class="text-2xl text-[#9ba7b5]">不代表 wgpu / GPU 3D</Text></View>
    </Show>
    <Text class="absolute left-[40] top-[678] text-2xl text-[#f1e8ce]">{status().slice(0,38)}</Text>
    <Text class="absolute left-[40] top-[722] text-2xl text-[#9ba7b5]">Select 切换页面 · Start 播放 · MENU 退出</Text>
  </View>;
}
mount(App);
