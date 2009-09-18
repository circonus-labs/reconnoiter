-- Copyright (c) 2008, OmniTI Computer Consulting, Inc.
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are
-- met:
--
--     * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--     * Redistributions in binary form must reproduce the above
--       copyright notice, this list of conditions and the following
--       disclaimer in the documentation and/or other materials provided
--       with the distribution.
--     * Neither the name OmniTI Computer Consulting, Inc. nor the names
--       of its contributors may be used to endorse or promote products
--       derived from this software without specific prior written
--       permission.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
-- "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
-- LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
-- A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
-- OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
-- SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
-- LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
-- DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
-- THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
-- (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
-- OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

function seconds(self)
  return self.sec + (self.usec / 1000000.0)
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
