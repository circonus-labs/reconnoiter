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
local noit = require("noit_binding")

function Headers()
  local map = {}
  local mt = {
    __index = function(t,k)
      local K = map[string.lower(k)]
      if K ~= nil then
        return rawget(t,K)
      end
      return nil
    end,
    __newindex = function(t,k,v)
      local kl = string.lower(k)
      if map[kl] ~= nil then
         rawset(t,map[kl],nil)
      end
      map[kl] = k
      rawset(t,k,v)
    end
  }
  return setmetatable({},mt)
end

local HttpClient = {};
HttpClient.__index = HttpClient;

function HttpClient:new(hooks)
    local obj = { }
    setmetatable(obj, HttpClient)
    obj.hooks = hooks or { }
    return obj
end

function HttpClient:connect(target, port, ssl, ssl_host, ssl_layer)
    if ssl == nil then ssl = false end
    self.e = mtev.socket(target)
    self.target = target
    self.port = port
    self.truncated = nil
    local rv, err = self.e:connect(self.target, self.port)
    if rv ~= 0 then
        return rv, err
    end
    if self.hooks.connected ~= nil then self.hooks.connected() end
    if ssl == false then return rv, err end
    return self.e:ssl_upgrade_socket(self.hooks.certfile and self.hooks.certfile(),
                                     self.hooks.keyfile and self.hooks.keyfile(),
                                     self.hooks.cachain and self.hooks.cachain(),
                                     self.hooks.ciphers and self.hooks.ciphers(),
                                     ssl_host, ssl_layer)
end

function HttpClient:ssl_ctx()
   return self.e:ssl_ctx()
end

function HttpClient:do_request(method, uri, _headers, payload, http_version)
    local version = http_version or "1.1"
    local headers = Headers()
    for k,v in pairs(_headers) do headers[k] = v end
    self.method = method:lower()
    self.raw_bytes = 0
    self.content_bytes = 0
    local sstr = method .. " " .. uri .. " " .. "HTTP/" ..  version .. "\r\n"
    headers["Content-Length"] = nil
    if payload ~= nil and string.len(payload) > 0 then
      headers["Content-Length"] = string.len(payload)
    end
    headers["Accept-Encoding"] = "gzip, deflate";
    if headers["User-Agent"] == nil then
        headers["User-Agent"] = "Reconnoiter/0.9"
    end
    for header, value in pairs(headers) do
      if value ~= nil then sstr = sstr .. header .. ": " .. value .. "\r\n" end
    end
    sstr = sstr .. "\r\n"
    if payload ~= nil and string.len(payload) > 0 then
      sstr = sstr .. payload
    end
    self.e:write(sstr)
end

function HttpClient:get_headers()
    local lasthdr
    local str = self.e:read("\n");
    local cookie_count = 1;
    if str == nil then error("no response") end
    self.protocol, self.code = string.match(str, "^HTTP/(%d.%d)%s+(%d+)%s+")
    if self.protocol == nil then error("malformed HTTP response") end
    self.code = tonumber(self.code)
    self.headers = Headers()
    self.cookies = {}
    while true do
        local str = self.e:read("\n")
        if str == nil or str == "\r\n" or str == "\n" then break end
        str = string.gsub(str, '%s+$', '')
        local hdr, val = string.match(str, "^([%.-_%a%d]+):%s*(.*)$")
        if hdr == nil then
            if lasthdr == nil then error ("malformed header line") end
            hdr = lasthdr
            val = string.match(str, "^%s+(.+)")
            if val == nil then error("malformed header line") end
            self.headers[hdr] = self.headers[hdr] .. " " .. val
        else
            if string.lower(hdr) == "set-cookie" then
                self.cookies[cookie_count] = val;
                cookie_count = cookie_count + 1;
            else
                self.headers[hdr] = val
            end
            lasthdr = hdr
        end
    end
    if self.hooks.headers ~= nil then self.hooks.headers(self.headers, self.cookies) end
end

function ce_passthru(str) 
    return str
end

function te_none(self)
    self.content_bytes = ''
    if self.hooks.consume ~= nil then self.hooks.consume('') end
end

function te_close(self, content_enc_func)
    local len = 32678
    local str
    repeat
        local str = self.e:read(len)
        if str ~= nil then
            self.raw_bytes = self.raw_bytes + string.len(str)
            local decoded = content_enc_func(str)
            if decoded ~= nil then
                self.content_bytes = self.content_bytes + string.len(decoded)
            end
            if self.hooks.consume ~= nil then self.hooks.consume(decoded) end
        end
    until str == nil or string.len(str) ~= len
end

function te_length(self, content_enc_func, read_limit)
    local len = tonumber(self.headers["Content-Length"])
    if read_limit and read_limit > 0 and len > read_limit then
      len = read_limit
      self.truncated = true
    end
    repeat
        local str = self.e:read(len)
        if str ~= nil then
            self.raw_bytes = self.raw_bytes + string.len(str)
            len = len - string.len(str)
        end
        local decoded = content_enc_func(str)
        if decoded ~= nil then
          self.content_bytes = self.content_bytes + string.len(decoded)
        end
        if self.hooks.consume ~= nil then self.hooks.consume(decoded) end
    until str == nil or len == 0
end

