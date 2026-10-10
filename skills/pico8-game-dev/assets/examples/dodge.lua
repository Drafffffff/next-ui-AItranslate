function finish_round(won)
 state=won and "win" or "over"
 music(-1,200)
 if won then sfx(3,3) else sfx(4,3) end
end
-- THREE-LIFE DODGE: bounded objects and bounded survival counters
function overlap(a,b)
 return a.x<b.x+b.w and b.x<a.x+a.w
  and a.y<b.y+b.h and b.y<a.y+a.h
end
function reset_round()
 player={x=60,y=104,w=8,h=8}
 hazards={}
 lives=3
 invul=0
 score=0
 tick=0
 spawn=30
 state="play"
 sfx(-1,3)
 music(0,200,7)
end
function _init()
 best=0
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
 if invul>0 then invul=invul-1 end
 if btn(0) then player.x=player.x-1.5 end
 if btn(1) then player.x=player.x+1.5 end
 player.x=mid(4,player.x,116)
 tick=tick+1
 if tick>=30 then
  tick=0
  score=min(30000,score+1)
 end
 local level=min(6,flr(score/10))
 spawn=spawn-1
 if spawn<=0 then
  spawn=max(15,36-level*3)
  if #hazards<12 then
   add(hazards,{x=4+flr(rnd(113)),y=16,w=8,h=8,
    warning=18,speed=0.8+level*0.15})
  end
 end
 local hurt=false
 for i=#hazards,1,-1 do
  local h=hazards[i]
  if h.warning>0 then h.warning=h.warning-1
  else h.y=h.y+h.speed end
  if h.y>127 then deli(hazards,i)
  elseif h.warning==0 and not hurt and invul==0 and overlap(player,h) then
   lives=lives-1 sfx(2,3)
   invul=45
   hurt=true
   deli(hazards,i)
  end
 end
 if lives<=0 then
  best=max(best,score)
  finish_round(false)
 end
end
function _draw()
 cls(0)
 rect(2,14,125,126,1)
 for h in all(hazards) do
  if h.warning>0 then
   if h.warning%6<3 then rectfill(h.x,16,h.x+7,18,10) end
  else spr(5,h.x,h.y) end
 end
 if invul==0 or invul%6<3 then spr(4,player.x,player.y) end
 print("score "..score,2,2,7)
 for i=1,lives do spr(7,120-i*9,2) end
 if state~="play" then
  rectfill(12,44,115,88,0)
  if state=="title" then print("block dodge",36,50,10)
  else print("game over",40,50,8) end
  print("best "..best,42,60,7)
  print("left/right:move",24,70,7)
  print("o:start/retry",30,80,11)
 end
end
