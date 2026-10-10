from pathlib import Path
import json,difflib
base=Path('build/workshop-case-repairs')
def replace(s,a,b):
 assert s.count(a)==1,(a[:80],s.count(a));return s.replace(a,b)
for id in ['game-muqioi9l-yczh','game-muq0mtg0-ecul','game-muq16s15-5kr4']:
 p=json.loads((base/(id+'.original.json')).read_text());s=p['lua'];old=s
 if id=='game-muqioi9l-yczh':
  s=replace(s,'function blocked(px,py)\n','function blocked(px,py)\n if entered_dungeon then return px<8 or py<8 or px+7>119 or py+7>119 end\n')
  s=replace(s,' entered_dungeon=false\n build_map()',' entered_dungeon=false\n opened_caves={}\n build_map()\n for spot in all({{64,96},{152,56},{184,120}}) do\n  if not blocked(spot[1],spot[2]) then add(enemies,{x=spot[1],y=spot[2],hp=2,dx=0,dy=0,t=0}) end\n end\n music(6,200,3)')
  s=replace(s,' entered_dungeon=true\n player.x',' entered_dungeon=true\n swings={} pickups={} parts={}\n player.atk=0 inv=30\n player.x')
  s=replace(s,'function open_cave(cx,cy)\n spawn_shard','function open_cave(cx,cy)\n local key=cy*MAPW+cx\n if opened_caves[key] then add_msg("already collected") return end\n opened_caves[key]=true\n spawn_shard')
  s=replace(s,'   if abs(ddx)>1 then e.x+= (ddx>0 and sp or -sp) end\n   if abs(ddy)>1 then e.y+= (ddy>0 and sp or -sp) end','   local nx=e.x+(ddx>0 and sp or -sp)\n   local ny=e.y+(ddy>0 and sp or -sp)\n   if abs(ddx)>1 and not blocked(nx,e.y) then e.x=nx end\n   if abs(ddy)>1 and not blocked(e.x,ny) then e.y=ny end')
  s=replace(s,'if entered_dungeon and #enemies==0 then','if entered_dungeon and #enemies==0 and player.hp>0 then')
  s=replace(s,'      state="win"','      state="win" music(-1,200) sfx(3,3)')
  s=replace(s,'  state="over"','  state="over" music(-1,200)')
  s=replace(s,'  if shards>=3 and not entered_dungeon and cx>=18 then\n   start_dungeon()\n  elseif not entered_dungeon then\n   open_cave(cx,cy)\n  end','  if not entered_dungeon then\n   if cx==28 and cy==21 then\n    if shards>=3 then start_dungeon() else add_msg("need 3 shards") end\n   else open_cave(cx,cy) end\n  end')
  s=replace(s,' if shake>0 then\n  camera(rnd(3)-1.5,rnd(3)-1.5)\n else\n  camera()\n end',' if shake>0 then shake-=1 end')
  s=replace(s,'  local camx=player.x-3-57\n  local camy=player.y-3-57\n  camera(flr(camx),flr(camy))','  local camx=mid(0,player.x-60,MAPW*8-128)\n  local camy=mid(0,player.y-60,MAPH*8-128)\n  local sx=shake>0 and flr(rnd(3))-1 or 0\n  local sy=shake>0 and flr(rnd(3))-1 or 0\n  camera(flr(camx)+sx,flr(camy)+sy)')
  s=replace(s,' end\n camera()\nend\n\nfunction hero_sprite',' end\nend\n\nfunction hero_sprite')
  s=replace(s,' if entered_dungeon then\n  local n=#enemies',' camera()\n if entered_dungeon then\n  local n=#enemies')
  for a,b in [(16,2),(17,2),(18,2),(19,0),(20,4),(21,1),(22,0)]:s=s.replace('sfx('+str(a)+')','sfx('+str(b)+',3)')
 elif id=='game-muq0mtg0-ecul':
  s=replace(s,' "1200000300000001",',' "1020000300000001",')
  s=replace(s,' px=2*ts+1\n py=14*ts+1\n pa=0.75   -- face up along start straight',' px=20\n py=116\n pa=0.5   -- face left along the bottom straight')
  s=replace(s,' lasty=py\n passed_goal=false',' lastx=px\n checkpoint=1\n music(6,200,3)')
  s=replace(s,'function _init()','checkpoints={{12,64},{64,12},{116,64},{64,116}}\nfunction kart_blocked(x,y)\n for ox=-2.5,2.5,5 do for oy=-2.5,2.5,5 do\n  local cx=flr((x+ox)/ts)+1 local cy=flr((y+oy)/ts)+1\n  if cx<1 or cx>16 or cy<1 or cy>16 or sub(track[cy],cx,cx)=="1" then return true end\n end end\n return false\nend\nfunction advance_track()\n local c=checkpoints[checkpoint]\n if c and abs(px-c[1])<=6 and abs(py-c[2])<=6 then checkpoint+=1 end\n if checkpoint==5 and lastx>20 and px<=20 and abs(py-116)<=3 then\n  lap+=1 checkpoint=1 sfx(0,3)\n  if lap>=maxlap then best=min(best,t) state="over" music(-1,200) sfx(3,3) end\n end\n lastx=px\nend\n\nfunction _init()')
  s=replace(s,' if left then turn=-0.025 end\n if right then turn=0.025 end',' if left then turn=0.025 end\n if right then turn=-0.025 end')
  s=s.replace('flr((px+pw/2)/ts)','flr(px/ts)').replace('flr((py+ph/2)/ts)','flr(py/ts)')
  a=s.index(' px+=dx\n py+=dy\n');b=s.index('\nend\n\nfunction _draw()',a)
  s=s[:a]+''' if kart_blocked(px+dx,py+dy) then
  if abs(pv)>0.3 then sfx(2,3) end
  pv*=-0.5
 else px+=dx py+=dy end
 advance_track()'''+s[b:]
 else:
  s=replace(s,' tick=0 drop=30 state="play"',' tick=0 drop=30 state="play"\n move_dir=0 move_hold=0 oldx=false oldo=false')
  s=replace(s,' -- horizontal hold with repeat\n if btn(0) then if try_move(-1,0) then sfx(17,3) end end\n if btn(1) then if try_move(1,0) then sfx(17,3) end end',' -- immediate move, then DAS 10 frames and ARR 3 frames\n local dir=(btn(1) and 1 or 0)-(btn(0) and 1 or 0)\n local repeat_move=false\n if dir==0 then move_dir=0 move_hold=0\n elseif dir!=move_dir then move_dir=dir move_hold=0 repeat_move=true\n else move_hold+=1 repeat_move=move_hold>=10 and (move_hold-10)%3==0 end\n if repeat_move and try_move(dir,0) then sfx(17,3) end')
  s=replace(s,' if tick>=drop then\n  tick=0\n  if not try_move(0,1) then lock_piece() end\n end',' local interval=btn(3) and min(drop,2) or drop\n if tick>=interval then\n  tick=0\n  if not try_move(0,1) then lock_piece()\n  elseif btn(3) then score=min(30000,score+1) end\n end')
  s=replace(s,' -- soft drop / hard drop on x tap',' -- X hard drop; down is continuous soft drop')
  s=replace(s,'print("arrows  move",','print("lr move down soft",')
 (base/(id+'.patched.lua')).write_text(s)
 (base/(id+'.diff')).write_text(''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='published.lua',tofile='repaired.lua')))
 print(id,len(old),len(s),'changed')
