-- Workshop helper, adapted around the DAS/ARR responsibilities in grid-turns.
-- Not an upstream excerpt. Call once per _update (30hz).
function grid_input_reset()
 grid_prev={}
 grid_opposite={1,0,3,2}
 grid_dir=-1
 grid_wait=0
end
function grid_request(delay,interval)
 local fresh=-1
 local fallback=-1
 for i=0,3 do
  local down=btn(i)
  local valid=down and not btn(grid_opposite[i+1])
  if valid then
   if down and not grid_prev[i] and fresh<0 then fresh=i end
   if fallback<0 then fallback=i end
  end
  grid_prev[i]=down
 end
 local d=fresh
 if d<0 and grid_dir>=0 and btn(grid_dir) and not btn(grid_opposite[grid_dir+1]) then d=grid_dir end
 if d<0 then d=fallback end
 if d<0 then grid_dir=-1 grid_wait=0 return -1 end
 if d!=grid_dir then
  grid_dir=d
  grid_wait=max(1,delay or 6)
  return d
 end
 grid_wait-=1
 if grid_wait<=0 then
  grid_wait=max(1,interval or 3)
  return d
 end
 return -1
end

function finish_round(won)
 state=won and "win" or "over"
 music(-1,200)
 if won then sfx(3,3) else sfx(4,3) end
end
-- BOX GOALS: adaptation of grid/crate ideas from Woj-Rak's MIT framework.
-- Upstream source, revision and license: assets/community/sources.json.
function box_at(x,y)
 for i=1,#boxes do
  if boxes[i].x==x and boxes[i].y==y then return i end
 end
end
function wall(x,y)
 if x<0 or y<0 or x>=8 or y>=8 then return true end
 return board[y+1][x+1]==1
end
function reset_round()
 board={{1,1,1,1,1,1,1,1},{1,0,0,0,0,0,0,1},
  {1,0,0,0,1,0,0,1},{1,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,1},{1,0,0,0,1,0,0,1},
  {1,0,0,0,0,0,0,1},{1,1,1,1,1,1,1,1}}
 boxes={{x=3,y=3},{x=3,y=4}}
 goals={{x=5,y=3},{x=5,y=4}}
 px=2 py=3 moves=0
 history={}
 grid_input_reset()
 state="play"
 sfx(-1,3)
 music(6,200,3)
end
function _init()
 held={}
 reset_round()
end
function finished()
 for g in all(goals) do
  if not box_at(g.x,g.y) then return false end
 end
 return true
end
function step(dx,dy)
 local x=px+dx local y=py+dy
 if wall(x,y) then return end
 local id=box_at(x,y)
 if id and (wall(x+dx,y+dy) or box_at(x+dx,y+dy)) then return end
 local old={x=px,y=py,moves=moves,boxes={}}
 for b in all(boxes) do add(old.boxes,{x=b.x,y=b.y}) end
 add(history,old)
 if #history>64 then deli(history,1) end
 if id then boxes[id].x=x+dx boxes[id].y=y+dy end
 sfx(5,3)
 px=x py=y moves=min(30000,moves+1)
 if finished() then finish_round(true) end
end
function undo()
 if #history==0 then return end
 local ended=state=="win"
 local old=deli(history,#history)
 px=old.x py=old.y moves=old.moves boxes=old.boxes
 state="play"
 if ended then music(6,200,3) end
end
function _update()
 local direction=grid_request(6,3)
 local action=-1
 for i=4,5 do
  local down=btn(i)
  if down and not held[i] and action==-1 then action=i end
  held[i]=down
 end
 if action==4 then reset_round() return end
 if action==5 then undo() return end
 if state~="play" then return end
 if direction==0 then step(-1,0)
 elseif direction==1 then step(1,0)
 elseif direction==2 then step(0,-1)
 elseif direction==3 then step(0,1) end
end
function _draw()
 cls(1)
 print("moves "..moves,16,2,7)
 print("o:retry x:undo",16,12,6)
 for y=0,7 do for x=0,7 do
  if board[y+1][x+1]==1 then
   rectfill(16+x*12,24+y*12,27+x*12,35+y*12,5)
  end
 end end
 for g in all(goals) do spr(11,18+g.x*12,26+g.y*12) end
 for b in all(boxes) do
  local x=18+b.x*12 local y=26+b.y*12
  rectfill(x,y,x+7,y+7,9) rect(x,y,x+7,y+7,4)
 end
 spr(4,18+px*12,26+py*12)
 if state=="win" then
  rectfill(20,47,107,76,1)
  print("all boxes home",36,53,11)
  print("o:retry x:undo",30,65,7)
 end
end
