-- Copyright (c) 2013, Circonus, Inc.
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
--     * Neither the name Circonus, Inc. nor the names of its contributors
--       may be used to endorse or promote products derived from this
--       software without specific prior written permission.
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

module(..., package.seeall)

local codenames = { }
codenames[200] = "OK"
codenames[403] = "DENIED"
codenames[404] = "NOT FOUND"

function serve_file(mime_type, code)
  code = code or 200
  return function(rest, file, st)
    local http = rest:http()
    local fd, errno = noit.open(file, bit.bor(O_RDONLY,O_NOFOLLOW))
    if ( fd < 0 ) then
      http:status(404, "NOT FOUND")
      http:flush_end()
      return
    end
    http:status(code, codenames[code] or "SUMTHIN")
    http:header("Content-Type", mime_type)
    if not http:option(http.CHUNKED) then http:option(http.CLOSE) end
    http:option(http.GZIP)
    http:write_fd(fd)
    noit.close(fd)
    http:flush_end()
  end
end

function lua_embed(rest, file, st)
  local http = rest:http()
  local inp = io.open(file, "rb")
  local data = inp:read("*all")
  inp:close();

  local f,e
  if type(_ENV) == "table" then
    -- we're in lua 5.2 land... it is a sad place.
    local loader = function(str)
      local cnt = 1
      return function()
        if cnt == 1 then
          cnt = 0
          return "return function(http)\n" .. str .. "\nend\n"
        end
        return nil
      end
    end
    f,e = assert(load(loader(data), file, "bt", _ENV))
  else
    f,e = assert(loadstring("return function(http)\n" .. data .. "\nend\n"))
    setfenv(f, getfenv(2))
  end
  f = f()

  http:status(200, "OK")
  http:header("Content-Type", "text/html")
  if not http:option(http.CHUNKED) then http:option(http.CLOSE) end
  http:option(http.GZIP)
  f(http)
  http:flush_end();
end

local handlers = {
  default = { serve = serve_file("application/unknown") },
  lua = { serve = lua_embed },
  css = { serve = serve_file("text/css") },
  js = { serve = serve_file("text/javascript") },
  ico = { serve = serve_file("image/x-icon") },
  png = { serve = serve_file("image/png") },
  jpg = { serve = serve_file("image/jpeg") },
  jpe = { serve = serve_file("image/jpeg") },
  jpeg = { serve = serve_file("image/jpeg") },
  gif = { serve = serve_file("image/gif") },
  html = { serve = serve_file("text/html") },
}

function file_not_found(rest)
  local http = rest:http()
  http:status(404, "NOT FOUND")
  http:option(http.CLOSE)
  http:flush_end()
end

function serve(rest, config, file, ext)
  local st, errno, error = noit.stat(file)
  if(st == nil or bit.band(st.mode, S_IFREG) == 0) then
    local errfile = config.webroot .. '/404.lua'
    if file == errfile then
      return file_not_found(rest)
    else
      return serve(rest, config, errfile, ext)
    end
  end

  if handlers[ext] == nil then ext = 'default' end
  handlers[ext].serve(rest, file, st)
end

function handler(rest, config)
  if(config.webroot == nil) then config.webroot = config.document_root end
  if(config.webroot == nil) then file_not_found(rest) end
  
  local http = rest:http()
  if not rest:apply_acl() then
    http:status(403, "NOT FOUND")
    http:option(http.CLOSE)
    http:flush_end()
  end
  local req = http:request()

  local req_headers = req:headers()
  local host = req:headers("Host")
  local uri = req:uri()

  local file = config.webroot .. req:uri()

  if file:match("/$") then file = file .. "index.html" end

  local extre = noit.pcre("\\.([^\\./]+)$")
  local rv, m, ext = extre(file)

  if not rv then
    file = file .. '.lua'
    ext = 'lua'
  end

  serve(rest, config, file, ext)
end
