function finish_round(won)
 state=won and "win" or "over"
 music(-1,200)
 if won then sfx(3,3) else sfx(4,3) end
end
-- PLATFORM GOAL: separate-axis collision; O is an edge-triggered jump
function overlap(a,b)
 return a.x<b.x+b.w and b.x<a.x+a.w
  and a.y<b.y+b.h and b.y<a.y+a.h
end
function reset_round()
 player={x=12,y=112,w=8,h=8}
 vy=0
 grounded=false
 platforms={{x=0,y=120,w=56,h=8},{x=72,y=120,w=56,h=8},
  {x=28,y=100,w=24,h=6},
  {x=58,y=80,w=24,h=6},{x=88,y=60,w=24,h=6}}
 goal={x=100,y=52,w=8,h=8}
 state="play"
 sfx(-1,3)
 music(6,200,3)
end
function _init()
 old_o=false
 reset_round()
 state="title"
end
function move_x(dx)
 player.x=mid(0,player.x+dx,120)
 for p in all(platforms) do
  if overlap(player,p) then
   if dx>0 then player.x=p.x-player.w
   elseif dx<0 then player.x=p.x+p.w end
  end
 end
end
function move_y()
 vy=min(3,vy+0.2)
 player.y=player.y+vy
 grounded=false
 for p in all(platforms) do
  if overlap(player,p) then
   if vy>0 then player.y=p.y-player.h grounded=true
   elseif vy<0 then player.y=p.y+p.h end
   vy=0
  end
 end
end
function _update()
 local o=btn(4)
 local press=o and not old_o
 old_o=o
 if state~="play" then
  if press then reset_round() end
  return
 end
 if press and grounded then vy=-3.5 sfx(1,3) end
 local dx=0
 if btn(0) then dx=dx-1.4 end
 if btn(1) then dx=dx+1.4 end
 move_x(dx)
 move_y()
 if player.y>127 then finish_round(false) end
 if overlap(player,goal) then finish_round(true) end
end
function _draw()
 cls(1)
 print("left/right:move o:jump",2,2,7)
 for p in all(platforms) do
  rectfill(p.x,p.y,p.x+p.w-1,p.y+p.h-1,5)
  line(p.x,p.y,p.x+p.w-1,p.y,11)
 end
 spr(11,goal.x,goal.y)
 spr(4,player.x,player.y)
 if state~="play" then
  rectfill(10,30,117,51,1)
  if state=="win" then print("goal! o:retry",30,36,11)
  elseif state=="over" then print("fell! o:retry",30,36,8)
  else print("o:start",48,36,10) end
 end
end
