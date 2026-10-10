#!/usr/bin/env python3
"""Development-only checks: pinned z8lua syntax/fixed-point behavior + mocked P8 I/O.

Does not add gameplay testing to the handheld Agent or prove rendering/performance.
Uses existing pinned source from workshop build; never downloads or changes it.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from build_cart import check_lua

ROOT = Path(__file__).resolve().parents[3]
SKILL = Path(__file__).resolve().parent.parent
PIN = 'e6928578d46b61fd5ea30cfcf547e855a30a0553'
CORE = 'lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lctype eris lauxlib lbaselib lcorolib ldblib ltablib lstrlib lpico8lib linit'.split()
PRELUDE = '''
keys={}
music_calls={} sound_calls={}
function music(n,fade,mask) add(music_calls,{n=n,fade=fade,mask=mask}) end
function sfx(n,ch) add(sound_calls,{n=n,ch=ch}) end
function btn(i) return keys[i] or false end
function btnp(i) return btn(i) end
function add(t,v) t[#t+1]=v return v end
function deli(t,i) local v=t[i] for j=i,#t-1 do t[j]=t[j+1] end t[#t]=nil return v end
function all(t) local i=0 return function() i=i+1 return t[i] end end
seed=1
function rnd(n) seed=(seed*17+13)%997 return (seed/997)*(n or 1) end
function noop() end
cls=noop rect=noop rectfill=noop spr=noop print=noop pset=noop clip=noop
lines={}
function line(a,b,c,d,e) add(lines,{a,b,c,d,e}) end
function frame(k) keys=k or {} _update() _draw() end
function start() frame({}) frame({[4]=true}) assert(state=="play") frame({}) end
function near(a,b) assert(abs(a-b)<0.001,"position mismatch") end
'''
TESTS = {
'collect': '''
_init() assert(state=="title") start()
score=2 tick=3 frame({[4]=true}) frame({[4]=true}) assert(score==2 and tick==5,"held restart repeated")
local x=player.x frame({[0]=true}) near(player.x,x-1.5)
player.x=4 player.y=20 frame({[0]=true,[2]=true}) assert(player.x==4 and player.y==20)
local random=rnd local attempts=0
rnd=function(n) attempts=attempts+1 return 0 end
place_fish() assert(attempts==32 and not overlap(player,fish),"spawn fallback") rnd=random
score=0
for i=1,10 do fish={x=player.x,y=player.y,w=8,h=8} frame() end
assert(state=="win" and score==10,"collect target")
x=player.x frame({[1]=true}) assert(player.x==x,"ended game still moves")
start() assert(score==0 and left==30 and tick==1,"reset")
left=1 tick=29 fish={x=4,y=20,w=8,h=8} frame() assert(state=="over" and left==0,"deadline")
''',
'dodge': '''
_init() start()
hazards={{x=player.x,y=player.y,w=8,h=8,warning=0,speed=0},
 {x=player.x,y=player.y,w=8,h=8,warning=0,speed=0}}
frame() assert(lives==2 and invul==45 and #hazards==1,"multiple hits in one frame")
frame() assert(lives==2 and invul==44,"invulnerability")
lives=1 invul=0 score=8 frame() assert(state=="over" and best==8,"game over")
start() assert(lives==3 and #hazards==0 and invul==0 and best==8,"reset leaves stale objects")
hazards={} for i=1,12 do add(hazards,{x=4,y=20,w=8,h=8,warning=100,speed=0}) end
spawn=0 frame() assert(#hazards<=12,"unbounded spawning")
hazards={{x=4,y=128,w=8,h=8,warning=0,speed=0}} frame() assert(#hazards==0,"offscreen leak")
reset_round()
for batch=1,3 do for i=1,20000 do hazards={} spawn=30 frame() end end
assert(score==2000 and state=="play","long-run fixed-point overflow")
score=30000 tick=29 frame() assert(score==30000,"score saturation")
''',
'snake': '''
_init() start()
frame({[0]=true}) assert(dx==1 and dy==0,"reverse accepted")
frame({[2]=true}) frame({[0]=true}) assert(dx==0 and dy==-1,"two turns in one tick")
while tick~=0 do frame() end
assert(body[1].x==5 and body[1].y==4,"queued turn lost")
-- The departing tail is legal when not eating.
body={{x=1,y=1},{x=1,y=2},{x=2,y=2},{x=2,y=1}}
dx=1 dy=0 tick=5 turned=true food={x=9,y=9}
frame() assert(state=="play" and body[1].x==2 and #body==4,"tail vacates")
food={x=3,y=1} tick=5 turned=true frame() assert(score==1 and #body==5,"growth")
body={{x=11,y=1},{x=10,y=1}} dx=1 dy=0 tick=5 turned=true food={x=0,y=0}
frame() assert(state=="over","wall collision")
start() assert(#body==3 and score==0,"restart")
body={} for y=0,11 do for x=0,11 do add(body,{x=x,y=y}) end end
place_food() assert(state=="win","full board must terminate")
reset_round() body={{x=5,y=5},{x=5,y=4},{x=4,y=4},{x=4,y=5}}
dx=-1 dy=0 tick=5 turned=true food={x=9,y=9}
frame() assert(state=="play","tail exclusion")
''',
'platform': '''
_init() start() assert(grounded,"standing on ground")
frame({[4]=true}) assert(vy<0 and not grounded,"jump")
for i=1,60 do frame({[4]=true}) end
assert(grounded and player.y==112 and vy==0,"held O auto-jumps or landing jitters")
frame() frame({[4]=true}) assert(vy<0,"new jump edge")
-- Horizontal wall and vertical ceiling are distinct contacts.
player={x=18,y=100,w=8,h=8} move_x(5) assert(player.x==20,"horizontal penetration")
player={x=30,y=108,w=8,h=8} vy=-3 move_y() assert(player.y==106 and vy==0 and not grounded,"ceiling counts as floor")
player={x=30,y=90,w=8,h=8} vy=2 move_y() assert(player.y==92 and grounded and vy==0,"platform landing")
player={x=100,y=52,w=8,h=8} vy=0 frame() assert(state=="win","goal unreachable")
start() player.x=62 player.y=120 vy=2 frame() frame() frame() assert(state=="over","gap fall")
start() assert(player.x==12 and grounded and vy==0,"reset")
-- Bounded random-input updates exercise drawing and collision paths.
for i=1,2000 do
 frame({[0]=rnd(2)<1,[1]=rnd(2)<1,[4]=rnd(3)<1})
 assert(player.x>=0 and player.x<=120,"screen boundary")
 if state~="play" then start() end
end
''',
'sokoban': '''
_init() assert(state=="play")
step(1,0) step(1,0) assert(box_at(5,3) and px==4 and moves==2)
step(-1,0) step(-1,0) step(0,1) step(1,0) step(1,0)
assert(state=="win" and finished(),"unreachable box goals")
undo() assert(state=="play" and box_at(4,4) and moves==6,"undo win")
reset_round() px=1 py=1
step(-1,0) assert(px==1 and moves==0 and #history==0,"blocked move changed history")
boxes={{x=2,y=1},{x=3,y=1}} step(1,0)
assert(px==1 and box_at(2,1) and moves==0,"blocked push mutated boxes")
reset_round()
for i=1,100 do step(-1,0) step(1,0) end
assert(#history==64,"undo history unbounded")
frame({[4]=true}) assert(moves==0)
step(-1,0) frame({[4]=true}) assert(moves==1,"held reset repeated")
frame() frame({[5]=true}) assert(moves==0 and px==2,"undo key")
-- The actual shared helper must repeat by short countdown, not btnp defaults.
grid_input_reset() keys={[1]=true}
local requested={}
for i=1,13 do if grid_request(6,3)==1 then add(requested,i) end end
assert(#requested==4 and requested[1]==1 and requested[2]==7 and requested[3]==10 and requested[4]==13,"uneven repeat cadence")
keys={} assert(grid_request(6,3)==-1)
keys={[1]=true} assert(grid_request(6,3)==1,"released key did not reset")
keys={[1]=true,[2]=true} assert(grid_request(6,3)==2,"new direction did not take priority")
keys={[0]=true,[1]=true} assert(grid_request(6,3)==-1,"opposites must cancel")
keys={[0]=true,[1]=true,[3]=true} assert(grid_request(6,3)==3,"valid other axis lost")
grid_input_reset() keys={[1]=true} local count=0
for block=1,40 do for i=1,1000 do if grid_request(6,3)==1 then count+=1 end end end
assert(count==13333,"long held input overflowed or drifted")
reset_round() held={} local sf=#sound_calls
frame({[1]=true}) assert(px==3 and moves==1 and #sound_calls==sf+1,"first action/sound")
for i=1,5 do frame({[1]=true}) end
assert(px==3 and moves==1 and #sound_calls==sf+1,"render frames retriggered action/sound")
frame({[1]=true}) assert(px==4 and moves==2 and #sound_calls==sf+2,"first repeat action/sound")
frame({[4]=true}) local mc=#music_calls
for i=1,30 do frame({[4]=true}) end
assert(moves==0 and #music_calls==mc,"held restart retriggered music")
''',
'raycaster': '''
_init() start()
local depth,side=cast(1,0) near(depth,2.5) assert(side==0,"DDA depth")
depth,side=cast(0,1) near(depth,4.5) assert(side==1,"zero axis")
assert(wall(-1,1) and wall(8,2) and not free(0.9,1.5),"closed map")
for i=1,120 do frame({[2]=true}) end
assert(px<3.86 and py==1.5,"walked through wall")
local previous=angle frame({[0]=true}) assert(angle~=previous,"turn")
for i=1,2000 do
 frame({[0]=rnd(2)<1,[2]=rnd(2)<1})
 assert(free(px,py) and angle>=0 and angle<1,"world bounds")
end
-- Flood fill proves that the small exit is connected to the start.
local seen={} local queue={{x=1,y=1}} local head=1 local found=false
while head<=#queue do
 local p=queue[head] head=head+1
 local key=p.x+p.y*8
 if not seen[key] and not wall(p.x,p.y) then
  seen[key]=true if p.x==6 and p.y==6 then found=true end
  for d in all({{1,0},{-1,0},{0,1},{0,-1}}) do
   if not wall(p.x+d[1],p.y+d[2]) then add(queue,{x=p.x+d[1],y=p.y+d[2]}) end
  end
 end
end
assert(found,"exit disconnected")
px=6.5 py=6.5 frame() assert(state=="win")
start() near(px,1.5) near(py,1.5)
''',
'mesh3d': '''
_init() start()
lines={}
edge3d({x=0,y=0,z=0},{x=0,y=0,z=0.1},8)
assert(#lines==0,"behind near plane drawn")
edge3d({x=0,y=0,z=0},{x=0.1,y=0,z=1},8)
assert(#lines==1,"crossing near plane rejected")
for i=1,4 do assert(abs(lines[1][i])<1000,"unbounded projection") end
cubes={{x=0,y=0,z=0.51},{x=0,y=0,z=0.51}} spawn=35
frame() assert(lives==2 and invul==30 and #cubes==0,"multiple hits")
lives=1 invul=0 cubes={{x=0,y=0,z=0.51}} frame() assert(state=="over")
start() assert(lives==3 and invul==0 and seconds==0 and #cubes<=1,"reset")
for i=1,150 do frame({[1]=true,[2]=true}) end
assert(px==1 and py==1 and #cubes<=6,"world or geometry bound")
reset_round() spawn=35 cubes={}
for i=1,900 do cubes={} spawn=35 frame() end
assert(state=="win" and seconds==30,"survival target")
start() assert(state=="play" and seconds==0)
'''

}
DRIVER = '''
#include <cstdio>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
static int budget=0;
static void bound(lua_State* L,lua_Debug*) { if(++budget>5000) luaL_error(L,"example instruction budget exceeded"); }
int main(int argc,char** argv){
 lua_State* L=luaL_newstate(); luaL_openlibs(L); lua_sethook(L,bound,LUA_MASKCOUNT,10000);
 for(int i=1;i<argc;i++)if(luaL_loadfile(L,argv[i])||lua_pcall(L,0,0,0)){
  fprintf(stderr,"%s: %s\\n",argv[i],lua_tostring(L,-1));lua_close(L);return 1;
 }
 lua_close(L);return 0;
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/'build/workshop-fake08/libs/z8lua')
    args=parser.parse_args()
    version=subprocess.run(['git','-C',str(args.source),'rev-parse','HEAD'],capture_output=True,text=True,check=True).stdout.strip()
    if version!=PIN: raise SystemExit('Use the workshop pinned z8lua source: '+PIN)
    catalog=json.loads((SKILL/'assets/examples/catalog.json').read_text())
    with tempfile.TemporaryDirectory(prefix='workshop-skill-examples-') as temporary:
        temp=Path(temporary);driver=temp/'driver.cpp';driver.write_text(DRIVER)
        binary=temp/'z8lua-verify'
        subprocess.run(['clang++','-std=c++17','-O1','-Wno-deprecated','-I'+str(args.source),str(driver),*[str(args.source/(s+'.c')) for s in CORE],'-o',str(binary)],check=True,capture_output=True,timeout=120)
        prelude=temp/'p8-api-mocks.lua';prelude.write_text(PRELUDE)
        for e in catalog['examples']:
            source=SKILL/e['path'];check_lua(source.read_text())
            checks=temp/(e['id']+'-checks.lua');checks.write_text(TESTS[e['id']]+'\nlocal before=#music_calls _draw() _draw() assert(#music_calls==before,"draw restarts music")\nassert(#music_calls>0,"example has no music")\n')
            result=subprocess.run([str(binary),str(prelude),str(source),str(checks)],capture_output=True,text=True,timeout=15)
            if result.returncode: raise SystemExit(result.stderr)
            print('PASS:',e['id'],'— actual pinned z8lua syntax/fixed-point; state, input, collision and restart scenarios; mocked graphics/input.')
    print('Development checks only; not automated gameplay validation of generated user games.')

if __name__=='__main__': main()
