function finish_round(won)
 state=won and "win" or "over"
 music(-1,200)
 if won then sfx(3,3) else sfx(4,3) end
end
-- GRID SNAKE: input every frame, movement once per tick
function occupied(x,y,limit)
 for i=1,limit or #body do
  if body[i].x==x and body[i].y==y then return true end
 end
 return false
end
function place_food()
 local empty={}
 for y=0,11 do
  for x=0,11 do
   if not occupied(x,y) then add(empty,{x=x,y=y}) end
  end
 end
 if #empty==0 then finish_round(true) return end
 food=empty[1+flr(rnd(#empty))]
end
function reset_round()
 body={{x=5,y=5},{x=4,y=5},{x=3,y=5}}
 dx=1
 dy=0
 turned=false
 tick=0
 score=0
 state="play"
 sfx(-1,3)
 music(6,200,3)
 place_food()
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
 if not turned then
  local nx=dx
  local ny=dy
  if btn(0) then nx=-1 ny=0
  elseif btn(1) then nx=1 ny=0
  elseif btn(2) then nx=0 ny=-1
  elseif btn(3) then nx=0 ny=1 end
  if (nx~=dx or ny~=dy) and (nx~=-dx or ny~=-dy) then
   dx=nx dy=ny turned=true
  end
 end
 tick=tick+1
 if tick<6 then return end
 tick=0
 turned=false
 local x=body[1].x+dx
 local y=body[1].y+dy
 local eat=x==food.x and y==food.y
 local limit=#body
 if not eat then limit=limit-1 end
 if x<0 or x>=12 or y<0 or y>=12 or occupied(x,y,limit) then
  finish_round(false) return
 end
 local next_body={{x=x,y=y}}
 for i=1,limit do add(next_body,body[i]) end
 body=next_body
 if eat then score=score+1 sfx(0,3) place_food() end
end
function _draw()
 cls(1)
 print("score "..score,16,2,7)
 rect(14,22,113,121,5)
 for i=1,#body do
  local c=11
  if i==1 then c=10 end
  local x=16+body[i].x*8
  local y=24+body[i].y*8
  rectfill(x,y,x+6,y+6,c)
 end
 if state~="win" then spr(3,16+food.x*8,24+food.y*8) end
 if state~="play" then
  rectfill(16,48,111,90,1)
  if state=="win" then print("board cleared!",34,55,11)
  elseif state=="over" then print("game over",40,55,8)
  else print("grid snake",36,55,10) end
  print("arrows:turn",34,68,7)
  print("o:start/retry",30,80,7)
 end
end