function te_chunked(self, content_enc_func, read_limit)
    while true do
        local str = self.e:read("\n")
        if str == nil then error("bad chunk transfer") end
        local hexlen = string.match(str, "^([0-9a-fA-F]+)")
        if hexlen == nil then error("bad chunk length: " .. str) end
        local len = tonumber(hexlen, 16)
        if len == 0 then 
          if self.hooks.consume ~= nil then self.hooks.consume("") end
          break 
        end
        str = self.e:read(len)
        if string.len(str or "") ~= len then error("short chunked read") end
        self.raw_bytes = self.raw_bytes + string.len(str)
        local decoded = content_enc_func(str)
        if decoded ~= nil then
          self.content_bytes = self.content_bytes + string.len(decoded)
        end
        if self.hooks.consume ~= nil then self.hooks.consume(decoded) end
        if read_limit and read_limit > 0 then
          if self.content_bytes > read_limit then
            self.truncated = true
            return
          end
        end
        -- each chunk ('cept a 0 size one) is followed by a \r\n
        str = self.e:read("\n")
        if str ~= "\r\n" and str ~= "\n" then error("short chunked boundary read") end
    end
    -- read trailers
    while true do
        local str = self.e:read("\n")
        if str == nil then error("bad chunk trailers") end
        if str == "\r\n" or str == "\n" then break end
    end
end

function HttpClient:get_body(read_limit)
    local cefunc = ce_passthru
    if self.method == 'head' then return te_none(self) end
    local ce = self.headers["Content-Encoding"]
    if ce ~= nil then
      local deflater
      if ce == 'gzip' then
        deflater = noit.gunzip()
      elseif ce == 'deflate' then
        deflater = noit.gunzip()
      elseif ce:find(',') then
        local tokens = noit.extras.split(ce, ",")
        for _, token in pairs(tokens) do
          if token:gsub("^%s*(.-)%s*$", "%1") == "gzip" then
            deflater = noit.gunzip()
            break
          elseif token:gsub("^%s*(.-)%s*$", "%1") == "deflate" then
            deflater = noit.gunzip()
            break
          end
        end
      end

      if deflater == nil then
        error("unknown content-encoding: " .. ce)
      end

      cefunc = function(str)
        return deflater(str, read_limit)
      end
    end
    local te = self.headers["Transfer-Encoding"]
    local cl = self.headers["Content-Length"]
    if te ~= nil and te == "chunked" then
        return te_chunked(self, cefunc, read_limit)
    elseif cl ~= nil and tonumber(cl) ~= nil then
        return te_length(self, cefunc, read_limit)
    elseif self.headers["Connection"] == "keep-alive"
       and (self.code == 204 or self.code == 304) then
        return te_none(self)
    end
    return te_close(self, cefunc)
end

function HttpClient:get_response(read_limit)
    self:get_headers()
    return self:get_body(read_limit)
end

function HttpClient:auth_digest(method, uri, user, pass, challenge)
    local c = ', ' .. challenge
    local nc = '00000001'
    local function rand_string(t, l)
        local n = #t
        local o = ''
        while l > 0 do
          o = o .. t[math.random(1,n)]
          l = l - 1
        end
        return o
    end
    local cnonce =
        rand_string({'a','b','c','d','e','f','g','h','i','j','k','l','m',
                     'n','o','p','q','r','s','t','u','v','x','y','z','A',
                     'B','C','D','E','F','G','H','I','J','K','L','M','N',
                     'O','P','Q','R','S','T','U','V','W','X','Y','Z','0',
                     '1','2','3','4','5','6','7','8','9'}, 8)
    local p = {}
    for k,v in string.gmatch(c, ',%s+(%a+)="([^"]+)"') do p[k] = v end
    for k,v in string.gmatch(c, ',%s+(%a+)=([^",][^,]*)') do p[k] = v end

    -- qop can be a list
    for q in string.gmatch(p.qop, '([^,]+)') do
        if q == "auth" then p.qop = "auth" end
    end

    -- calculate H(A1)
    local ha1 = noit.md5_hex(user .. ':' .. p.realm .. ':' .. pass)
    if string.lower(p.qop or '') == 'md5-sess' then
        ha1 = noit.md5_hex(ha1 .. ':' .. p.nonce .. ':' .. cnonce)
    end
    -- calculate H(A2)
    local ha2 = ''
    if p.qop == "auth" or p.qop == nil then
        ha2 = noit.md5_hex(method .. ':' .. uri)
    else
        -- we don't support auth-int
        error("qop=" .. p.qop .. " is unsupported")
    end
    local resp = ''
    if p.qop == "auth" then
        resp = noit.md5_hex(ha1 .. ':' .. p.nonce .. ':' .. nc
                                .. ':' .. cnonce .. ':' .. p.qop
                                .. ':' .. ha2)
    else
        resp = noit.md5_hex(ha1 .. ':' .. p.nonce .. ':' .. ha2)
    end
    local o = {}
    o.username = user
    o.realm = p.realm
    o.nonce = p.nonce
    o.uri = uri
    o.cnonce = cnonce
    o.qop = p.qop
    o.response = resp
    o.algorithm = p.algorithm
    if p.opaque then o.opaque = p.opaque end
    local hdr = ''
    for k,v in pairs(o) do
      if hdr == '' then hdr = k .. '="' .. v .. '"'
      else hdr = hdr .. ', ' .. k .. '="' .. v .. '"' end
    end
    hdr = hdr .. ', nc=' .. nc
    return hdr
end

return HttpClient
