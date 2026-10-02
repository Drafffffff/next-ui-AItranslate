import {createSignal,createMemo,For,Show,type JSX} from 'solid-js';
import {mount} from '@pocketjs/framework/solid';
import {View,Text,Image} from '@pocketjs/framework/solid/components';
import {onFrame} from '@pocketjs/framework/solid/lifecycle';
import {BTN} from '@pocketjs/framework/input';
import {installServices,prepare} from '../../pocketjs-brick/app/services.ts';
import {COPY,mode,clock,mix,statusMessage,waveformHeight,type MicState} from './model.ts';
import {hostEntries,initialHostIndex,receiverName,edits,hubEntries,reuseEntries,reuseActionRows,replyRevision,replyReaderKey,backPage,windowStart,readRow,ellipsis,type Page,type Entry} from './navigation.ts';
import {microphoneTexture} from './microphone.ts';
interface Bridge {theme():string[];fonts(text:string):boolean;snapshot():MicState;command(op:string):boolean}
const mic=(globalThis as unknown as {mic:Bridge}).mic;
function App(){
 installServices();
 const colors=mic.theme(),bg=colors[7],ink=colors[4],muted=colors[6],accent=colors[1],selected=colors[5],line=mix(bg,ink,.13);
 const microphone=microphoneTexture(ink);
 const words='应用菜单 连接电脑 查找电脑 结束查找 正在查找 等待连接 在电脑上运行 已连接 切换 电脑 麦克风输入 Linux 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz ↑↓←→ [] + : / % 普通输入 Codex Mac 当前输入框 选择任务 任务中心 编辑文字 最近口述 AI 回复 任务提醒 操作审批 待确认的操作 进入 切换任务 返回普通输入 等待新回复 查看最近口述 不会自动发送 收到新回复后可阅读 只阅读文字仍在编辑 允许 拒绝 确认 操作已结束 等待回复 没有未读提醒 在 Mac 查看完整操作 未连接 正在连接 已发送 文字保留 继续说话 选择 说话 删除 取消 返回 阅读 左右翻页 L R 翻阅回复 任务 编辑 仅显示前段 已截断 上下阅读 按住 A 说话 松开结束 最长录音 60 秒 正在打开任务 正在识别 正在听 暂时无法显示 充电 运行中 完成 待确认 空闲 等待确认 选择一个任务 在 Mac 选择输入框 按 A 重试 A B X Y SE ST SR MENU L1 R1 L+R 退出 模式 普通 空格 回车 浏览 需要 Mac 审核 口述结果 查看 AI 回复 内容暂时无法显示 此任务暂无口述 电脑接收器未连接 未选择任务 等待语音 上下 查看 页 行 —% · … / '+JSON.stringify(edits)+JSON.stringify(COPY);
 if(!mic.fonts(words))throw new Error(COPY.fontError);
 const [state,setState]=createSignal<MicState>({state:'bluetooth'}),[page,setPage]=createSignal<Page>('main');
 const [index,setIndex]=createSignal(0),[row,setRow]=createSignal(0),[lines,setLines]=createSignal<string[]>([]),[reading,setReading]=createSignal<string[]>([]);
 const waveBars=Array.from({length:24},(_,i)=>i);
 const [waveform,setWaveform]=createSignal<number[]>(waveBars.map(()=>6));
 const [textOwner,setTextOwner]=createSignal(''),[detailMode,setDetailMode]=createSignal<'reply'|'dictation'>('reply');
 const destination=(s:MicState=state())=>s.control?.mode==='codex'?'codex:'+(s.control?.targetID||s.control?.target||''):'ordinary';
 const [choice,setChoice]=createSignal<'deny'|'allow'>('deny'),[notice,setNotice]=createSignal(''),[pulse,setPulse]=createSignal(0),[pending,setPending]=createSignal(false);
 const control=()=>state().control,codex=()=>control()?.mode==='codex',current=()=>mode(state());
 const booting=()=>['bluetooth','service','waiting'].includes(current());
 const unavailable=()=>!state().connected;
 const remoteReady=()=>state().connected&&control()?.remoteAllowed===true;
 const canEdit=()=>remoteReady()&&(!codex()||!!control()?.targetID&&control()?.codexAvailable);
 const inputBusy=()=>holding||current()==='recording'||current()==='processing'||current()==='poweroff'||pending();
 const busy=()=>inputBusy()||control()?.focusPending===true;
 const send=(command:string)=>mic.command('control:'+command);
 const mainDictation=()=>page()==='main'&&codex()&&detailMode()==='dictation'&&textOwner()===destination()&&!!state().text&&!inputBusy()&&!booting()&&current()!=='error'&&remoteReady();
 const mainReply=()=>page()==='main'&&codex()&&!!control()?.targetID&&!mainDictation()&&!inputBusy()&&!booting()&&current()!=='error'&&remoteReady();
 const reply=()=>control()?.reply?.task===control()?.targetID?control()?.reply:null;
 const mainRows=()=>mainDictation()?lines():reading();
 const rows=()=>page()==='read'?lines():reading();
 const rawEntries=():Entry[]=>page()==='connections'?hostEntries(state()):page()==='hub'?hubEntries(state()):page()==='edit'?[...edits,...(codex()&&reply()?[{id:'view-reply',label:'查看 AI 回复'}]:[])]:[];
 const entries=createMemo<Entry[]>(previous=>reuseEntries(rawEntries(),previous),[]);
 const visibleStart=()=>windowStart(index(),entries().length);
 const visibleEntries=createMemo<Entry[]>(previous=>reuseEntries(entries().slice(visibleStart(),visibleStart()+5),previous),[]);
 const isList=()=>['hub','edit','connections'].includes(page());
 const isReading=()=>['read','approval'].includes(page());
 const preparedReaders=new Map<string,string[]>();
 let readerOwner='',awaitingResult=false,recordingOwner='',recordingSession=0,observedReply='',seenReply='';
 let shoulderUsed=false,connectionReturn:Page='main',connectionIndex=0,connectionRow=0;
 let frame=0,old=0,holding=false,direction=0,held=0,deleteArmed=false,deleteHeld=0,wakeGeneration=0;
 let lastText='',readingText='',textGeneration=0,readingGeneration=0,fontLabels='',approvalID='',noticeUntil=0,awaitUntil=0,pendingID='';
 function flash(text:string){setNotice(text);noticeUntil=frame+150;}
 function open(next:Page,from:Page=page()){
  if(next==='connections'&&page()!=='connections'){connectionReturn=page();connectionIndex=index();connectionRow=row();}
  if(page()==='connections'&&next!=='connections'&&state().hosts?.discovering)mic.command('hosts:stop');
  setPage(next);setIndex(next==='connections'?initialHostIndex(state()):0);setRow(0);readingText='';setReading([]);deleteArmed=false;
  if(next==='hub')send('tasks');
  if(next==='approval'){approvalID=control()?.approval?.id||'';setChoice('deny');}
 }
 function home(){
  setPage('main');setRow(0);deleteArmed=false;
  const value=reply(),id=control()?.targetID;
  if(value&&id){const key=replyReaderKey(value),cached=preparedReaders.get(key);if(cached){readerOwner='reply:'+id;readingText=key;setReading(cached);}}
 }
 function back(){if(page()==='connections'){open(connectionReturn);setIndex(connectionIndex);setRow(connectionRow);}else open(backPage(page()),'main');}
 async function layout(text:string,reader:boolean,key='',owner=''){
  const mine=reader?++readingGeneration:++textGeneration;
  if(reader){
   setRow(0);
   if(!text){readerOwner='';setReading([]);return;}
   const cached=preparedReaders.get(key);
   if(cached){readerOwner=owner;setReading(cached);return;}
   if(readerOwner!==owner)setReading([]);
   readerOwner=owner;
  }else setLines([]);
  if(!text)return;
  try{
   const value=await prepare(text);
   if(reader&&mine===readingGeneration){
    preparedReaders.set(key,value);if(preparedReaders.size>8)preparedReaders.delete(preparedReaders.keys().next().value!);
    setReading(value);
   }else if(!reader&&mine===textGeneration)setLines(value);
  }
  catch{if(reader&&mine===readingGeneration)setReading(['内容暂时无法显示']);else if(!reader&&mine===textGeneration)flash('内容暂时无法显示');}
 }
 function selectEntry(){
  const entry=entries()[index()];if(!entry)return;
  if(page()==='connections'){
   if(entry.targetID){mic.command('hosts:choose:'+entry.targetID);home();}
   else mic.command(entry.id==='discover'?'hosts:discover':'hosts:stop');
   return;
  }
  if(page()==='hub'&&entry.targetID){
   if(!control()?.codexAvailable){flash('Codex 未打开');return;}
   if(!codex())send('mode:codex');
   if(send('choose:'+(entry.targetID||entry.id))){setDetailMode('reply');pendingID=entry.targetID||entry.id;setPending(true);awaitUntil=frame+180;home();}return;
  }
  if(page()==='edit'){
   if(entry.id==='view-reply'){setDetailMode('reply');home();}
   else if(entry.id==='read'&&codex()){
    if(state().text&&textOwner()===destination()){setDetailMode('dictation');home();}
    else flash('此任务暂无口述');
   }else if(entry.id==='read')open('read');else{send(entry.id);home();}return;
  }
  open(entry.id as Page);
 }
 function toggleMode(){
  home();setDetailMode('reply');if(codex())send('mode:ordinary');else if(control()?.codexAvailable){send('mode:codex');if(!control()?.targetID)open('hub','main');}
 }
 onFrame(buttons=>{
  frame++;const snapshot=mic.snapshot();
  if((snapshot.wakeGeneration||0)!==wakeGeneration){wakeGeneration=snapshot.wakeGeneration||0;holding=false;old=0;direction=held=0;deleteArmed=false;shoulderUsed=true;awaitingResult=false;home();setPending(false);}
  if(frame%6===0||snapshot.wakeGeneration!==state().wakeGeneration){
   const oldID=isList()?entries()[index()]?.id:null;
   if(snapshot.state==='recording'){
    const height=waveformHeight(snapshot.rms);
    setWaveform(previous=>state().state==='recording'?[...previous.slice(1),height]:waveBars.map((_,i)=>i===23?height:6));
   }
   setState(snapshot);
   if(oldID){const next=entries().findIndex(e=>e.id===oldID);if(next>=0)setIndex(next);else setIndex(i=>Math.min(i,Math.max(0,entries().length-1)));}
   if(pending()&&((frame>awaitUntil-165&&snapshot.control?.targetID===pendingID&&!snapshot.control?.focusPending)||frame>=awaitUntil||!snapshot.connected)){setPending(false);pendingID='';}
   if(page()==='approval'&&snapshot.control?.approval?.id!==approvalID){open('hub');flash('操作已结束');}
   const targetReply=snapshot.control?.mode==='codex'&&snapshot.control?.reply?.task===snapshot.control?.targetID?snapshot.control?.reply:null;
   const body=page()==='approval'?(snapshot.control?.approval?.summary||''):page()==='main'?(targetReply?.text||''):'';
   const identity=page()==='approval'?'approval:'+snapshot.control?.approval?.id+':'+body:page()==='main'&&targetReply?replyReaderKey(targetReply):'';
   if(identity!==readingText){readingText=identity;void layout(body,true,identity,page()==='approval'?'approval:'+snapshot.control?.approval?.id:'reply:'+snapshot.control?.targetID);}
   const text=snapshot.text||'';if(text!==lastText){lastText=text;setTextOwner(recordingOwner||destination(snapshot));void layout(text,false);}
   if(awaitingResult&&(snapshot.error||!snapshot.connected)){awaitingResult=false;recordingOwner='';}
   if(awaitingResult&&snapshot.state!=='recording'&&snapshot.state!=='processing'&&(snapshot.session||0)!==recordingSession){
    awaitingResult=false;
    if(text){setTextOwner(recordingOwner);setDetailMode('dictation');setRow(0);}else flash('未识别到语音');
    recordingOwner='';
   }
   // New replies arrive from either Mac or handheld turns. Do not depend on X.
   if(targetReply&&!inputBusy()){
    const version=targetReply.task+':'+replyRevision(targetReply);
    if(version!==observedReply){observedReply=version;setDetailMode('reply');setRow(0);}
    // Only an actually displayed revision is read; an open menu keeps its badge.
    if(mainReply()&&targetReply.revision&&preparedReaders.has(replyReaderKey(targetReply))&&readingText===replyReaderKey(targetReply)&&version!==seenReply){
     if(send('reply:seen:'+targetReply.revision))seenReply=version;
    }
   }
   const labels=[snapshot.error,snapshot.control?.target,snapshot.control?.approval?.title,snapshot.control?.approval?.tool,snapshot.control?.reply?.title,snapshot.control?.controlHint,notice(),...(snapshot.hosts?.known||[]).map(h=>h.name),...entries().map(e=>e.label+(e.detail||''))].join('');
   if(labels!==fontLabels){fontLabels=labels;mic.fonts(words+labels);}
  }
  if(notice()&&frame>=noticeUntil)setNotice('');
  const pressed=buttons&~old,released=old&~buttons;old=buttons;
  const shoulders=BTN.LTRIGGER|BTN.RTRIGGER;
  const connectionChord=(buttons&shoulders)===shoulders&&(pressed&shoulders)!==0;
  if(connectionChord){shoulderUsed=true;deleteArmed=false;if(!busy()){if(page()==='connections')back();else open('connections');}else flash('请先结束本次输入');return;}

  if(pressed&shoulders)shoulderUsed=!!(buttons&(BTN.UP|BTN.DOWN|BTN.LEFT|BTN.RIGHT))||busy()||page()!=='main'||(buttons&shoulders)===shoulders;
  if(buttons&shoulders&&(busy()||page()!=='main'||buttons&~shoulders))shoulderUsed=true;
  // Only a standalone shoulder release reads the reply; selection never turns into a page turn.
  if(released&shoulders&&!shoulderUsed&&!(buttons&shoulders)&&(mainDictation()||mainReply()&&reply())){
   const next=!!(released&BTN.RTRIGGER),limit=Math.max(0,mainRows().length-7);
   if(next&&row()<limit)setRow(r=>Math.min(limit,r+7));
   else if(!next&&row()>0)setRow(r=>Math.max(0,r-7));
   else if(!mainDictation()&&next&&(reply()!.page+1)<reply()!.pages)send('reply:next');
   else if(!mainDictation()&&!next&&reply()!.page>0)send('reply:prev');
  }
  // Back/cancel wins over simultaneous record/submit. A stale key never becomes approval.
  if(pressed&BTN.CROSS){
   deleteArmed=false;
   if(page()!=='main')back();
   else if(holding||current()==='recording'||current()==='processing'){holding=false;awaitingResult=false;recordingOwner='';mic.command('cancel');}
   else if(!busy()&&!booting()&&current()!=='error'&&canEdit()){send('delete');deleteArmed=true;}
   else if(!busy()&&current()!=='error'&&!canEdit())flash(codex()?'未选择任务':'无法编辑');
  }
  // Global shortcuts only belong to the home screen; subpages use A/B.
  if(page()==='main'&&!busy()&&!booting()&&current()!=='error'&&remoteReady()&&!(buttons&BTN.CROSS)){
   if(pressed&BTN.START)toggleMode();
   else if(pressed&BTN.TRIANGLE&&codex()){if(control()?.codexAvailable||control()?.approval)open('hub');}
   else if(pressed&BTN.SELECT&&canEdit()){open('edit');}
   else if(canEdit()&&(pressed&BTN.ZL)&&!(buttons&BTN.CIRCLE))send('space');
   else if(canEdit()&&(pressed&BTN.ZR)&&!(buttons&BTN.CIRCLE))send('enter');
   else if((pressed&BTN.SQUARE)&&page()==='main'&&codex()&&canEdit()){send('submit');}
  }
  if(page()==='edit'&&!busy()&&!booting()&&current()!=='error'&&canEdit()&&!(buttons&(BTN.CROSS|BTN.CIRCLE))){
   if(pressed&BTN.ZL)send('space');else if(pressed&BTN.ZR)send('enter');
  }
  if((pressed&BTN.CIRCLE)&&!(buttons&BTN.CROSS)&&!(pressed&(BTN.START|BTN.TRIANGLE|BTN.SELECT))&&!busy()){
   if(isList()){if(page()==='connections'||!unavailable())selectEntry();else flash('电脑未连接');}
   else if(page()==='approval'){
    const approval=control()?.approval;
    if(approval&&approval.id===approvalID&&approval.expiresAt>Date.now()/1000&&(choice()==='deny'||approval.allowAvailable)){
     if(send('approval:'+approvalID+':'+choice())){open('hub');flash(choice()==='allow'?'已允许':'已拒绝');}
    }else flash('操作已结束或需在 Mac 确认');
   }else if(isReading()){/* Reading is read-only; B returns to its menu. */}
   else if(current()==='error'){mic.command('retry');}
   else if(!booting()&&!unavailable()){
    if(codex()&&(!control()?.codexAvailable||!control()?.target)){if(control()?.codexAvailable)open('hub','main');else flash('Codex 已关闭');}
    else{holding=mic.command('start');if(holding){awaitingResult=true;recordingOwner=destination();recordingSession=state().session||0;setRow(0);}}
   }
  }
  if((released&BTN.CIRCLE)&&holding){holding=false;mic.command('stop');}
  const d=buttons&BTN.DOWN?BTN.DOWN:buttons&BTN.UP?BTN.UP:buttons&BTN.RIGHT?BTN.RIGHT:buttons&BTN.LEFT?BTN.LEFT:0;
  if(d!==direction){direction=d;held=0;}else if(d)held++;
  if(d&&(held===0||held>=21&&held%(held>=72?3:6)===0)&&!busy()){
   if(isList()){
    if(page()==='hub'&&(control()?.taskPages||1)>1&&(d===BTN.RIGHT||d===BTN.LEFT)){send('tasks:'+(d===BTN.RIGHT?'next':'prev'));setIndex(0);}
    else if(d===BTN.DOWN||d===BTN.UP)setIndex(i=>Math.max(0,Math.min(entries().length-1,i+(d===BTN.DOWN?1:-1))));
   }else if(isReading()){
    if(d===BTN.DOWN||d===BTN.UP)setRow(r=>readRow(r,d===BTN.DOWN?1:-1,rows().length));
    else if(page()==='approval'&&control()?.approval?.allowAvailable)setChoice(d===BTN.RIGHT?'allow':'deny');
   }else if(!booting()&&current()!=='error'&&canEdit())send((d===BTN.LEFT?'left':d===BTN.RIGHT?'right':d===BTN.UP?'up':'down')+((buttons&(BTN.LTRIGGER|BTN.RTRIGGER))?':select':''));
  }
  if(deleteArmed&&(buttons&BTN.CROSS)&&!(pressed&BTN.CROSS)&&page()==='main'&&!busy()&&!booting()&&current()!=='error'&&canEdit()){deleteHeld++;if(deleteHeld>=24&&deleteHeld%6===0)send('delete');}else deleteHeld=0;
  if(!(buttons&BTN.CROSS))deleteArmed=false;
  if(frame%3===0&&(booting()||busy()))setPulse(p=>(p+1)%120);
 });
 const title=()=>page()==='main'?(codex()?'Codex':'普通输入'):page()==='hub'?'任务中心':page()==='edit'?'编辑文字':page()==='read'?'最近口述':page()==='connections'?'连接电脑':'操作审批';
 const mainNotice=()=>statusMessage(notice()||(control()?.controlHint&&control()?.controlHint!=='已发送'?control()?.controlHint||'':''));
 const mainContext=()=>{
  const target=codex()?control()?.target||'未选择任务':receiverName(state())+(state().connected?' 当前输入框':'');
  if(mainNotice())return (codex()?ellipsis(target,12)+' · ':'')+ellipsis(mainNotice(),16);
  return ellipsis(target,mainDictation()?17:23)+(mainDictation()?' · 口述结果':'')+(mainReply()&&reply()&&reply()!.pages>1?' · '+(reply()!.page+1)+'/'+reply()!.pages:'')+(mainReply()&&reply()?.truncated?' · 已截断':'');
 };
 const context=()=>page()==='approval'?(control()?.approval?.tool||'')+' · '+(control()?.approval?.title||''):page()==='main'?mainContext():page()==='hub'?((control()?.taskPage||0)+1)+' / '+(control()?.taskPages||1):'';
 const mainStatus=()=>pending()||control()?.focusPending?'正在打开任务':current()==='recording'?'正在听':current()==='processing'?'正在识别':current()==='poweroff'?'正在关机':current()==='error'?'暂时无法连接':booting()?current()==='waiting'?'等待电脑连接':COPY[current()]:codex()&&!control()?.target?'未选择任务':'等待语音';
 const hasText=()=>!codex()&&page()==='main'&&!inputBusy()&&!booting()&&current()!=='error'&&!!state().text&&textOwner()===destination();
 const statusHint=()=>(current()==='error'?statusMessage(state().error||''):current()==='waiting'?'电脑接收器未连接':current()==='recording'?clock(state().seconds):current()==='processing'?'':current()==='poweroff'?'下次开机会回到这里':'');
 const primaryFooter=():[string,string][]=>{
  if(page()==='approval')return [['A',choice()==='deny'?'拒绝':'允许'],['B','返回'],...(reading().length>7?[['↑↓','阅读'] as [string,string]]:[]),...(control()?.approval?.allowAvailable?[['←→','选择'] as [string,string]]:[])];
  if(isReading())return [['↑↓','阅读'],['B','返回']];
  if(isList())return [['A','选择'],['B','返回'],['↑↓','浏览'],...(page()==='edit'?[['L3','空格'] as [string,string],['R3',codex()?'换行':'回车'] as [string,string]]:[]),...(page()==='hub'&&(control()?.taskPages||1)>1?[['←→','翻页'] as [string,string]]:[])];
  if(current()==='recording')return [['A','松开结束'],['B','取消']];
  if(current()==='processing')return [['B','取消']];
  if(pending())return [];
  if(current()==='poweroff')return [];
  if(current()==='error')return [['A','重试'],['L+R','连接'],['MENU','退出']];
  if(booting()||unavailable())return [['L+R','连接'],['MENU','退出']];
  if(!remoteReady())return [['A','说话']];
  if(codex()&&!control()?.codexAvailable)return [];
  return [['A',codex()&&!control()?.targetID?'选择任务':'说话'],...(!codex()&&canEdit()?[['B','删除'] as [string,string]]:[]),...(codex()&&canEdit()&&control()?.draftAvailable===true?[['X','发送'] as [string,string]]:[])];
 };
 const rawFooterRows=():[string,string][][]=>{
  const primary=primaryFooter();
  if(page()!=='main'||inputBusy()||booting()||current()==='error'||!remoteReady())return primary.length?[primary]:[];
  const secondary:[string,string][]=[];
  if(codex()&&(control()?.codexAvailable||control()?.approval))secondary.push(['Y','任务']);
  if(canEdit())secondary.push(['SE','编辑']);
  if((mainDictation()||mainReply())&&(mainRows().length>7||mainReply()&&reply()&&reply()!.pages>1))secondary.push(['L/R','阅读']);
  if(codex()||control()?.codexAvailable)secondary.push(['ST',codex()?'普通':'Codex']);
  return [[...primary,...secondary]];
 };
 const footerRows=createMemo<[string,string][][]>(previous=>{
  const rows=rawFooterRows();
  if(!busy()&&page()!=='connections'&&rows.length&&rows[0].length<6&&!rows[0].some(a=>a[0]==='L+R'))rows[0]=[...rows[0],['L+R','连接']];
  return reuseActionRows(rows,previous);
 },[]);
 function Label(p:{children:JSX.Element;size?:number;color?:string;width?:number}){const size=p.size||30;return <Text style={{fontSlot:size===54?23:size===40?22:21,textAlign:0,textColor:p.color||ink,width:p.width||912,height:size+20,lineHeight:size+16}}>{p.children}</Text>;}
 return <View class="relative w-full h-full overflow-hidden" style={{bgColor:bg}}>
  <View class="absolute left-[56] top-[32] flex-row items-center gap-[8]">
   <View class="relative w-[40] h-[40] overflow-hidden"><Image class="absolute left-[0] top-[0] w-[64] h-[64]" src={microphone}/></View>
   <Label size={40} width={652}>{title()}</Label>
  </View>
  <View class="absolute right-[56] top-[42] flex-row gap-[14] items-center"><View class="w-[9] h-[9] rounded-[5]" style={{bgColor:state().connected?accent:muted}}/><Label width={120} color={muted}>{(state().battery?.charging?'+ ':'')+(state().battery?.percent==null?'—%':state().battery?.percent+'%')}</Label></View>
  <Show when={context()||notice()&&page()!=='main'}><View class="absolute left-[56] top-[100]"><Label color={muted}>{notice()&&page()!=='main'?statusMessage(notice()):ellipsis(context(),29)}</Label></View></Show>
  <View class="absolute left-[56] top-[166] w-[912] h-[1]" style={{bgColor:line}}/>
  <Show when={page()==='main'&&!hasText()&&!mainReply()&&!mainDictation()}>
   <View class="absolute left-[56] top-[260]"><Label size={54}>{mainStatus()}</Label></View>
   <Show when={statusHint()}><View class="absolute left-[56] top-[360]"><Label color={muted}>{ellipsis(statusHint(),29)}</Label></View></Show>
   <Show when={current()==='recording'}><View class="absolute left-[56] top-[432] w-[912] h-[112] flex-row items-center justify-between"><For each={waveBars}>{i=><View class="w-[12] rounded-[6]" style={{height:waveform()[i],bgColor:accent}}/>}</For></View></Show>
   <Show when={booting()||current()==='processing'||pending()||control()?.focusPending}><View class="absolute left-[56] top-[464] w-[912] h-[5] overflow-hidden" style={{bgColor:line}}><View class="w-[140] h-[5]" style={{bgColor:accent,translateX:pulse()/120*1052-140}}/></View></Show>
  </Show>
  <Show when={mainReply()||mainDictation()}>
   <View class="absolute left-[56] top-[194] w-[912] h-[420] overflow-hidden flex-col"><For each={mainRows().slice(row(),row()+7)}>{text=><Text style={{fontSlot:20,textColor:ink,width:912,height:60,lineHeight:60}}>{text}</Text>}</For><Show when={mainReply()&&!reply()}><Label color={muted}>等待回复</Label></Show></View>
  </Show>
  <Show when={hasText()}>
   <View class="absolute left-[56] top-[190]"><Label color={muted}>{control()?.controlHint==='已发送'?'已发送':'最近口述'}</Label></View>
   <View class="absolute left-[56] top-[250] w-[912] h-[360] overflow-hidden flex-col"><For each={lines().slice(0,6)}>{text=><Text style={{fontSlot:20,textColor:ink,width:912,height:60,lineHeight:60}}>{text}</Text>}</For></View>
  </Show>
  <Show when={isList()}>
   <View class="absolute left-[56] top-[194] w-[912] flex-col gap-[4]">
    <For each={visibleEntries()}>{(entry,i)=><View class="relative h-[92] px-[16] rounded-[6]" style={{bgColor:i()+visibleStart()===index()?accent:bg}}>
     <View class={entry.detail?"absolute left-[16] top-[3]":"absolute left-[16] top-[16]"}><Label size={40} color={i()+visibleStart()===index()?selected:ink} width={entry.unread?760:880}>{ellipsis(entry.label,entry.unread?18:22)}</Label></View>
     <Show when={entry.unread}><View class="absolute right-[16] top-[23] h-[42] px-[12] rounded-[21]" style={{bgColor:i()+visibleStart()===index()?selected:accent}}><Text style={{fontSlot:21,textColor:i()+visibleStart()===index()?accent:selected,height:42,lineHeight:38}}>{entry.unread!>99?'99+':String(entry.unread)}</Text></View></Show>
     <Show when={entry.detail}><View class="absolute left-[16] top-[50]"><Label color={i()+visibleStart()===index()?selected:muted} width={880}>{ellipsis(entry.detail||'',28)}</Label></View></Show>
    </View>}</For>
    <Show when={!entries().length}><Label color={muted}>{page()==='connections'?'未发现电脑':'等待任务同步'}</Label></Show>
   </View>
  </Show>
  <Show when={isReading()}>
   <View class="absolute left-[56] top-[194] w-[912] h-[420] overflow-hidden flex-col"><For each={rows().slice(row(),row()+7)}>{text=><Text style={{fontSlot:20,textColor:ink,width:912,height:60,lineHeight:60}}>{text}</Text>}</For></View>
   <Show when={page()==='approval'}><View class="absolute left-[56] top-[626]"><Label color={muted}>{!control()?.approval?.allowAvailable?'需要 Mac 审核':reading().length>7?(row()+1)+'/'+reading().length:''}</Label></View></Show>
   <Show when={page()!=='approval'}><View class="absolute left-[56] top-[626]"><Label color={muted}>{row()+1+' / '+Math.max(1,rows().length)}</Label></View></Show>
  </Show>
  <View class="absolute left-[56] top-[686] w-[912] h-[1]" style={{bgColor:line}}/>
  <For each={footerRows()}>{(actions)=><View class="absolute left-[56] top-[704] w-[912] flex-row justify-start gap-[8]"><For each={actions}>{([key,label])=><View class="flex-row items-center gap-[8]"><View class="h-[42] px-[12] rounded-[21]" style={{bgColor:accent}}><Text style={{fontSlot:21,textColor:selected,height:42,lineHeight:38}}>{key}</Text></View><Text style={{fontSlot:21,textAlign:0,textColor:muted,height:50,lineHeight:46}}>{label}</Text></View>}</For></View>}</For>
 </View>;
}
mount(App);
