local base = _G
local string = require("string")
module("noit.timeval")

local metat = { }

function new(sec, usec)
  local o = base.setmetatable( { sec = sec, usec = usec }, metat)
  return o
end

function now()
  return new(base.noit.gettimeofday())
end

function metat.__add(o1, o2)
  local secs = o1.sec + o2.sec
  local usecs = o1.usec + o2.usec
  if usecs > 1000000 then
    usecs = usecs - 1000000
    secs = secs + 1
  end
  return new(secs, usecs)
end

function metat.__sub(o1, o2)
  local secs = o1.sec - o2.sec
  local usecs = o1.usec - o2.usec
  if usecs < 0 then
    secs = secs - 1
    usecs = 1000000 + usecs
  end
  if secs < 0 then
    secs = secs + 1
    usecs = usecs - 1000000
  end
  return new(secs, usecs)
end

function metat.__tostring(a)
  return string.format("%f", (1.0 * a.sec) + (a.usec/1000000.0))
end
