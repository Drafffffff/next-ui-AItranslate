// Usable host-created foundation; the Agent adapts its mechanics to the user's idea.
export const STARTER=`state="title"
function reset()
 player={x=60,y=62,speed=2}
 ticks=0
end
function _init()
 reset()
 music(6,0,3)
end
function _update()
 if state=="title" then
  if btnp(4) then reset() state="play" sfx(0,3) end
 elseif state=="play" then
  ticks+=1
  local dx=(btn(1) and 1 or 0)-(btn(0) and 1 or 0)
  local dy=(btn(3) and 1 or 0)-(btn(2) and 1 or 0)
  if dx!=0 and dy!=0 then dx*=0.707 dy*=0.707 end
  player.x=mid(8,player.x+dx*player.speed,112)
  player.y=mid(20,player.y+dy*player.speed,104)
  if btnp(5) then state="over" sfx(4,3) end
 else
  if btnp(4) then reset() state="play" sfx(0,3) end
  if btnp(5) then state="title" end
 end
end
function _draw()
 cls(1)
 if state=="title" then
  rect(8,8,119,119,13)
  spr(4,60,32)
  print("your next game",36,56,7)
  print("press o to start",32,80,10)
 elseif state=="play" then
  rect(4,16,123,115,13)
  spr(4,player.x,player.y)
  print("sandbox",4,4,7)
  print("arrows:move x:end",24,120,6)
 else
  print("ready for your idea",24,44,7)
  print("o:restart x:title",28,76,10)
 end
end
`;
