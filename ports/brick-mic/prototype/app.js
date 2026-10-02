'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const tasks = [
    {id:'demo-translate',name:'优化翻译画面的阅读体验',status:'待输入',text:'',messages:[],unread:0},
    {id:'demo-mic',name:'完善 Brick Mic 体验',status:'待输入',text:'',messages:[],unread:0},
    {id:'demo-docs',name:'整理项目安装文档',status:'待输入',text:'',messages:[],unread:0}
  ];
  const labels = {completed:'本轮回复完成',waiting:'需要你回应',failed:'执行失败'};
  const menu = ['撤销','重做','复制','粘贴','全选','插入换行','阅读本次识别'];
  const state = {mode:'ordinary',page:'main',task:null,lastTask:null,index:0,menuIndex:0,
    connected:true,codex:true,sleep:false,exited:false,recording:false,processing:false,opening:false,
    shift:false,battery:95,charge:'discharging',activeApp:'notes',macTask:tasks[0].id,
    lastText:'',note:'',noteError:false,notes:'',session:0,queue:[],clipboard:'',
    recordingAt:0,recordingTarget:null,pendingVibrate:null};
  const histories = new Map();
  const editor = $('mac-editor');
  let cursor={anchor:0,caret:0,column:null},heldTimer=0,repeatTimer=0,holdName=null,recordTicker=0,noteTimer=0;
  let hapticTimer=0,openingToken=0,shiftDown=false;
  let lastBattery={percent:state.battery,charge:state.charge};
  const escape = text => String(text).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const task = id => tasks.find(t=>t.id===id);
  const totalUnread = () => tasks.reduce((sum,t)=>sum+t.unread,0);
  const busy = () => state.recording||state.processing||state.opening;
  const targetId = () => state.mode==='codex' ? state.task : state.activeApp==='notes' ? 'notes' : state.macTask;
  const buffer = id => id==='notes' ? state.notes : task(id)?.text || '';
  const setBuffer = (id,value) => {if(id==='notes')state.notes=value;else if(task(id))task(id).text=value;};
  const shownId = () => state.activeApp==='notes' ? 'notes' : state.macTask;
  function log(text){$('last-action').textContent=text;}
  function note(text,error=false,duration=2600){state.note=text;state.noteError=error;clearTimeout(noteTimer);renderBrick();if(duration)noteTimer=setTimeout(()=>{state.note='';state.noteError=false;renderBrick();},duration);}
  function saveHistory(id){let h=histories.get(id);if(!h){h={undo:[],redo:[]};histories.set(id,h);}h.undo.push(buffer(id));if(h.undo.length>40)h.undo.shift();h.redo=[];}
  function syncCursor(){cursor.caret=editor.selectionDirection==='backward'?editor.selectionStart:editor.selectionEnd;cursor.anchor=editor.selectionDirection==='backward'?editor.selectionEnd:editor.selectionStart;cursor.column=null;renderSelection();}
  function renderSelection(){const a=editor.selectionStart,b=editor.selectionEnd;$('selection-status').textContent=a===b?`光标 ${[...editor.value.slice(0,a)].length}`:`已选择 ${[...editor.value.slice(a,b)].length} 个字符`;}
  function renderMac(focus=false){
    const id=shownId(),t=task(id);
    $('mac-app-title').textContent=state.activeApp==='notes'?'备忘录':'Codex';
    $('notes-tab').classList.toggle('active',state.activeApp==='notes');
    $('codex-tab').classList.toggle('active',state.activeApp==='codex');
    $('codex-tab').disabled=!state.codex;
    $('editor-eyebrow').textContent=state.activeApp==='notes'?'普通输入目标':'Codex 任务';
    $('editor-title').textContent=state.activeApp==='notes'?'随手记':t.name;
    $('editor-subtitle').textContent=state.activeApp==='notes'?'普通模式下，由你选择 Mac 的输入框。':'当前任务里的文字留在 Mac，按掌机 X 才发送。';
    if(editor.value!==buffer(id)){editor.value=buffer(id);cursor={anchor:editor.value.length,caret:editor.value.length,column:null};editor.setSelectionRange(cursor.caret,cursor.caret);}
    $('editor-hint').textContent=state.activeApp==='notes'?'文字会填入，不会自动发送':`任务状态：${t.status} · 未发送文字保留在输入框`;
    $('mac-send').hidden=state.activeApp!=='codex';
    $('messages').innerHTML=(t?.messages||[]).slice(-3).reverse().map(m=>`<div class="message"><small>${escape(m.kind)}</small>${escape(m.text)}</div>`).join('');
    if(focus)editor.focus({preventScroll:true});
    renderSelection();
    $('focus-status').textContent=state.activeApp==='codex'?'任务输入框已定位':'已选择输入框';
  }
  function footer(items){return items.map(([key,label])=>`<span><kbd>${key}</kbd>${label}</span>`).join('');}
  function renderBrick(){
    $('mode').textContent=state.mode==='ordinary'?'普通输入':'Codex';
    $('target').textContent=state.mode==='ordinary'?(state.activeApp==='notes'?'备忘录':'当前 Mac 窗口：Codex'):(task(state.task)?.name||'请选择任务');
    $('connection').textContent=state.connected?'● 已连接':'○ 已断开';
    const unknown=state.charge==='unknown',battery=unknown?'—%':`${state.battery}%`;
    $('battery-text').textContent=(state.charge==='charging'?'ϟ ':state.charge==='full'?'✓ ':'')+battery;
    $('battery-fill').style.width=unknown?'0%':`${state.battery}%`;
    document.querySelector('.battery-wrap').classList.toggle('low',!unknown&&state.battery<=20);
    document.querySelector('.battery-wrap').setAttribute('aria-label',unknown?'掌机电量未知':`掌机电量 ${state.battery}%，${state.charge==='charging'?'充电中':state.charge==='full'?'已充满':'放电'}`);
    if(state.connected)lastBattery={percent:state.battery,charge:state.charge};
    const macBattery=lastBattery.charge==='unknown'?'—%':`${lastBattery.percent}%`;
    $('mac-device-battery').textContent=`Brick · ${state.connected?'':'上次 '}${macBattery}${lastBattery.charge==='charging'?' · 充电中':lastBattery.charge==='full'?' · 已充满':''}`;
    $('battery-output').textContent=`${state.battery}%`;
    $('unread').hidden=!state.codex||totalUnread()===0;
    $('unread').textContent=`${totalUnread()} 未读`;
    $('sleep-cover').hidden=!state.sleep&&!state.exited;
    $('sleep-title').textContent=state.exited?'应用已退出':'已息屏';
    $('sleep-note').textContent=state.exited?'点「重开应用」继续':!state.connected?'蓝牙断开 · Mac 暂存消息':totalUnread()?`${totalUnread()} 条未读 · 短按电源查看`:'蓝牙保持连接';
    $('power').textContent=state.sleep?'电源 · 唤醒':'电源 · 息屏';
    $('screen-note').textContent=state.page==='main'?state.note:'';
    $('screen-note').classList.toggle('error',state.noteError);
    document.querySelectorAll('[data-action=L],[data-action=R]').forEach(b=>b.setAttribute('aria-pressed',String(state.shift||shiftDown)));
    const c=$('content');
    if(state.page==='tasks'){
      c.innerHTML=`<h2>选择任务</h2><p class="task-description">选好后自动打开 Mac 对应输入框</p><div class="task-list">${tasks.map((t,i)=>`<button class="task-row ${i===state.index?'selected':''}" data-task="${i}"><span>${escape(t.name)}</span><small>${t.unread?`${t.unread} 未读`:escape(t.status)}</small></button>`).join('')}</div>`;
      $('footer').innerHTML=footer([['A','选择'],['B','返回'],['START','普通输入']]);
    }else if(state.page==='menu'){
      c.innerHTML=`<h2>编辑 Mac 文本</h2><div class="edit-menu">${menu.map((name,i)=>`<button class="task-row ${i===state.menuIndex?'selected':''}" data-menu="${i}">${name}</button>`).join('')}</div>`;
      $('footer').innerHTML=footer([['A','执行'],['B','返回'],['START',state.mode==='codex'?'普通输入':'Codex']]);
    }else if(state.page==='reading'){
      c.innerHTML=`<h2>本次识别</h2><div class="reading-text">${escape(state.lastText||'还没有识别文字。')}</div>`;
      $('footer').innerHTML=footer([['↑↓','阅读'],['B','返回']]);
    }else{
      let title='按住 A 说话',hint=state.mode==='codex'?'自动定位任务，松开后填入文字':'在 Mac 选择输入框，即可开始';
      if(!state.connected){title='等待 Mac 连接';hint='已识别文字与任务内容会保留';}
      else if(state.mode==='codex'&&!state.codex){title='Codex 已关闭';hint='按 START 返回普通输入';}
      else if(state.opening){title='正在打开任务';hint='定位 Mac 的任务输入框';}
      else if(state.recording){title='正在录音';hint='松开 A 结束，B 取消';}
      else if(state.processing){title='正在识别';hint='完成后填入，不会自动发送';}
      else if(state.lastText){title='已填入';hint=state.mode==='codex'?'可以编辑，按 X 发送':'继续说话，或用十字键编辑';}
      const wave=Array.from({length:24},(_,i)=>`<i style="--height:${3+(i*17%11)}cqw;animation-delay:${(i%6)*-.12}s"></i>`).join('');
      c.innerHTML=`<h2>${state.opening||state.processing?'<span class="spinner"></span>':''}${title}</h2><p>${hint}</p>${state.recording||!state.lastText?`<div class="wave ${state.recording?'recording':''}">${wave}</div>${state.recording?'<div class="duration" id="duration">00:00</div>':''}`:`<div class="transcript">${escape(state.lastText)}</div>`}`;
      const items=[['A','按住说话'],['B',busy()?'取消':'删除']];
      if(state.mode==='codex'&&state.codex){items.push(['X','发送'],['Y','任务'],['START','普通输入']);}
      else if(state.mode==='codex'){items.push(['START','普通输入']);}
      else if(state.codex){items.push(['START','Codex']);}
      items.push(['SELECT','更多']);
      $('footer').innerHTML=footer(items);
    }
    const controlsDisabled=state.exited||state.sleep||!state.connected;
    document.querySelector('[data-action=X]').disabled=controlsDisabled||state.mode!=='codex'||!state.codex||state.page!=='main'||busy();
    document.querySelector('[data-action=Y]').disabled=controlsDisabled||state.mode!=='codex'||!state.codex||busy();
    $('queue-status').textContent=state.queue.length?`Mac 暂存 ${state.queue.length} 条通知，重连后同步`:totalUnread()?`${totalUnread()} 条未读，通知不会切换输入模式`:'通知也可在息屏时接收';
    c.querySelectorAll('[data-task]').forEach(b=>b.addEventListener('click',()=>chooseTask(Number(b.dataset.task))));
    c.querySelectorAll('[data-menu]').forEach(b=>b.addEventListener('click',()=>editMenu(Number(b.dataset.menu))));
  }
  function render(){renderMac();renderBrick();}
  function focusTarget(){
    if(!state.connected){note('蓝牙已断开，暂时不能控制 Mac',true);return null;}
    if(state.mode==='codex'){
      if(!state.codex||!task(state.task)){note('请先选择可用的 Codex 任务',true);return null;}
      if(state.activeApp!=='codex'||state.macTask!==state.task)log(`自动打开「${task(state.task).name}」并聚焦输入框`);
      state.activeApp='codex';state.macTask=state.task;renderMac(true);return state.task;
    }
    renderMac(true);return shownId();
  }
  function showApp(app){
    if(app==='codex'&&!state.codex)return;
    state.activeApp=app;renderMac(true);renderBrick();log(`你在 Mac 选择了${app==='notes'?'备忘录':'Codex'}输入框`);
  }
  function chooseTask(index){
    if(!state.codex||!state.connected)return;
    state.index=index;state.page='main';state.opening=true;state.lastText='';
    const mine=++openingToken;renderBrick();
    setTimeout(()=>{if(mine!==openingToken)return;state.opening=false;state.mode='codex';state.task=tasks[index].id;state.lastTask=state.task;tasks[index].unread=0;state.activeApp='codex';state.macTask=state.task;render();editor.focus({preventScroll:true});note('Mac 任务输入框已定位');log(`打开「${tasks[index].name}」· 聚焦输入框 · 没有发送文字`);},260);
  }
  function switchMode(){
    if(state.recording||state.processing||state.opening){note('本次输入完成后可切换；B 可取消');return;}
    stopHold();state.shift=false;state.lastText='';
    if(state.mode==='codex'||state.page==='tasks'&&!state.task){state.mode='ordinary';state.task=null;state.page='main';note('已返回普通输入，请在 Mac 选择输入框');log('返回普通输入 · 解除任务绑定 · 未发送内容保留');}
    else if(state.codex){if(state.lastTask)chooseTask(tasks.findIndex(t=>t.id===state.lastTask));else{state.page='tasks';state.index=0;renderBrick();log('首次进入 Codex · 选择任务');}}
    else note('Codex 未打开，继续使用普通输入');
    renderBrick();
  }
  function startRecording(){
    if(state.page==='tasks'){chooseTask(state.index);return;}
    if(state.page==='menu'){editMenu(state.menuIndex);return;}
    if(state.page!=='main'||state.exited||state.sleep||state.processing||state.opening)return;
    const id=focusTarget();if(!id)return;
    state.recording=true;state.lastText='';state.recordingAt=Date.now();state.recordingTarget={id,mode:state.mode,session:++state.session};state.note='';renderBrick();log(`录音开始 · 目标：${id==='notes'?'备忘录':task(id).name}`);
    clearInterval(recordTicker);recordTicker=setInterval(()=>{const el=$('duration');if(el){const s=Math.floor((Date.now()-state.recordingAt)/1000);el.textContent=`${String(Math.floor(s/60)).padStart(2,'0')}:${String(s%60).padStart(2,'0')}`;}},200);
  }
  function stopRecording(){
    if(!state.recording)return;clearInterval(recordTicker);state.recording=false;state.processing=true;const target=state.recordingTarget,text=$('speech').value;renderBrick();
    setTimeout(()=>{
      if(target.session!==state.session)return;state.processing=false;
      if(!state.connected||state.sleep||state.exited){note('本次输入已取消',true);return;}
      if(target.mode==='ordinary'&&shownId()!==target.id){state.lastText=text;note('Mac 输入目标已变化，文字未填入',true,0);log('普通输入：目标变化，保留识别结果');renderBrick();return;}
      if(target.mode==='codex'&&!state.codex){state.lastText=text;note('Codex 已关闭，文字未填入',true,0);renderBrick();return;}
      const id=focusTarget();if(id!==target.id){note('目标变化，未填入文字',true);return;}
      replaceSelection(id,text);state.lastText=text;note(target.mode==='codex'?'已填入，按 X 才发送':'已输入到当前 Mac 输入框');log(`${target.mode==='codex'?'Codex 定向':'普通'}输入完成 · 只填入，未发送`);renderBrick();
      if(state.pendingVibrate){const kind=state.pendingVibrate;state.pendingVibrate=null;feedback(kind);}
    },320);
  }
  function cancel(){
    if(busy()){state.session++;openingToken++;state.recording=false;state.processing=false;state.opening=false;clearInterval(recordTicker);note('本次操作已取消');log('取消本次输入 · 不发送');}
    else if(state.page!=='main'){state.page='main';renderBrick();}
    else deleteText();
  }
  function replaceSelection(id,text){
    saveHistory(id);const value=buffer(id),a=editor.selectionStart,b=editor.selectionEnd;setBuffer(id,value.slice(0,a)+text+value.slice(b));editor.value=buffer(id);cursor={anchor:a+text.length,caret:a+text.length,column:null};editor.focus({preventScroll:true});editor.setSelectionRange(cursor.caret,cursor.caret);renderSelection();
  }
  const segmenter=new Intl.Segmenter('zh',{granularity:'grapheme'});
  function boundaries(value){return [...segmenter.segment(value)].map(s=>s.index).concat(value.length);}
  function visualRows(value){
    const style=getComputedStyle(editor),mirror=document.createElement('div');
    Object.assign(mirror.style,{position:'absolute',left:'-10000px',top:'0',visibility:'hidden',
      width:`${editor.clientWidth-parseFloat(style.paddingLeft)-parseFloat(style.paddingRight)}px`,
      whiteSpace:'pre-wrap',overflowWrap:'break-word',fontFamily:style.fontFamily,fontSize:style.fontSize,
      fontWeight:style.fontWeight,lineHeight:style.lineHeight,letterSpacing:style.letterSpacing});
    mirror.setAttribute('aria-hidden','true');
    const parts=[...segmenter.segment(value)].map(s=>({text:s.segment,index:s.index}));
    parts.push({text:'\u200b',index:value.length});
    for(const part of parts){const span=document.createElement('span');span.textContent=part.text;mirror.append(span);}
    document.body.append(mirror);const base=mirror.getBoundingClientRect(),rows=[];
    [...mirror.children].forEach((span,i)=>{const rect=span.getBoundingClientRect(),part=parts[i];
      let row=rows.find(r=>Math.abs(r.top-rect.top)<1);if(!row){row={top:rect.top,points:[]};rows.push(row);}
      row.points.push({index:part.index,x:rect.left-base.left});
      if(part.text!=='\n'&&i<parts.length-1)row.points.push({index:part.index+part.text.length,x:rect.right-base.left});
    });mirror.remove();return rows.sort((a,b)=>a.top-b.top);
  }
  function moveCaret(direction,selecting){
    const id=focusTarget();if(!id)return;
    const value=editor.value,points=boundaries(value),a=editor.selectionStart,b=editor.selectionEnd;
    if(a===b){cursor.anchor=a;cursor.caret=a;}
    let next=cursor.caret;
    if(!selecting&&a!==b&&(direction==='left'||direction==='right'))next=direction==='left'?a:b;
    else if(direction==='left')next=points.filter(n=>n<cursor.caret).at(-1)??0;
    else if(direction==='right')next=points.find(n=>n>cursor.caret)??value.length;
    else{
      const rows=visualRows(value);let rowIndex=0;
      rows.forEach((row,i)=>{if(Math.min(...row.points.map(p=>p.index))<=cursor.caret)rowIndex=i;});
      const row=rows[rowIndex],current=row.points.find(p=>p.index===cursor.caret)||row.points.at(-1);
      if(cursor.column===null)cursor.column=current.x;
      const targetRow=rows[Math.max(0,Math.min(rows.length-1,rowIndex+(direction==='up'?-1:1)))];
      next=targetRow.points.reduce((best,p)=>Math.abs(p.x-cursor.column)<Math.abs(best.x-cursor.column)?p:best).index;
    }
    if(direction==='left'||direction==='right')cursor.column=null;
    if(!selecting)cursor.anchor=next;cursor.caret=next;
    editor.focus({preventScroll:true});editor.setSelectionRange(Math.min(cursor.anchor,next),Math.max(cursor.anchor,next),next<cursor.anchor?'backward':'forward');renderSelection();
    log(`${selecting?'选择文本':'移动 Mac 光标'} · ${direction} · ${id==='notes'?'备忘录':task(id).name}`);
  }
  function deleteText(){
    const id=focusTarget();if(!id)return;let a=editor.selectionStart,b=editor.selectionEnd;
    if(a===b){if(a===0)return;a=boundaries(editor.value).filter(n=>n<a).at(-1)??0;}
    saveHistory(id);setBuffer(id,editor.value.slice(0,a)+editor.value.slice(b));editor.value=buffer(id);cursor={anchor:a,caret:a,column:null};editor.setSelectionRange(a,a);renderSelection();log('B 删除 Mac 输入框文字 · 可以撤销');
  }
  function direction(name,event){
    if(state.sleep||state.exited||busy())return;
    if(state.page==='tasks'){state.index=(state.index+(name==='up'||name==='left'?-1:1)+tasks.length)%tasks.length;renderBrick();}
    else if(state.page==='menu'){const step=name==='up'?-2:name==='down'?2:name==='left'?-1:1;state.menuIndex=(state.menuIndex+step+menu.length)%menu.length;renderBrick();}
    else if(state.page==='reading'){$('content').querySelector('.reading-text').scrollBy({top:name==='up'?-45:45});}
    else moveCaret(name,state.shift||shiftDown||event?.shiftKey);
  }
  function editMenu(index){
    const name=menu[index];state.page='main';
    if(name==='阅读本次识别'){state.page='reading';renderBrick();return;}
    const id=focusTarget();if(!id){renderBrick();return;}
    if(name==='全选'){cursor={anchor:0,caret:editor.value.length,column:null};editor.setSelectionRange(0,editor.value.length);renderSelection();}
    else if(name==='复制'){state.clipboard=editor.value.slice(editor.selectionStart,editor.selectionEnd);note('已复制选区（模拟剪贴板）');}
    else if(name==='粘贴'){replaceSelection(id,state.clipboard);}
    else if(name==='插入换行'){replaceSelection(id,'\n');}
    else {const h=histories.get(id),from=name==='撤销'?h?.undo:h?.redo,to=name==='撤销'?h?.redo:h?.undo;if(from?.length){to.push(buffer(id));setBuffer(id,from.pop());renderMac(true);editor.setSelectionRange(editor.value.length,editor.value.length);syncCursor();}else note(`没有可${name}的内容`);}
    log(`编辑 Mac 文本 · ${name}`);renderBrick();
  }
  function submit(fromMac=false){
    if(busy()){note('识别完成后才能发送');return;}
    if(!fromMac&&(state.mode!=='codex'||state.page!=='main'))return;
    const id=fromMac?shownId():focusTarget(),t=task(id);if(!t)return;
    if(!t.text.trim()){note('输入框为空，没有发送');return;}
    t.messages.push({kind:'你 · 已发送',text:t.text});t.text='';t.status='处理中';state.lastText='';state.note='';renderMac(true);renderBrick();note('已发送到选定任务');log(`X 提交「${t.name}」· 文本已发送一次`);
  }
  function feedback(kind){
    const led=$('led');led.style.background=kind==='failed'?'var(--danger)':kind==='waiting'?'var(--alert)':'var(--accent)';led.classList.remove('flash');void led.offsetWidth;led.classList.add('flash');
    if(state.recording){state.pendingVibrate=kind;return;}
    $('device').classList.remove('vibrate');void $('device').offsetWidth;$('device').classList.add('vibrate');$('haptic').textContent=kind==='completed'?'短震 × 2':'短震 × 1';$('haptic').classList.add('alert');clearTimeout(hapticTimer);hapticTimer=setTimeout(()=>{$('haptic').textContent='TRIMUI BRICK';$('haptic').classList.remove('alert');$('device').classList.remove('vibrate');},2500);
  }
  function receive(event,alert=true){const t=task(event.id);t.status=labels[event.kind];t.unread++;if(alert)feedback(event.kind);render();log(`收到「${t.name}」通知 · ${labels[event.kind]} · ${state.sleep?'息屏保持':'未改变'}输入模式`);if(!state.sleep)note(`${t.name} · ${labels[event.kind]}`);}
  function emitEvent(){if(!state.codex){log('Codex 未运行，不能产生新的任务事件');return;}const event={id:$('event-task').value,kind:$('event-kind').value};if(!state.connected||state.exited){state.queue.push(event);renderBrick();log('连接不可用 · 消息暂存在模拟 Mac');}else receive(event);}
  function action(name){
    if(name==='POWER'){if(state.exited)return;if(state.recording||state.processing){state.session++;state.recording=state.processing=false;clearInterval(recordTicker);}stopHold();state.shift=false;state.sleep=!state.sleep;renderBrick();log(state.sleep?'息屏待机 · 麦克风关闭 · 蓝牙保持':'快速唤醒 · 保留输入模式与任务');return;}
    if(name==='MENU'){state.session++;state.recording=state.processing=false;openingToken++;state.opening=false;clearInterval(recordTicker);stopHold();state.exited=true;state.shift=false;renderBrick();log('退出 Brick Mic · 未发送内容保留在模拟 Mac');return;}
    if(state.sleep||state.exited)return;
    if(name==='START'){switchMode();return;}
    if(!state.connected){note('请先恢复蓝牙连接',true);return;}
    if(name==='X'){submit();return;}
    if(name==='Y'){if(busy())return;if(state.mode==='codex'&&state.codex){state.page=state.page==='tasks'?'main':'tasks';state.index=Math.max(0,tasks.findIndex(t=>t.id===state.task));renderBrick();}return;}
    if(name==='SELECT'){if(busy())return;state.page=state.page==='menu'?'main':'menu';renderBrick();return;}
    if(name==='L'||name==='R'){state.shift=!state.shift;renderBrick();note(state.shift?'选字已锁定；再次点肩键解除':'已解除选字');}
  }
  function stopHold(){clearTimeout(heldTimer);clearTimeout(repeatTimer);holdName=null;document.querySelectorAll('.pressed').forEach(b=>b.classList.remove('pressed'));}
  function hold(name,event){
    if(state.exited||state.sleep)return;
    stopHold();holdName=name;
    if(name==='A'){startRecording();return;}
    if(name==='B'){if(busy()||state.page!=='main'){cancel();return;}deleteText();}
    else direction(name,event);
    const started=Date.now();function repeat(){if(holdName!==name)return;if(name==='B')deleteText();else direction(name,event);repeatTimer=setTimeout(repeat,name==='B'?100:Date.now()-started>1200?45:90);}
    heldTimer=setTimeout(repeat,name==='B'?400:350);
  }
  document.querySelectorAll('[data-action]').forEach(b=>{b.addEventListener('pointerdown',e=>e.preventDefault());b.addEventListener('click',()=>action(b.dataset.action));});
  document.querySelectorAll('[data-hold]').forEach(b=>{
    b.addEventListener('pointerdown',e=>{e.preventDefault();b.setPointerCapture(e.pointerId);hold(b.dataset.hold,e);b.classList.add('pressed');});
    b.addEventListener('pointerup',()=>{if(b.dataset.hold==='A')stopRecording();stopHold();});
    b.addEventListener('pointercancel',()=>{if(b.dataset.hold==='A')cancel();stopHold();});
    b.addEventListener('click',e=>{if(e.detail===0){hold(b.dataset.hold);if(b.dataset.hold==='A')setTimeout(stopRecording,350);stopHold();}});
  });
  window.addEventListener('blur',()=>{stopHold();shiftDown=false;if(state.recording)cancel();renderBrick();});
  document.addEventListener('keydown',e=>{
    if(e.target.matches('textarea,input,select'))return;
    if(e.key==='Shift'){shiftDown=true;renderBrick();return;}
    const map={ArrowUp:'up',ArrowDown:'down',ArrowLeft:'left',ArrowRight:'right',' ':'A',Backspace:'B',Enter:'X',s:'START',t:'Y',e:'SELECT'};
    const name=map[e.key];if(!name)return;e.preventDefault();if(e.repeat)return;
    if(['up','down','left','right','A','B'].includes(name))hold(name,e);else action(name);
  });
  document.addEventListener('keyup',e=>{if(e.key==='Shift'){shiftDown=false;renderBrick();}if(e.key===' '&&holdName==='A')stopRecording();if(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight',' ','Backspace'].includes(e.key))stopHold();});
  editor.addEventListener('beforeinput',()=>saveHistory(shownId()));
  editor.addEventListener('input',()=>{setBuffer(shownId(),editor.value);syncCursor();});
  ['keyup','pointerup'].forEach(type=>editor.addEventListener(type,syncCursor));
  editor.addEventListener('select',()=>{cursor.caret=editor.selectionDirection==='backward'?editor.selectionStart:editor.selectionEnd;cursor.anchor=editor.selectionDirection==='backward'?editor.selectionEnd:editor.selectionStart;renderSelection();});
  editor.addEventListener('focus',()=>{$('focus-status').textContent='已选择输入框';});
  $('notes-tab').addEventListener('click',()=>showApp('notes'));
  $('codex-tab').addEventListener('click',()=>showApp('codex'));
  $('mac-send').addEventListener('click',()=>submit(true));
  $('unread').addEventListener('click',()=>{if(state.sleep||state.exited||busy())return;state.page='tasks';state.index=Math.max(0,tasks.findIndex(t=>t.unread));renderBrick();});
  $('theme').addEventListener('click',()=>{document.body.classList.toggle('dark');$('theme').textContent=document.body.classList.contains('dark')?'切换浅色':'切换深色';});
  $('battery').addEventListener('input',e=>{state.battery=Number(e.target.value);renderBrick();});
  $('charge').addEventListener('change',e=>{state.charge=e.target.value;renderBrick();});
  $('codex-running').addEventListener('change',e=>{
    state.codex=e.target.checked;
    if(!state.codex){if(state.mode==='codex'||state.opening){state.session++;state.recording=state.processing=state.opening=false;openingToken++;clearInterval(recordTicker);stopHold();}state.page='main';state.shift=false;if(state.activeApp==='codex')state.activeApp='notes';note(state.mode==='codex'?'Codex 已关闭，START 返回普通输入':'Codex 入口已隐藏',state.mode==='codex',0);}
    else note('Codex 可用，START 可以切换');render();log(state.codex?'检测到 Codex · 未自动切换模式':'检测到 Codex 退出 · 停止任务控制');
  });
  $('connected').addEventListener('change',e=>{
    state.connected=e.target.checked;
    if(!state.connected){state.session++;openingToken++;state.recording=state.processing=state.opening=false;clearInterval(recordTicker);stopHold();state.shift=false;note('蓝牙连接已断开',true,0);}
    else{note('连接已恢复');if(state.queue.length){const events=state.queue.splice(0);events.forEach(event=>receive(event,false));feedback('completed');log(`重连补收 ${events.length} 条消息 · 只汇总提醒一次`);}}render();
  });
  $('restart').addEventListener('click',()=>{state.exited=false;state.sleep=false;state.page='main';state.lastText='';state.note='';if(!state.codex){state.mode='ordinary';state.task=null;}render();log('重开应用 · 保留模式与 Mac 未发送文字 · 没有自动发送');});
  $('event-task').innerHTML=tasks.map(t=>`<option value="${t.id}">${escape(t.name)}</option>`).join('');
  $('notify').addEventListener('click',emitEvent);
  render();editor.focus({preventScroll:true});
})();
