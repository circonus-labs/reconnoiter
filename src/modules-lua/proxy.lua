module(..., package.seeall)

local HttpClient = require 'noit.HttpClient'

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
noit.log("error", " (proxying : " .. req:uri() .. ")\n");
  client:do_request("GET", req:uri(), headers_send)
noit.log("error", " (reading : " .. req:uri() .. ")\n");
  client:get_response()
noit.log("error", " (done : " .. req:uri() .. ")\n");

  if not coded then return error(rest) end

  http:flush_end()
end
