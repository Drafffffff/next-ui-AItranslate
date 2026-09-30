-- title: Brick Material Lab
-- author: next-ui-AItranslate contributors
-- desc: A handheld cellular sandbox for the TIC-80 port
-- script: lua
-- license: MIT
W=120 H=62 grid={} life={} cx=60 cy=12 mat=1 brush=2 paused=false tick=0
names={"SAND","WATER","STONE","FIRE"}
colors={4,9,13,2}
function reset()
 grid={} life={}
 for y=0,H-1 do for x=0,W-1 do grid[y*W+x]=0 end end
 for x=0,W-1 do grid[(H-1)*W+x]=3 end
 for x=20,100 do grid[48*W+x]=3 end
 for y=4,20 do for x=28,42 do grid[y*W+x]=1 end end
 for y=4,20 do for x=70,87 do grid[y*W+x]=2 end end
end
function move(i,x,y)
 if x<0 or x>=W or y<0 or y>=H then return false end
 local j=y*W+x local a=grid[i] local b=grid[j]
 if b==0 or (a==1 and b==2) then grid[i]=b grid[j]=a return true end
 return false
end
function physics()
 for y=H-2,0,-1 do
  local d=tick%4==0 and 1 or -1
  for k=0,W-1 do
   local x=d==1 and k or W-1-k local i=y*W+x local c=grid[i]
   if c==1 or c==2 then
    if not move(i,x,y+1) and not move(i,x+d,y+1) and not move(i,x-d,y+1) and c==2 then
     if not move(i,x+d,y) then move(i,x-d,y) end
    end
   elseif c==4 then
    life[i]=(life[i] or 25)-1
    if life[i]<=0 or (y>0 and grid[i-W]==2) then grid[i]=0 life[i]=nil
    elseif y>0 and grid[i-W]==0 and tick%4==0 then grid[i-W]=4 life[i-W]=life[i] grid[i]=0 life[i]=nil end
   end
  end
 end
end
function TIC()
 tick=tick+1
 if btn(0) then cy=math.max(0,cy-1) end
 if btn(1) then cy=math.min(H-2,cy+1) end
 if btn(2) then cx=math.max(0,cx-1) end
 if btn(3) then cx=math.min(W-1,cx+1) end
 if btnp(6) then mat=mat%4+1 end
 if btnp(7) then brush=brush%4+1 end
 if keyp(48) then paused=not paused end
 if keyp(18) then reset() end
 if btn(4) or btn(5) then
  for y=math.max(0,cy-brush),math.min(H-2,cy+brush) do
   for x=math.max(0,cx-brush),math.min(W-1,cx+brush) do
    if (x-cx)^2+(y-cy)^2<=brush^2 then local i=y*W+x grid[i]=btn(5) and 0 or mat life[i]=25 end
   end
  end
 end
 if not paused and tick%2==0 then physics() end
 cls(0)
 for y=0,H-1 do for x=0,W-1 do local c=grid[y*W+x] if c>0 then rect(x*2,y*2+12,2,2,colors[c]) end end end
 print(names[mat].."  A DRAW B ERASE X TYPE Y SIZE",2,1,12,false,1,true)
 print(paused and "PAUSE" or "LIVE",211,7,paused and 2 or 5,false,1,true)
 circb(cx*2+1,cy*2+13,brush*2+2,12)
end
reset()

-- <PALETTE>
-- 000:1a1c2c5d275db13e53ef7d57ffcd75a7f07038b76425717929366f3b5dc941a6f673eff7f4f4f494b0c2566c86333c57
-- </PALETTE>
