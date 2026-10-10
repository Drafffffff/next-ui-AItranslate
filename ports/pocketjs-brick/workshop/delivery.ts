import type {GameAudio} from './audio.ts';
/** Strip comments/strings before examining literal P8 audio calls. Syntax is
 * checked separately by z8lua; these diagnostics do not simulate gameplay. */
export function codeOnly(lua:string){
 const parts:string[]=[];let i=0,last=0;
 while(i<lua.length){
  const start=i,comment=lua.startsWith('--',i);if(comment)i+=2;
  const long=lua[i]==='['?/^\[(=*)\[/.exec(lua.slice(i,i+256)):null;
  if(long){const close=']'+long[1]+']',end=lua.indexOf(close,i+long[0].length);i=end<0?lua.length:end+close.length;}
  else if(comment){const end=lua.indexOf('\n',i);i=end<0?lua.length:end;}
  else if(lua[i]==='"'||lua[i]==="'"){const quote=lua[i++];while(i<lua.length){if(lua[i]==='\\'){i+=2;continue;}if(lua[i++]===quote)break;}}
  else{i++;continue;}
  parts.push(lua.slice(last,start),lua.slice(start,i).replace(/[^\n]/g,' '));last=i;
 }
 parts.push(lua.slice(last));return parts.join('');
}
export function deliveryDiagnostics(lua:string,audio:GameAudio){
 const code=codeOnly(lua),errors:string[]=[],warnings:string[]=[];let bgm_start=false,operation_sound=false;
 // Cart sandbox exposes P8 helpers, not the host Lua standard namespaces.
 // A user-defined object with the same name is legal; without a full scope
 // parser, report its calls for review instead of rejecting it as a library.
 for(const [name,replacement] of [['table','add/del/deli'],['math','flr/abs/min/max/sin/cos'],['string','sub/split/ord/chr']] as const){
  if(!new RegExp('(^|[^\\w.:])'+name+'\\s*\\.\\s*[A-Za-z_]\\w*\\s*\\(','m').test(code))continue;
  const bound=new RegExp('\\blocal\\s+'+name+'\\b|\\b'+name+'\\s*=|\\bfunction\\s+'+name+'\\s*[.:]|\\bfunction\\b[^\\n(]*\\([^)]*\\b'+name+'\\b').test(code);
  const message='fake-08 卡带不提供普通 Lua '+name+' 标准库；使用 P8 '+replacement+' 或自己的实现。';
  if(bound)warnings.push(message+'检测到同名自定义绑定，请核对作用域与初始化。');else errors.push(message);
 }
 // Limited static hint, not a full Lua scope/type analysis or runtime check.
 for(const declaration of code.matchAll(/(?:^|\n)[ \t]*function[ \t]+([A-Za-z_]\w*)[ \t]*\(/g)){
  const name=declaration[1];
  if(new RegExp('(^|\\n)[ \\t]*'+name+'[ \\t]*=[ \\t]*-?(?:[0-9]|\\.\\d)').test(code))warnings.push('全局 '+name+' 同时有数字赋值和函数声明；核对名称覆盖与实际使用类型，例如尺寸tile_size和查询tile_at应分开。这是静态提示，未执行游戏。');
 }
 const sounds=new Map(audio.sfx.map(s=>[s.id,s])),songs=new Map(audio.music.map(p=>[p.id,p]));
 const shadowed=/\bfunction\s+(?:sfx|music)\b|\b(?:sfx|music)\s*=/.test(code);
 if(shadowed)errors.push('声音接口被重定义，请保留原生 sfx/music 播放接口。');
 const dynamic=new Set<string>();for(const c of code.matchAll(/\b(sfx|music)\s*\(\s*([^,)]*)/g))if(!/^-?\d+$/.test(c[2].trim()))dynamic.add(c[1]);
 const seen=new Set<string>(),literalEffects=new Set<number>(),literalTracks=new Set<number>();
 for(const match of code.matchAll(/\b(sfx|music)\s*\(\s*(-?\d+)\s*(?=[,)])/g)){
  const name=match[1],id=Number(match[2]);if(id<0||shadowed)continue;
  if(name==='music'){bgm_start=true;literalTracks.add(id);}else{operation_sound=true;literalEffects.add(id);}
  const key=name+':'+id;if(seen.has(key))continue;seen.add(key);
  const sound=sounds.get(id),song=songs.get(id);
  if(name==='sfx'&&(!sound||!sound.notes.some(n=>n[2]>0))||name==='music'&&(!song||song.channels.every(c=>c<0||!sounds.get(c)?.notes.some(n=>n[2]>0))))errors.push(name+'('+id+') 引用空白或未提供的声音槽位。');
 }
 if(!bgm_start){if(dynamic.has('music'))warnings.push('BGM 使用动态编号，需核对初始化、槽位和循环。');else errors.push('缺少背景音乐播放调用；成品必须有 BGM。');}
 if(!operation_sound){if(dynamic.has('sfx'))warnings.push('音效使用动态编号，需核对操作事件和槽位。');else errors.push('缺少操作音效播放调用；成品必须有操作反馈。');}
 // Shared tracker resources are legal. Warn about long feedback, never infer
 // per-frame repetition or actual audio underruns from a static call alone.
 const musicSounds=new Set<number>();for(const id of literalTracks)for(const c of songs.get(id)?.channels||[])if(c>=0)musicSounds.add(c);
 for(const id of literalEffects){
  const sound=sounds.get(id);if(!sound||!musicSounds.has(id))continue;
  const rows=sound.loop_end>sound.loop_start?sound.loop_end:sound.loop_start>0?sound.loop_start:sound.notes.reduce((last,n,i)=>n[2]>0?i+1:last,0);
  const duration=rows*sound.speed*183/22050;
  if(duration>=.3)warnings.push('SFX '+id+' 同时用于 BGM 和音效，约'+duration.toFixed(2)+'秒；核对事件触发节拍和独立短音效槽，避免频繁重播截断声音。这是源码提示，未测量欠载。');
 }
 if(/\bcamera\s*\(\s*\)/.test(code))warnings.push('核对 camera() 重置位置：世界实体与地图应使用同一镜头，HUD 再恢复屏幕坐标。');
 return {errors,warnings,bgm_start,operation_sound,gameplay_tested:false};
}
