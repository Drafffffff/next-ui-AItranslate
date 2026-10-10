import type {AssistantMessage} from './node_modules/@earendil-works/pi-ai/dist/types.js';
import type {AssistantMessageEventStream} from './node_modules/@earendil-works/pi-ai/dist/utils/event-stream.js';
/** Incremental OpenAI-compatible SSE. Arguments are only parsed at completion;
 * neither partial JSON nor a disconnected stream can execute a tool. */
export function completionSse(message:AssistantMessage,stream:AssistantMessageEventStream){
 let buffer='',data:string[]=[],failure:Error|undefined,ended=false,saw=false,reason='',textIndex=-1,thinkingIndex=-1;
 const calls=new Map<number,{index:number;id:string;name:string;args:string}>();
 const emitText=(value:string,thinking=false)=>{
  if(!value)return;let index=thinking?thinkingIndex:textIndex;
  if(index<0){index=message.content.length;message.content.push(thinking?{type:'thinking',thinking:''}:{type:'text',text:''});if(thinking)thinkingIndex=index;else textIndex=index;stream.push({type:thinking?'thinking_start':'text_start',contentIndex:index,partial:message} as any);}
  const part=message.content[index] as any;part[thinking?'thinking':'text']+=value;
  stream.push({type:thinking?'thinking_delta':'text_delta',contentIndex:index,delta:value,partial:message} as any);
 };
 const dispatch=()=>{
  if(!data.length||ended)return;const raw=data.join('\n');data=[];
  if(raw==='[DONE]'){ended=true;return;}const value=JSON.parse(raw);saw=true;
  if(value.error)throw new Error(value.error.message||'模型流式响应失败');
  if(value.usage){const u=value.usage;message.usage.input=Number(u.prompt_tokens)||0;message.usage.output=Number(u.completion_tokens)||0;message.usage.totalTokens=Number(u.total_tokens)||message.usage.input+message.usage.output;message.usage.cacheRead=Number(u.prompt_cache_hit_tokens)||0;}
  const choice=value.choices?.[0];if(!choice)return;if(choice.finish_reason)reason=choice.finish_reason;
  const delta=choice.delta||{};emitText(delta.content||delta.refusal||'');emitText(delta.reasoning_content||'',true);
  for(const call of delta.tool_calls||[]){
   const number=call.index;if(!Number.isInteger(number)||number<0||number>=64)throw new Error('无效的流式工具序号');
   let item=calls.get(number);
   if(!item){item={index:message.content.length,id:'',name:'',args:''};calls.set(number,item);message.content.push({type:'toolCall',id:'',name:'',arguments:{}});stream.push({type:'toolcall_start',contentIndex:item.index,partial:message});}
   if(call.id)item.id+=call.id;if(call.function?.name)item.name+=call.function.name;
   const fragment=call.function?.arguments||'';item.args+=fragment;if(item.args.length>8*1024*1024)throw new Error('流式工具参数超过设备内存预算');
   Object.assign(message.content[item.index],{id:item.id,name:item.name});
   if(fragment)stream.push({type:'toolcall_delta',contentIndex:item.index,delta:fragment,partial:message});
  }
 };
 return {
  get sawEvents(){return saw;},
  feed(chunk:string){
   if(failure||ended)return;
   try{buffer+=chunk;if(buffer.length>8*1024*1024)throw new Error('流式事件超过设备内存预算');let at,cursor=0;
    // Scan one delivery once; copying the shrinking suffix for every line
    // becomes quadratic when a slow frame coalesces thousands of SSE events.
    while((at=buffer.indexOf('\n',cursor))>=0){const line=buffer.slice(cursor,at).replace(/\r$/,'');cursor=at+1;if(!line)dispatch();else if(line.startsWith('data:'))data.push(line.slice(5).replace(/^ /,''));}
    buffer=buffer.slice(cursor);
   }catch(error){failure=error instanceof Error?error:new Error(String(error));}
  },
  finish(){
   this.feed('\n\n');if(failure)throw failure;if(!ended&&!reason)throw new Error('模型流式响应中断，未执行未完成的工具');
   for(const item of calls.values()){
    if(!item.id||!item.name)throw new Error('模型返回了不完整的工具调用');
    let args:any;try{args=JSON.parse(item.args||'{}');}catch{args=item.args;}
    const call=message.content[item.index] as any;call.arguments=args;stream.push({type:'toolcall_end',contentIndex:item.index,toolCall:call,partial:message});
   }
   if(textIndex>=0)stream.push({type:'text_end',contentIndex:textIndex,content:(message.content[textIndex] as any).text,partial:message});
   if(thinkingIndex>=0)stream.push({type:'thinking_end',contentIndex:thinkingIndex,content:(message.content[thinkingIndex] as any).thinking,partial:message});
   if(textIndex<0&&!calls.size&&reason!=='length')throw new Error('模型没有返回文字或工具调用');
   message.stopReason=reason==='length'?'length':calls.size?'toolUse':'stop';
  }
 };
}
