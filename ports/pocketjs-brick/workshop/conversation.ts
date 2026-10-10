import type {GameMessage} from './agent.ts';
export interface Block {id:string;kind:'user'|'assistant'|'tool';heading:string;text:string}
export const TOOL_LABELS:Record<string,string>={terminal:'运行辅助脚本',ask_user_question:'询问你的想法',read_game:'读取当前游戏',read_skill:'加载开发参考',write_game:'写入游戏草稿',check_game:'检查卡带格式',publish_game:'发布游戏版本'};
const MODULE_LABELS:Record<string,string>={'gameplay-design':'玩法共创',api:'API参考','game-loop':'玩法与修复','3d':'三维开发','pixel-art':'像素绘制',music:'音乐与音效',community:'社区来源','example/collect':'收集示例','example/dodge':'躲避示例','example/snake':'贪吃蛇示例','example/platform':'平台示例','example/sokoban':'推箱子示例','example/raycaster':'第一人称示例','example/mesh3d':'三维线框示例'};
export function messageText(content:unknown):string{
 if(typeof content==='string')return content;
 return Array.isArray(content)?content.filter(c=>c.type==='text').map(c=>c.text||'').join('\n'):'';
}
const text=messageText;
export function toolText(call:any,result:any,expanded=false):string{
 const args=call.arguments||{};let detail='';
 if(call.name==='ask_user_question')detail=String(args.question||'');
 if(call.name==='read_skill')detail=MODULE_LABELS[String(args.module)]||String(args.module||'');
 if(call.name==='write_game')detail=args.edits?'局部修改 · '+args.edits.length+' 处':args.lua!==undefined?String(args.title||'游戏')+' · '+String(args.lua).split('\n').length+' 行代码':'更新信息与素材';
 if(!result)return expanded?JSON.stringify(args,null,2):detail;
 const raw=text(result.content);
 if(result.isError&&!expanded){const prefix=raw.slice(0,600).split(/\n\nReceived arguments:/)[0].trim();return prefix+(raw.length>prefix.length?'\n完整错误见 SE 详情':'');}
 let expandedWrite:string|undefined;
 try{const value=JSON.parse(raw);
  if(call.name==='ask_user_question')detail=String(args.question||'')+'\n'+(value.skipped?'你跳过了这个问题':'你的回答：'+String(value.answer||''));
  else if(call.name==='terminal')detail=value.stdout?String(value.stdout).slice(0,220):'脚本完成 · 临时工作区';
  else if(call.name==='read_game')detail=String(value.title||'当前游戏')+' · 第 '+(value.revision??0)+' 版';
  else if(call.name==='write_game')detail=String(value.title||args.title||'游戏')+' · '+(value.mode==='edit'?'局部修改 '+value.edit_count+' 处':value.mode==='metadata-assets'?'更新信息与素材':value.line_count+' 行代码');
  else if(call.name==='check_game')detail=value.format_checked?'卡带格式通过':'格式检查未通过';
  else if(call.name==='publish_game')detail='第 '+value.revision+' 版 · 已加入 AI生成游戏';
  if(result.isError)detail=raw;
  if(expanded&&call.name==='write_game'&&!result.isError){const {diff,...metadata}=value;expandedWrite=JSON.stringify(metadata,null,2)+(diff?'\n\n代码差异\n'+diff:'')+'\n\n参数\n'+JSON.stringify(args,null,2);}
 }catch{detail=raw;}
 return expanded?expandedWrite??(call.name==='write_game'?raw+'\n'+JSON.stringify(args,null,2):JSON.stringify(args,null,2)+'\n'+raw):detail;
}
export function conversation(messages:GameMessage[],active:Set<string>,expanded=false):Block[]{
 const results=new Map(messages.filter(m=>m.role==='toolResult').map(m=>[m.toolCallId,m]));
 const blocks:Block[]=[];
 messages.forEach((m,index)=>{
  if(m.role==='user'){blocks.push({id:'user-'+index,kind:'user',heading:'你',text:text(m.content)});return;}
  if(m.role!=='assistant')return;
  const body=text(m.content);
  if(body)blocks.push({id:'assistant-'+index,kind:'assistant',heading:'Agent',text:body});
  for(const call of m.content.filter(c=>c.type==='toolCall')){
   const result=results.get(call.id),state=result?(result.isError?'失败':'完成'):active.has(call.id)?'进行中':'未完成';
   let detail='',expandedWrite:string|undefined;const args=call.arguments||{};
   if(call.name==='ask_user_question')detail=String(args.question||'');
 if(call.name==='read_skill')detail=MODULE_LABELS[String(args.module)]||String(args.module||'');
   if(call.name==='write_game')detail=args.edits?'局部修改 · '+args.edits.length+' 处':args.lua!==undefined?String(args.title||'游戏')+' · '+String(args.lua).split('\n').length+' 行代码':'更新信息与素材';
   if(result){
    const raw=text(result.content);try{const value=JSON.parse(raw);
     if(call.name==='ask_user_question')detail=String(args.question||'')+'\n'+(value.skipped?'你跳过了这个问题':'你的回答：'+String(value.answer||''));
  else if(call.name==='terminal')detail=value.stdout?String(value.stdout).slice(0,220):'脚本完成 · 临时工作区';
  else if(call.name==='read_game')detail=String(value.title||'当前游戏')+' · 第 '+(value.revision??0)+' 版';
     else if(call.name==='write_game')detail=String(value.title||args.title||'游戏')+' · '+(value.mode==='edit'?'局部修改 '+value.edit_count+' 处':value.mode==='metadata-assets'?'更新信息与素材':value.line_count+' 行代码');
     else if(call.name==='check_game')detail=value.format_checked?'卡带格式通过':'格式检查未通过';
     else if(call.name==='publish_game')detail='第 '+value.revision+' 版 · 已加入 AI生成游戏';
     if(result.isError)detail=raw;
     if(expanded&&call.name==='write_game'&&!result.isError){const {diff,...metadata}=value;expandedWrite=JSON.stringify(metadata,null,2)+(diff?'\n\n代码差异\n'+diff:'')+'\n\n参数\n'+JSON.stringify(args,null,2);}
    }catch{detail=raw;}
    if(expanded)detail=expandedWrite??(call.name==='write_game'?raw+'\n'+JSON.stringify(args,null,2):JSON.stringify(args,null,2)+'\n'+raw);
   }else if(expanded)detail=JSON.stringify(args,null,2);
   blocks.push({id:call.id,kind:'tool',heading:(TOOL_LABELS[call.name]||call.name)+' · '+state,text:detail});
  }
  if(m.stopReason==='error'||m.stopReason==='aborted')blocks.push({id:'error-'+index,kind:'assistant',heading:m.stopReason==='aborted'?'已取消':'Agent 遇到问题',text:m.errorMessage||'本次操作未完成，进度已保存'});
 });return blocks;
}
export function mix(a:string,b:string,weight:number){return '#'+[1,3,5].map(i=>Math.round(parseInt(a.slice(i,i+2),16)*(1-weight)+parseInt(b.slice(i,i+2),16)*weight).toString(16).padStart(2,'0')).join('');}
