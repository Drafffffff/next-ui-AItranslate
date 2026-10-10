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
