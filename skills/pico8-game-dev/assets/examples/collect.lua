function finish_round(won)
 state=won and "win" or "over"
 music(-1,200)
 if won then sfx(3,3) else sfx(4,3) end
end
-- CAT COLLECT: collect 10 fish in 30 seconds
function overlap(a,b)
 return a.x<b.x+b.w and b.x<a.x+a.w
  and a.y<b.y+b.h and b.y<a.y+a.h
end
function place_fish()
 for i=1,16 do
  local f={x=4+flr(rnd(112)),y=20+flr(rnd(100)),w=8,h=8}
  if not overlap(player,f) then fish=f return end
 end
 -- Guaranteed safe fallback: choose the corner opposite the player.
 fish={x=4,y=20,w=8,h=8}
 if player.x<60 then fish.x=116 end
 if player.y<64 then fish.y=116 end
end
function reset_round()
 player={x=60,y=64,w=8,h=8}
 score=0
 left=30
 tick=0
 place_fish()
 state="play"
 sfx(-1,3)
 music(6,200,3)
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
 local dx=0
 local dy=0
 if btn(0) then dx=dx-1 end
 if btn(1) then dx=dx+1 end
 if btn(2) then dy=dy-1 end
 if btn(3) then dy=dy+1 end
 if dx~=0 and dy~=0 then dx=dx*0.7071 dy=dy*0.7071 end
 player.x=mid(4,player.x+dx*1.5,116)
 player.y=mid(20,player.y+dy*1.5,116)
 if overlap(player,fish) then
  score=score+1
  sfx(0,3)
  if score>=10 then finish_round(true) return end
  place_fish()
 end
 tick=tick+1
 if tick>=30 then
  tick=0
  left=left-1
  if left<=0 then finish_round(false) end
 end
end
function _draw()
 cls(1)
 rect(2,18,125,126,5)
 spr(1,player.x,player.y)
 spr(3,fish.x,fish.y)
 print("fish "..score.."/10",2,2,7)
 print("time "..left,80,2,7)
 if state~="play" then
  rectfill(10,43,117,84,1)
  if state=="title" then print("cat collect",36,49,10)
  elseif state=="win" then print("you win!",40,49,11)
  else print("time up!",40,49,8) end
  print("arrows:move",34,61,7)
  print("o:start/retry",30,73,7)
 end
end
