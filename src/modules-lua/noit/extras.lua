-- Copyright (c) 2012, OmniTI Computer Consulting, Inc.
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

local string = require("string")
local table = require("table")
module("noit.extras")

-- from http://www.wellho.net/resources/ex.php4?item=u108/split
-- modified to split up to 'max' times
function split(str, delimiter, max)
  local result = { }
  local from  = 1
  local delim_from, delim_to = string.find( str, delimiter, from  )
  local nb = 0
  if max == nil then
    max = 0
  end
  while delim_from do
    local insert_string = string.sub( str, from , delim_from-1 )
    nb = nb + 1
    table.insert( result, insert_string )
    from  = delim_to + 1
    delim_from, delim_to = string.find( str, delimiter, from  )
    if nb == max then
      break
    end
  end
  local last_res = string.sub (str, from)
  if last_res ~= nil and last_res ~= "" then
    table.insert( result, last_res )
  end
  return result
end

