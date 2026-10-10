function finish_round(won)
 state=won and "win" or "over"
 music(-1,200)
 if won then sfx(3,3) else sfx(4,3) end
end
-- MAZE EXIT: compact DDA raycaster, based on community raycasting patterns.
-- Reference: M2tias/pico8-raycaster (MIT); original cart retained separately.
function wall(x,y)
 x=flr(x) y=flr(y)
 if x<0 or y<0 or x>=8 or y>=8 then return true end
 return grid[y+1][x+1]==1
end
function free(x,y)
 local r=0.15
 return not wall(x-r,y-r) and not wall(x+r,y-r)
  and not wall(x-r,y+r) and not wall(x+r,y+r)
end
function reset_round()
 grid={{1,1,1,1,1,1,1,1},{1,0,0,0,1,0,0,1},
  {1,0,1,0,1,0,1,1},{1,0,1,0,0,0,0,1},
  {1,0,1,1,1,1,0,1},{1,0,0,0,0,1,0,1},
  {1,1,1,1,0,0,0,1},{1,1,1,1,1,1,1,1}}
 px=1.5 py=1.5 angle=0
 state="play"
 sfx(-1,3)
 music(0,200,7)
end
function _init()
 old_o=false
 reset_round()
 state="title"
end
function _update()
 local o=btn(4)
 local press=o and not old_o
 old_o=o
 if state~="play" then
  if press then reset_round() end
  return
 end
 if btn(0) then angle=(angle+0.012)%1 end
 if btn(1) then angle=(angle-0.012)%1 end
 local speed=0
 if btn(2) then speed=speed+0.05 end
 if btn(3) then speed=speed-0.05 end
 local dx=cos(angle)*speed
 local dy=sin(angle)*speed
 if free(px+dx,py) then px=px+dx end
 if free(px,py+dy) then py=py+dy end
 if flr(px)==6 and flr(py)==6 then finish_round(true) end
end
function cast(rx,ry)
 local mx=flr(px) local my=flr(py)
 local ddx=1/max(0.005,abs(rx))
 local ddy=1/max(0.005,abs(ry))
 local sx=1 local sy=1
 local tx=(mx+1-px)*ddx
 local ty=(my+1-py)*ddy
 if rx<0 then sx=-1 tx=(px-mx)*ddx end
 if ry<0 then sy=-1 ty=(py-my)*ddy end
 for step=1,16 do
  local side=0
  local depth=0
  if tx<ty then mx=mx+sx depth=tx tx=tx+ddx
  else my=my+sy depth=ty ty=ty+ddy side=1 end
  if wall(mx,my) then return max(0.1,depth),side end
 end
 return 8,0
end
function _draw()
 cls(0)
 rectfill(0,16,127,59,1)
 rectfill(0,60,127,103,5)
 local dx=cos(angle) local dy=sin(angle)
 for column=0,31 do
  local camera_x=(column+0.5)/16-1
  local depth,side=cast(dx-dy*0.66*camera_x,dy+dx*0.66*camera_x)
  local h=min(88,64/depth)
  local c=6
  if side==1 then c=5 end
  if depth>4 then c=1 end
  rectfill(column*4,max(16,60-h/2),column*4+3,min(103,60+h/2),c)
 end
 print("arrows:move/turn",2,2,7)
 for y=0,7 do for x=0,7 do
  local c=1
  if grid[y+1][x+1]==1 then c=6 end
  rectfill(4+x*2,108+y*2,5+x*2,109+y*2,c)
 end end
 pset(16,120,11)
 pset(4+px*2,108+py*2,10)
 print("exit:green dot",26,112,11)
 if state~="play" then
  rectfill(8,36,119,88,0)
  if state=="win" then print("maze cleared!",36,44,11)
  else print("maze exit",44,44,10) end
  print("left/right:turn",24,57,7)
  print("up/down:walk",30,67,7)
  print("o:start/retry",30,78,7)
 end
end
