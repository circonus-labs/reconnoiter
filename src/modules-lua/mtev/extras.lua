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

local ipairs = ipairs
local pairs = pairs
local type = type
local tonumber = tonumber
local string = require("string")
local table = require("table")
local unpack = table.unpack or unpack
local _G = _G

module("mtev.extras")

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

function iptonumber(str)
  local num = 0
  for elem in str:gmatch("%d+") do
    num = (num * 256) + elem
  end
  return num
end

function check_host_header_against_certificate(host_header, cert_subject, san_list)
  local san_list_check = function (array, value)
    for i, line in ipairs(array) do
      if line == value then
        return true
      else
        line = string.gsub(line, '%.', "%%.")
        line = string.gsub(line, "%*", "[^.]*")
        local match = string.match(value, line)
        if match == value then
          return true
        end
      end
    end
    return false
  end
  -- First, check for SAN values if they exist - if they do, check for a match
  local san_array = { }
  if san_list ~= nil then
    san_array = split(san_list, ", ")
  end
  if san_list_check(san_array, host_header) then
    -- The host header was in the SAN list, so we're done
    return nil
  end
  -- Next, pull out the CN value
  local cn = string.sub(cert_subject, string.find(cert_subject, 'CN=[^/\n]*'))
  if cn == nil or cn == '' then
    -- no common name given, give an error
    return 'CN not found in certificate'
  end
  cn = string.sub(cn, 4)
  if cn == host_header then
    -- CN and host_header match exactly, so no error
    return nil
  end
  cn = string.gsub(cn, '%.', "%%.")
  cn = string.gsub(cn, "%*", "[^.]*")
  local match = string.match(host_header, cn)
  if match == host_header then
    return nil
  end
  return 'host header does not match CN or SANs in certificate'
end

function decode_form(req, desired_key)
  if string.find(req:headers("content-type") or "",
                 "application/x-www-form-urlencoded", 1, true) == nil then
    return nil
  end
  local p = req:payload()
  local form = {}
  if(p ~= nil) then
    for i, item in pairs(split(p,'&')) do
      local parts = split(item, '=', 2)
      local key = parts[1]:gsub("%%(%x%x)", function(s) return string.char(tonumber(s,16)) end)
      local val = parts[2]
      if val == nil then form[key] = true
      else
        val = val:gsub("%%(%x%x)", function(s) return string.char(tonumber(s,16)) end)
        form[key] = val
      end
    end
  end
  if desired_key ~= nil then return form[desired_key] end
  return form
end

function decode_cookie(req, desired_key)
  local c = req:headers('cookie')
  local jar = {}
  if(c ~= nil) then
    for i, item in pairs(split(c,'; ')) do
      local parts = split(item, '=', 2)
      local key = parts[1]:gsub("%%(%x%x)", function(s) return string.char(tonumber(s,16)) end)
      local val = parts[2]
      if val == nil then jar[key] = true
      else
        val = val:gsub("%%(%x%x)", function(s) return string.char(tonumber(s,16)) end)
        jar[key] = val
      end
    end
  end
  if desired_key ~= nil then return jar[desired_key] end
  return jar
end

function url_encode(str, str2)
  return str:gsub("([^-_%.~A-Za-z0-9])", function(c) return string.format("%%%02x", string.byte(c)) end)
end
function url_decode(str, str2)
  return str:gsub("%%(%x%x)", function(s) return string.char(tonumber(s,16)) end)
end

function set_cookie(http, key, value, attrs)
  if(attrs == nil) then attrs = {} end
  local lattrs = {}
  for k,v in pairs(attrs) do lattrs[string.lower(k)] = v end
  local urlkey = key:gsub("([^-%w])", function(c) return string.format("%%%02x", string.byte(c)) end)
  local urlvalue = value:gsub("([^-%w])", function(c) return string.format("%%%02x", string.byte(c)) end)
  local hdrval = urlkey .. "=" .. urlvalue
  if(lattrs['expires']) then
    hdrval = hdrval .. "; Expires=" .. os.date("%a, %d-%b-%Y %H:%M:%S GMT", os.time() + lattrs['expires'])
  end
  if(lattrs['domain']) then
    hdrval = hdrval .. "; Domain=" .. lattrs['domain']
  end
  if(lattrs['path']) then
    hdrval = hdrval .. "; Path=" .. lattrs['path']
  end
  if(lattrs['secure']) then hdrval = hdrval .. "; Secure" end
  if(lattrs['httponly']) then hdrval = hdrval .. "; HttpOnly" end
  http:header('Set-Cookie', hdrval)
end

function mtev_coros_resume(co,...)
  local rv = {_G.coroutine._resume(co,...)}
  if _G.coroutine.status(co) == "dead" then
    _G.mtev.cancel_coro(co)
  end
  if type(rv) == "table" then return unpack(rv) end
  return rv
end

_G.coroutine._resume = _G.coroutine.resume
_G.coroutine.resume = mtev_coros_resume
