// Bundle the repository Skill and its required references for the QuickJS host.
// Runtime never depends on filesystem access by the remote model.
import assert from 'node:assert/strict';
import {readFile, writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
const root = new URL('../../../skills/pico8-game-dev/', import.meta.url);
const exampleCatalog = JSON.parse(await readFile(new URL('assets/examples/catalog.json', root), 'utf8'));
assert.equal(exampleCatalog.schema_version, 1);
assert.deepEqual(exampleCatalog.examples.map(e => e.id).sort(), ['collect','dodge','mesh3d','platform','raycaster','snake','sokoban']);
for(const e of exampleCatalog.examples) assert(e.path === 'assets/examples/'+e.id+'.lua' && e.keywords.length && e.keywords.every(k => typeof k === 'string' && k.length), 'Invalid example metadata');
const community = JSON.parse(await readFile(new URL('assets/community/sources.json', root), 'utf8'));
const patterns = JSON.parse(await readFile(new URL('assets/community/patterns.json', root), 'utf8'));
assert.equal(community.schema_version,1); assert.equal(patterns.schema_version,1);
const communityFiles=[];
for(const source of community.sources){
 for(const [path,hash] of [[source.local_path,source.sha256],[source.license_path,source.license_sha256]]){
  if(!path)continue;
  assert(/^assets\/community\/[a-z0-9-]+\/(original\.p8|LICENSE)$/.test(path),'Invalid community path');
  const bytes=await readFile(new URL(path,root));
  assert.equal(createHash('sha256').update(bytes).digest('hex'),hash,'Community source or license changed: '+path);
  communityFiles.push({path,sha256:hash});
 }
}
for(const pattern of patterns.patterns){
 const source=community.sources.find(s=>s.id===pattern.source_id);
 assert(source && source.kind==='community-cart' && source.source_url===pattern.source_url && source.license===pattern.license && source.revision===pattern.revision,'Invalid pattern provenance');
 const original=(await readFile(new URL(source.local_path,root))).toString('latin1');
 assert(original.includes(pattern.code),'Snippet is not an exact upstream excerpt');
 assert(exampleCatalog.examples.some(e=>e.id===pattern.example_id));
}
const libraryModuleNames=['library-patterns','platform-feel','grid-turns','racing-physics','combat-design','level-design'];
const paths = [...libraryModuleNames.map(name=>'modules/'+name+'.md'),'assets/snippets/grid-input.lua','references/library-cases.json','modules/gameplay-design.md','assets/audio.json','modules/music.md','modules/3d.md','modules/pixel-art.md','references/community.md','assets/community/sources.json','assets/community/patterns.json','modules/game-loop.md', 'assets/examples/catalog.json', ...exampleCatalog.examples.map(e => e.path), 'SKILL.md', 'references/pico8-api.md', 'references/project-contract.md', 'assets/catalog.json', 'assets/starter.p8'];
const resources = Object.fromEntries(await Promise.all(paths.map(async path => [path, await readFile(new URL(path, root), 'utf8')])));
const library=JSON.parse(resources['references/library-cases.json']);
assert.equal(library.schema_version,1);
assert.equal(new Set(library.cases.map(c=>c.id)).size,library.cases.length);
for(const c of library.cases){
 assert(/^[a-z0-9-]+$/.test(c.id) && c.name && c.author);
 for(const hash of [c.cart_sha256,c.lua_sha256])assert(/^[a-f0-9]{64}$/.test(hash));
 for(const url of [c.source_url,c.cart_url])assert(new URL(url).hostname==='www.lexaloffle.com');
 assert(c.cart_hash_verified_on_device===true && c.lua_lines>0 && c.lua_bytes>0 && [30,60].includes(c.source_hz));
 assert(['mechanism-review','preliminary'].includes(c.review_level));
 assert(c.review_level==='preliminary'||c.observations.length>0);
 for(const o of c.observations)assert(o.symbol && o.finding && o.lines.length===2 && Number.isInteger(o.lines[0]) && Number.isInteger(o.lines[1]) && o.lines[0]>0 && o.lines[0]<=o.lines[1] && o.lines[1]<=c.lua_lines);
}
const docs = resources['SKILL.md'].replace(/^---\n[\s\S]*?\n---\n/, '').trim();
const contract = resources['references/project-contract.md'].split('## 单次 JSON 模式：开发端参考')[0].trim();
assert(contract.includes('read_game') && contract.includes('sprite_overrides'), 'Missing default tool contract');
const catalog = JSON.parse(resources['assets/catalog.json']);
const gfx = resources['assets/starter.p8'].split('\n__gfx__\n')[1]?.split('\n__')[0].trimEnd();
assert(gfx && gfx.split('\n').length === 128 && gfx.split('\n').every(row => /^[0-9a-f]{128}$/.test(row)), 'Invalid starter gfx');
assert.deepEqual(catalog.sprites.map(sprite => sprite.id).sort((a,b) => a-b), Array.from({length:12}, (_,i) => i+1));
const rows = gfx.split('\n');
for (const sprite of catalog.sprites) {
  const x = sprite.id % 16 * 8, y = Math.floor(sprite.id / 16) * 8;
  assert.deepEqual(rows.slice(y, y+8).map(row => row.slice(x, x+8)), sprite.pixels, 'Starter/catalog sprite mismatch');
}
const audio=JSON.parse(resources['assets/audio.json']);
assert.equal(audio.schema_version,1);
// Verify the community soundtrack adaptation against the retained original bytes.
const communityAudio=(await readFile(new URL(audio.source,root))).toString('latin1');
const upstreamSfx=communityAudio.split('__sfx__\n')[1].split('\n__')[0].split('\n');
for(const sound of audio.sfx.filter(s=>s.origin==='community-adaptation')){
 const row=upstreamSfx[sound.upstream_sfx];assert.equal(row.length,168);
 assert.equal(sound.id,sound.upstream_sfx+4);
 assert.equal(sound.speed,parseInt(row.slice(2,4),16));assert.equal(sound.loop_start,parseInt(row.slice(4,6),16));assert.equal(sound.loop_end,parseInt(row.slice(6,8),16));
 assert.deepEqual(sound.notes,Array.from({length:32},(_,i)=>[parseInt(row.slice(8+i*5,10+i*5),16),parseInt(row[10+i*5],16),Math.min(3,parseInt(row[11+i*5],16)),parseInt(row[12+i*5],16)]));
}
const upstreamMusic=communityAudio.split('__music__\n')[1].split('\n__')[0].split('\n');
for(let id=0;id<6;id++){
 const row=upstreamMusic[id],flags=parseInt(row.slice(0,2),16);
 assert.deepEqual(audio.music[id],{id,channels:Array.from({length:4},(_,i)=>{const n=parseInt(row.slice(3+i*2,5+i*2),16);return n>=64?-1:n+4;}),loop_start:!!(flags&1),loop_end:!!(flags&2),stop:!!(flags&4)});
}
const {tracks,custom_sfx_ids,custom_music_ids}=audio;
const audioCatalog={tracks,sounds:audio.sfx.map(({notes,...s})=>s),custom_sfx_ids,custom_music_ids};
const audioData={sfx:audio.sfx.map(({id,speed,loop_start,loop_end,notes})=>({id,speed,loop_start,loop_end,notes})),music:audio.music};
const assets = {catalog_version:catalog.schema_version, sprites:catalog.sprites.map(({pixels, ...sprite}) => sprite),
  custom_sprite_ids:{min:32,max:63}, optional_audio_module:"music"};
const examples=exampleCatalog.examples.map(e=>{
  const lua=resources[e.path];
  assert(lua.length<=8000 && /^[\x09\x0a\x0d\x20-\x7e]+$/.test(lua), 'Example source must be compact ASCII Lua');
  for(const callback of ['_init','_update','_draw'])assert.equal((lua.match(new RegExp('function\\s+'+callback+'\\s*\\(', 'g'))||[]).length,1,'Missing or duplicate example callback');
  return {...e,lua};
});
const modules=[
 {name:'library-patterns',path:'modules/library-patterns.md',description:'新建/重做核心：从实际掌机社区卡带选择实现方法和质量重点',kind:'reference'},
 {name:'platform-feel',path:'modules/platform-feel.md',description:'Celeste/Starling：跳跃缓冲、离地容错、手感与分轴碰撞',kind:'reference'},
 {name:'grid-turns',path:'modules/grid-turns.md',description:'Porklike/Guncho/Woodworm/Tetris：回合事务、撤销、软降与益智信息',kind:'reference'},
 {name:'racing-physics',path:'modules/racing-physics.md',description:'Off Road/Breakout/Combo Pool：操控、检查点、连续碰撞与合成',kind:'reference'},
 {name:'combat-design',path:'modules/combat-design.md',description:'Just One Boss/Buns：攻击预告、状态机、受击与有限波次',kind:'reference'},
 {name:'level-design',path:'modules/level-design.md',description:'Dusk Child/Into Ruins：场景、地图约束、目标与检查点恢复',kind:'reference'},
 {name:'gameplay-design',path:'modules/gameplay-design.md',description:'完善用户想法：动作、目标、风险取舍、反馈与可玩的创意组合',kind:'reference'},
 {name:'api',path:'references/pico8-api.md',description:'P8 API、数值与兼容范围',kind:'reference'},
 {name:'game-loop',path:'modules/game-loop.md',description:'状态、输入、碰撞、难度、重开与修复场景',kind:'reference'},
 {name:'3d',path:'modules/3d.md',description:'射线、真实三维顶点、透视、裁剪与预算',kind:'reference'},
 {name:'pixel-art',path:'modules/pixel-art.md',description:'精灵、配色、动画与占格',kind:'reference'},
 {name:'music',path:'modules/music.md',description:'需要新增或修改音乐/音效时读取；宿主同时返回当前音频数据',kind:'reference'},
 {name:'community',path:'references/community.md',description:'社区原作与公开Skill来源及采用记录',kind:'reference'},
].map(m=>({...m,text:resources[m.path].trim()}));
modules.find(m=>m.name==='grid-turns').text+='\n\n## 可调输入组件（工坊实现）\n\n```lua\n'+resources['assets/snippets/grid-input.lua']+'```';
for(const e of examples){
 const pattern=patterns.patterns.find(p=>p.example_id===e.id);
 const text=[e.title+'：'+e.when,
  '教学示例，不是当前项目源码或已执行结果。修改已有游戏仍以read_game为准；保留未要求改变的规则。',
  '```lua\n'+e.lua+'```',
  ...(pattern?['社区节选（带原作依赖，不能直接提交）：'+pattern.source_url+'；许可 '+pattern.license,pattern.adaptation,'```lua\n'+pattern.code+'\n```']:[])
 ].join('\n\n');
 modules.push({name:'example/'+e.id,path:e.path,description:e.when,kind:'complete-lua',text});
}
assert.equal(new Set(modules.map(m=>m.name)).size,modules.length);
for(const c of library.cases)for(const o of c.observations)for(const name of o.modules)assert(modules.some(m=>m.name===name),'Unreachable library module: '+name);
// Provenance is available when requested, without injecting source corpora or
// the research manifest into every model request.
modules.find(m=>m.name==='library-patterns').text+='\n\n## 实际审阅版本的来源\n\n'+library.cases.filter(c=>c.review_level==='mechanism-review').map(c=>
 `- [${c.name}](${c.source_url}) — ${c.author}；原作${c.source_hz} Hz。`+c.observations.map(o=>`${o.symbol} L${o.lines.join('–')}`).join('；')+'。'
).join('\n');
const moduleIndex=modules.map(({text,...m})=>m);
const manifest = {name:'pico8-game-dev', resources:[...paths.map(path => ({path, sha256:createHash('sha256').update(resources[path]).digest('hex')})),...communityFiles]};
const values={PICO8_SKILL_MODULES:modules,PICO8_MODULE_INDEX:moduleIndex,PICO8_AUDIO_DATA:audioData,PICO8_AUDIO_CATALOG:audioCatalog,
 PICO8_SKILL_TEXT:docs,PICO8_TOOL_CONTRACT:contract,PICO8_ASSETS:assets,PICO8_BUILTIN_GFX:gfx,PICO8_SKILL_MANIFEST:manifest};
const source = '// Generated by build-skill.mjs from skills/pico8-game-dev. Do not edit by hand.\n' +
  Object.entries(values).map(([name,value]) => `export const ${name} = ${JSON.stringify(value)};\n`).join('');
const output = new URL('skill.generated.ts', import.meta.url);
if (process.argv.includes('--check')) assert.equal(await readFile(output, 'utf8'), source, 'Skill bundle stale; run npm run build:skill');
else await writeFile(output, source);
console.log(`${process.argv.includes('--check')?'Checked':'Bundled'} pico8-game-dev (${Buffer.byteLength(source)} bytes): ${fileURLToPath(output)}`);
