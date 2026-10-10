function finish_round(won)
 state=won and "win" or "over"
 music(-1,200)
 if won then sfx(3,3) else sfx(4,3) end
end
-- CUBE TUNNEL: real 3D vertices, perspective and near-plane line clipping.
-- Original workshop adaptation; community geometry references are documented.
function reset_round()
 px=0 py=0
 cubes={}
 lives=3 invul=0
 tick=0 seconds=0 spawn=1
 state="play"
 sfx(-1,3)
 music(0,200,7)
end
function _init()
 edges={{1,2},{2,3},{3,4},{4,1},{5,6},{6,7},{7,8},{8,5},
  {1,5},{2,6},{3,7},{4,8}}
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
 if btn(0) then px=px-0.035 end
 if btn(1) then px=px+0.035 end
 if btn(2) then py=py+0.035 end
 if btn(3) then py=py-0.035 end
 px=mid(-1,px,1) py=mid(-1,py,1)
 if invul>0 then invul=invul-1 end
 tick=tick+1
 if tick>=30 then
  tick=0 seconds=seconds+1
  if seconds>=30 then finish_round(true) return end
 end
 spawn=spawn-1
 if spawn<=0 then
  spawn=35
  if #cubes<6 then add(cubes,{x=rnd(2)-1,y=rnd(2)-1,z=6}) end
 end
 local hurt=false
 for i=#cubes,1,-1 do
  local b=cubes[i]
  b.z=b.z-0.045
  if b.z<=0.5 then
   if not hurt and invul==0 and abs(b.x-px)<0.3 and abs(b.y-py)<0.3 then
    lives=lives-1 sfx(2,3) invul=30 hurt=true
   end
   deli(cubes,i)
  end
 end
 if lives<=0 then finish_round(false) end
end
function edge3d(a,b,c)
 local ax=a.x local ay=a.y local az=a.z
 local bx=b.x local by=b.y local bz=b.z
 local near=0.2
 if az<near and bz<near then return end
 if az<near then
  local t=(near-az)/(bz-az)
  ax=ax+(bx-ax)*t ay=ay+(by-ay)*t az=near
 elseif bz<near then
  local t=(near-bz)/(az-bz)
  bx=bx+(ax-bx)*t by=by+(ay-by)*t bz=near
 end
 line(64+ax*40/az,70-ay*40/az,64+bx*40/bz,70-by*40/bz,c)
end
function draw_cube(b)
 local x=b.x-px local y=b.y-py local z=b.z
 local s=0.16
 local v={{x=x-s,y=y-s,z=z-s},{x=x+s,y=y-s,z=z-s},
  {x=x+s,y=y+s,z=z-s},{x=x-s,y=y+s,z=z-s},
  {x=x-s,y=y-s,z=z+s},{x=x+s,y=y-s,z=z+s},
  {x=x+s,y=y+s,z=z+s},{x=x-s,y=y+s,z=z+s}}
 for e in all(edges) do edge3d(v[e[1]],v[e[2]],8) end
end
function _draw()
 cls(0)
 clip(0,14,128,114)
 rect(4,16,123,123,1)
 -- New cubes append at the far end; draw far-to-near.
 for i=#cubes,1,-1 do draw_cube(cubes[i]) end
 local c=11
 if invul>0 then c=10 end
 line(60,70,68,70,c) line(64,66,64,74,c)
 clip()
 print("survive "..seconds.."/30s",2,2,7)
 for i=1,lives do spr(7,120-i*9,2) end
 if state~="play" then
  rectfill(10,35,117,89,0)
  if state=="win" then print("tunnel clear!",36,43,11)
  elseif state=="over" then print("game over",40,43,8)
  else print("cube tunnel",38,43,10) end
  print("arrows:move viewpoint",16,57,7)
  print("dodge the red cubes",22,68,7)
  print("o:start/retry",30,80,7)
 end
end
