import {PICO8_SKILL_TEXT,PICO8_TOOL_CONTRACT,PICO8_ASSETS,PICO8_BUILTIN_GFX,PICO8_SKILL_MODULES,PICO8_MODULE_INDEX} from './skill.generated.ts';

export const BLANK_GFX=('0'.repeat(128)+'\n').repeat(128).trimEnd();
export const BUILTIN_GFX=PICO8_BUILTIN_GFX;
export interface SpriteOverride {id:number;pixels:string[]}
export function validateGfx(gfx:string) {
 if(typeof gfx!=='string'||gfx.split('\n').length!==128||!gfx.split('\n').every(row=>/^[0-9a-f]{128}$/.test(row)))throw new Error('精灵图像必须为 128×128 小写十六进制像素');
}
export function mergeSprites(gfx:string,overrides:unknown=[]):string {
 validateGfx(gfx);
 if(!Array.isArray(overrides)||overrides.length>32)throw new Error('自定义精灵最多 32 条');
 const seen=new Set<number>();
 for(const patch of overrides){
  if(!patch||typeof patch!=='object'||Object.keys(patch).sort().join(',')!=='id,pixels'||!Number.isInteger(patch.id)||patch.id<32||patch.id>63||seen.has(patch.id))throw new Error('自定义精灵 ID 必须唯一且位于 32..63');
  if(!Array.isArray(patch.pixels)||patch.pixels.length!==8||!patch.pixels.every((row:unknown)=>typeof row==='string'&&/^[0-9a-f]{8}$/.test(row)))throw new Error('自定义精灵必须为8行，每行8个小写0..f；透明色填0，不能填点号。');
  seen.add(patch.id);
 }
 const rows=gfx.split('\n');
 for(const patch of overrides){const x=patch.id%16*8,y=Math.floor(patch.id/16)*8;for(let dy=0;dy<8;dy++)rows[y+dy]=rows[y+dy].slice(0,x)+patch.pixels[dy]+rows[y+dy].slice(x+8);}
 return rows.join('\n');
}
export function gameAssets(gfx:string) {
 validateGfx(gfx);const rows=gfx.split('\n'),custom_sprites:SpriteOverride[]=[];
 for(let id=32;id<=63;id++){const x=id%16*8,y=Math.floor(id/16)*8,pixels=rows.slice(y,y+8).map(row=>row.slice(x,x+8));if(pixels.some(row=>/[1-9a-f]/.test(row)))custom_sprites.push({id,pixels});}
 return {...PICO8_ASSETS,custom_sprites};
}
export const SKILL_MODULE_NAMES=PICO8_MODULE_INDEX.map(m=>m.name);
export function readSkillModule(name:string){
 const module=PICO8_SKILL_MODULES.find(m=>m.name===name);
 if(!module)throw new Error('未知Skill模块，请使用模块目录中的名称');
 return {module:module.name,path:module.path,kind:module.kind,text:module.text,advisory:module.kind==='complete-lua'};
}
const HOST=`You develop exactly one PICO-8 game in the current project. Apply the core Skill below to every create, modify and repair request. Read the exact current draft first. Load only relevant Skill modules through read_skill when their details are needed; do not preload all modules or full-game examples. The music module contains sound authoring details and current project audio. Examples are teaching references, never the current source. Preserve unspecified existing rules and assets. Keep a concise persistent design_record through write_game: core_action, goal, risk, controls, scenes, audiovisual, preserve and unresolved. Read it with read_game on every modification; update only decisions justified by the user and actual code, preserve named input/goal rules. New/core-reworked games must create this record; local changes preserve it unless changed decisions require an update. When repairing stutter, distinguish input cadence, frame workload, authored rests, repeated sound triggers and runtime output. Tie one main change to exact source evidence and an observable check; do not claim CPU shortage, underruns or verified smoothness without measurements. Load grid-turns for held input and music for note duration/envelopes, preserving unrelated mechanics. Before check_game, review reachable input, goal/failure, world/camera coordinates, collision scene, restart and preserved rules against the actual code diff. BGM and operation SFX are required by default; load music only as needed. Use write_game for both complete Lua creation/restructure and precise edits:[{oldText,newText}] for local changes. Preserve omitted title, summary and assets; do not combine lua and edits. Read the returned diff, wait for a successful check_game, then call publish_game separately. Another write invalidates that check. Use ask_user_question for a meaningful unresolved gameplay decision; wait for the actual answer, respect free-form replies and skip. Pure discussion and read-only explanation can finish without publishing. New projects contain a playable host-created scaffold; adapt it around the user idea. Use terminal only for bounded synchronous JavaScript source analysis or pure algorithm checks against readText("game.lua"). This is an isolated interpreter with temporary memory files, not Linux shell or Node.js; print, readText, writeText and listFiles are the only host capabilities. Optional system_files can load bounded read-only snapshots of runtime, bundled Skill, workshop logs and system diagnostics. Use only documented virtual paths, not arbitrary SD-card paths. It cannot modify actual files, use imports, execute commands or access a network; write_game is still the edit tool. Do not confuse script checks with gameplay testing. Use only provided tools. No arbitrary filesystem access or external Skill activation is needed. Answer briefly in Simplified Chinese based on actual tool results; never claim gameplay was tested. Workshop SR (physical Start) launches the published game, A is voice and X stops active work.`;
export const PICO8_SYSTEM_PROMPT=[HOST,
 '<skill name="pico8-game-dev">',PICO8_SKILL_TEXT,'</skill>',
 '<reference name="references/project-contract.md" mode="tools">',PICO8_TOOL_CONTRACT,'</reference>',
 '<skill_modules>',JSON.stringify(PICO8_MODULE_INDEX),'</skill_modules>',
 '<asset_catalog name="assets/catalog.json">',JSON.stringify(PICO8_ASSETS),'</asset_catalog>'
].join('\n\n');
export function pico8SystemPrompt(existingDraft=false){
 return PICO8_SYSTEM_PROMPT+'\n\n<project_mode>'+
  (existingDraft?'已有游戏或草稿：本次基于read_game持续修改。重做核心玩法时加载library-patterns及相应类型模块；局部修复按实际代码选择参考，只有需要结构参考时才加载完整示例。':'新项目：先read_game，再read_skill library-patterns与一个相应类型模块，围绕用户目标建立小型完整玩法。示例仅在需要完整结构参考时读取。')+'</project_mode>';
}
