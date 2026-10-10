/** Fixed developer-verified catalog, 2026-10-02. Never fetch models on device.
 * https://api-docs.deepseek.com/updates/
 * https://help.aliyun.com/zh/model-studio/qwen3-8-max
 * https://help.aliyun.com/zh/model-studio/qwen3-8-flash
 * https://help.aliyun.com/zh/model-studio/deepseek-v4-1-flash
 */
export type GenerationProvider='deepseek'|'bailian'|'teamo';
interface Channel {endpoint:string;key:string}
export interface Config {
 key:string;endpoint:string;model:string;timeoutMs:number;provider?:GenerationProvider;
 generationChannels?:Partial<Record<GenerationProvider,Channel>>;
 contextWindowTokens?:number;contextReserveTokens?:number;contextKeepRecentTokens?:number;
 [name:string]:unknown;
}
export const GENERATION_MODELS=[
 {provider:'deepseek',model:'deepseek-v4-pro',name:'DeepSeek V4 Pro',tier:'旗舰'},
 {provider:'deepseek',model:'deepseek-flash',name:'DeepSeek V4.1 Flash',tier:'Flash'},
 {provider:'bailian',model:'qwen3.8-max',name:'Qwen3.8 Max',tier:'旗舰'},
 {provider:'bailian',model:'qwen3.8-flash',name:'Qwen3.8 Flash',tier:'Flash'},
 {provider:'bailian',model:'deepseek-v4.1-flash',name:'DeepSeek V4.1 Flash',tier:'Flash'},
 // Exact ID and OpenAI-compatible route from the user's local Pi configuration.
 {provider:'teamo',model:'claude-haiku-5-5',name:'Claude Haiku 5.5',tier:'Fast'},
] as const;
const ENDPOINTS={deepseek:'https://api.deepseek.com',bailian:'https://dashscope.aliyuncs.com/compatible-mode/v1',teamo:'https://api.teamorouter.cn/v1'};
export const providerName=(provider:GenerationProvider)=>provider==='bailian'?'百炼':provider==='teamo'?'TeamoRouter':'DeepSeek';
export function generationProvider(config:{provider?:GenerationProvider;endpoint:string;model?:string}):GenerationProvider {
 if(config.provider)return config.provider;
 if(/^https:\/\/api\.teamorouter\.cn(?:\/|$)/.test(config.endpoint))return 'teamo';
 return /(?:dashscope(?:-intl|-us)?\.aliyuncs\.com|\.maas\.aliyuncs\.com)(?:\/|$)/.test(config.endpoint)||config.model?.startsWith('qwen')?'bailian':'deepseek';
}
export function loadGenerationConfig(value:Record<string,unknown>={},existingKey=''):Config {
 const endpoint=typeof value.endpoint==='string'?value.endpoint:typeof value.baseUrl==='string'?value.baseUrl:'';
 const provider=generationProvider({provider:value.provider==='bailian'||value.provider==='deepseek'||value.provider==='teamo'?value.provider:undefined,endpoint,model:typeof value.model==='string'?value.model:undefined});
 return {...value,provider,endpoint:endpoint||ENDPOINTS[provider],model:typeof value.model==='string'?value.model:provider==='bailian'?'qwen3.8-flash':provider==='teamo'?'claude-haiku-5-5':'deepseek-flash',
  key:typeof value.key==='string'?value.key:provider==='deepseek'?existingKey:'',timeoutMs:typeof value.timeoutMs==='number'?value.timeoutMs:120000};
}
export function modelChoice(config:Config):number {
 return GENERATION_MODELS.findIndex(option=>option.provider===generationProvider(config)&&option.model===config.model);
}
export const modelLabel=(config:Config)=>{const choice=modelChoice(config);return (generationProvider(config)==='bailian'?'百炼 · ':'')+(choice<0?config.model:GENERATION_MODELS[choice].name);};
/** Keep legacy per-channel keys/hosts separate; public keys stay in native code.
 * Spreading preserves ASR, regional/workspace hosts and context settings.
 */
export function selectGenerationModel(config:Config,index:number):Config {
 const choice=GENERATION_MODELS[index];if(!choice)throw new Error('请选择可用模型');
 const current=generationProvider(config);
 const channels={...config.generationChannels,[current]:{endpoint:config.endpoint,key:config.key}};
 const channel=channels[choice.provider]??{endpoint:ENDPOINTS[choice.provider],key:''};
 return {...config,provider:choice.provider,model:choice.model,endpoint:channel.endpoint,key:channel.key,generationChannels:channels};
}
/** Native provider parameters are authoritative; Pi also records thinking=medium. */
export const generationParameters=(provider:GenerationProvider)=>provider==='bailian'?{enable_thinking:true}:provider==='teamo'?{reasoning_effort:'medium'}:{thinking:{type:'enabled'}};
/** Qwen's legacy max_tokens has a 32768 thinking-mode cap. The recommended
 * complete-output field includes reasoning and supports the current models.
 * https://help.aliyun.com/zh/model-studio/deep-thinking */
export const generationBudget=(provider:GenerationProvider,id:string,limit:number,summary=false)=>provider==='bailian'&&/^qwen3[.-]/.test(id)?
 {max_completion_tokens:limit,...(summary?{thinking_budget:Math.min(4096,Math.floor(limit/2))}:{})}:{max_tokens:limit};
