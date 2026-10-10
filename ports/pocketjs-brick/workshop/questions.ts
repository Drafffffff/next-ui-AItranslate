import type {GameMessage} from './agent.ts';
export interface UserQuestion {id:string;question:string;options:{label:string;description?:string}[];createdAt:number;answer?:QuestionAnswer}
export interface QuestionAnswer {answer:string;skipped?:boolean}
export const QUESTION_PARAMETERS={type:'object',properties:{question:{type:'string',minLength:1},options:{type:'array',maxItems:8,items:{type:'object',properties:{label:{type:'string',minLength:1},description:{type:'string'}},required:['label'],additionalProperties:false}}},required:['question'],additionalProperties:false};
export function makeQuestion(args:Record<string,unknown>,id:string):UserQuestion{
 if(!id||typeof args.question!=='string'||!args.question.trim()||args.question.length>16000)throw new Error('请提供一个明确的问题');
 const options=args.options??[];if(!Array.isArray(options)||options.length>8)throw new Error('建议选项最多八项，用户始终可以语音回答');
 for(const o of options)if(!o||typeof o.label!=='string'||!o.label.trim()||o.label.length>2000||o.description!==undefined&&(typeof o.description!=='string'||o.description.length>4000))throw new Error('问题选项格式异常');
 return {id,question:args.question.trim(),options:options.map(o=>({...o})),createdAt:Date.now()};
}
export function questionCall(messages:GameMessage[],id:string){return messages.some(m=>m.role==='assistant'&&m.content.some(c=>c.type==='toolCall'&&c.id===id&&c.name==='ask_user_question'))&&!messages.some(m=>m.role==='toolResult'&&m.toolCallId===id&&!m.isError);}
export function validQuestion(value:unknown,messages:GameMessage[]):value is UserQuestion{try{const q=value as UserQuestion;makeQuestion(q as unknown as Record<string,unknown>,q.id);return questionCall(messages,q.id)&&(!q.answer||typeof q.answer.answer==='string');}catch{return false;}}
export function recordAnswer(messages:GameMessage[],q:UserQuestion,answer:QuestionAnswer){
 if(!questionCall(messages,q.id))throw new Error('这个问题已经回答或不属于当前对话');
 for(let i=messages.length-1;i>=0;i--)if(messages[i].role==='toolResult'&&(messages[i] as any).toolCallId===q.id&&(messages[i] as any).isError)messages.splice(i,1);
 const result:GameMessage={role:'toolResult',toolCallId:q.id,toolName:'ask_user_question',content:[{type:'text',text:JSON.stringify(answer)}],isError:false,timestamp:Date.now()};messages.push(result);
}
