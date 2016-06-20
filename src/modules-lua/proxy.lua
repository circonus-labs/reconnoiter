-- Copyright (c) 2012, Circonus, Inc.
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

local HttpClient = require 'mtev.HttpClient'

function error(rest)
  local http = rest:http()
  http:status(502, "BAD GATEWAY")
  if not http:option(http.CHUNKED) then http:option(http.CLOSE) end
  http:option(http.GZIP)
  http:flush_end()
end

function handler(rest, config)
  local coded = false
  local http = rest:http()
  local req = http:request()

  local callbacks = { }
  local client = HttpClient:new(callbacks)
  callbacks.headers = function (t) hdrs_in = t end
  callbacks.consume = function (str)
    if not coded then
      http:status(client.code, "OK")
      for k,v in pairs(hdrs_in) do
        if k ~= "connection" and k ~= "content-length" then
          http:header(k,v)
        end
      end
      if not http:option(http.CHUNKED) then http:option(http.CLOSE) end
      http:option(http.GZIP)
      coded = true
    end
    http:write(str)
  end
  local rv, err = client:connect(config.target, 80)
  if rv ~= 0 then
     return error(rest)
  end

  local headers_send = {}
  for k,v in pairs(req:headers()) do
    if k ~= "connection" and k ~= "accept-encoding" then
      headers_send[k] = v
    end
  end
  headers_send.host = config.host
mtev.log("error", " (proxying : " .. req:uri() .. ")\n");
  client:do_request("GET", req:uri(), headers_send)
mtev.log("error", " (reading : " .. req:uri() .. ")\n");
  client:get_response()
mtev.log("error", " (done : " .. req:uri() .. ")\n");

  if not coded then return error(rest) end

  http:flush_end()
end
