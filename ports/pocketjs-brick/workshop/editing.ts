import {applyEditsToNormalizedContent,detectLineEnding,normalizeToLF,restoreLineEndings,generateDiffString} from './third-party/pi-edit.ts';

export const EDIT_PROPERTIES={edits:{type:'array',minItems:1,maxItems:64,description:'Precise changes to the current Lua. All oldText blocks match the same original source, uniquely and without overlap. Use enough surrounding context; do not send unchanged whole programs.',items:{type:'object',properties:{oldText:{type:'string',minLength:1},newText:{type:'string'}},required:['oldText','newText'],additionalProperties:false}}};

export function updateLua(current:string,args:Record<string,unknown>){
 if(args.lua!==undefined&&args.edits!==undefined)throw new Error('lua与edits只能选择一种；局部修改请传edits，完整重写请传lua');
 if(args.edits!==undefined){
  const edits=args.edits as {oldText:string;newText:string}[];
  if(!Array.isArray(edits)||!edits.length||edits.length>64)throw new Error('edits需要1..64个替换块');
  const result=applyEditsToNormalizedContent(normalizeToLF(current),edits,'当前游戏Lua');
  return {lua:restoreLineEndings(result.newContent,detectLineEnding(current)),mode:'edit',edit_count:edits.length};
 }
 return {lua:args.lua===undefined?current:String(args.lua),mode:args.lua===undefined?'metadata-assets':'replace',edit_count:0};
}

export function codeDifference(before:string,after:string){
 if(before===after)return {diff:'',diff_truncated:false,firstChangedLine:undefined};
 const result=generateDiffString(normalizeToLF(before),normalizeToLF(after));
 const truncated=result.diff.length>8000||result.diff.startsWith('[Diff exceeds');
 return {...result,diff:result.diff.length>8000?result.diff.slice(0,8000)+'\n[Diff display truncated; read_game returns complete source.]':result.diff,diff_truncated:truncated};
}
