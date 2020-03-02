-- Copyright (c) 2010, Graham Barr
--
-- Permission is hereby granted, free of charge, to any person
-- obtaining a copy of this software and associated documentation
-- files (the "Software"), to deal in the Software without
-- restriction, including without limitation the rights to use,
-- copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the
-- Software is furnished to do so, subject to the following
-- conditions:
--
-- The above copyright notice and this permission notice shall be
-- included in all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
-- EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
-- OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
-- NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
-- HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
-- WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
-- FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
-- OTHER DEALINGS IN THE SOFTWARE.

-- This connects to a haproxy instance
-- It issues a stat commands and translates the output into metrics

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>haproxy</name>
  <description><para>Monitor management metrics of a haproxy instance.</para></description>
  <loader>lua</loader>
  <object>noit.module.haproxy</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="uri" required="required" default="/admin?stats;csv" allowed="^/.+">
      The URL excluding schema and hostname for the haproxy stats CSV export.
      eg /admin?stats;csv
    </parameter>
    <parameter name="host" required="optional" allowed=".+">
      Host name to include in HTTP request, defaults to target IP
    </parameter>
    <parameter name="port" required="optional" default="80" allowed="^[0-9]+$">
      Port to connect to
    </parameter>
    <parameter name="select" required="optional" default=".*" allowed=".+">
      Specifies a regular expression to pick which metrics to report. Will be matched
      against the pxname and svname columns concatenated by a ","
    </parameter>
    <parameter name="auth_user" required="optional" allowed="[^:]*">
      The user to authenticate as.
    </parameter>
    <parameter name="auth_password" required="optional" allowed=".*">
      The password to use during authentication.
    </parameter>
    <parameter name="use_ssl" required="optional" allowed="^(?:true|false|on|off)$" default="false">
      Upgrade TCP connection to use SSL.
    </parameter>
    <parameter name="ca_chain" required="optional" allowed=".+">
      A path to a file containing all the certificate authorities that should be loaded to 
      validate the remote certificate (for SSL checks).
    </parameter>
    <parameter name="certificate_file" required="optional" allowed=".+">
      A path to a file containing the client certificate that will be presented to the remote 
      server (for SSL checks).
    </parameter>
    <parameter name="key_file" required="optional" allowed=".+">
      A path to a file containing key to be used in conjunction with the cilent certificate 
      (for SSL checks).
    </parameter>
    <parameter name="ciphers" required="optional" allowed=".+">
      A list of ciphers to be used in the SSL protocol (for SSL checks).
    </parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Monitor two haproxy instances</title>
      <para>The following example pulls all metrics available from haproxy running on 10.1.2.3 and 10.1.2.4</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="haproxy" object="noit.module.haproxy"/>
        </modules>
        <checks>
          <check uuid="2d42adbc-7c7a-11dd-a48f-4f59e0b654d3" module="haproxy" target="10.1.2.3" />
          <check uuid="324c2234-7c7a-11dd-8585-cbb783f8267f" module="haproxy" target="10.1.2.4" />
        </checks>
      </noit>
      ]]></programlisting>
    </example>
  </examples>
</module>
]=]);
  return 0
end

function init(module)
  return 0
end

function config(module, options)
  return 0
end

local HttpClient = require 'mtev.HttpClient'


function initiate(module, check)
  local config = check.interpolate(check.config)
  local host = config.host or check.target_ip or check.target
  local port = config.port or 80
  local use_ssl = config.use_ssl or "off"
  local converted_use_ssl = false;
  local uri  = config.uri or "/admin?stats;csv"

  -- expect the worst
  check.bad()
  check.unavailable()

  if use_ssl ~= nil and (use_ssl == "true" or use_ssl == "on") then
    converted_use_ssl = true
  end

  -- build request headers
  local headers = {}
  headers.Host = host
  for header, value in pairs(config) do
    hdr = string.match(header, '^header_(.+)$')
    if hdr ~= nil then
      headers[hdr] = value
    end
  end

  if config.auth_user ~= nil then
    local user = config.auth_user;
    local password = config.auth_password or ''
    local encoded = mtev.base64_encode(user .. ':' .. password)
    headers["Authorization"] = "Basic " .. encoded
  end

  -- gather output from HttpClient
  local output_tbl = {''}
  local callbacks = { }
  local hdrs_in = { }
  callbacks.consume = function (str) table.insert(output_tbl, str) end
  callbacks.headers = function (t) 
    hdrs_in = t 
  end

  -- setup SSL info
  local default_ca_chain = noit.default_ca_chain()
  callbacks.certfile = function () return config.certificate_file end
  callbacks.keyfile = function () return config.key_file end
  callbacks.cachain = function ()
      return config.ca_chain and config.ca_chain
                                    or default_ca_chain
  end
  callbacks.ciphers = function () return config.ciphers end

  local client = HttpClient:new(callbacks)
  local rv, err = client:connect(check.target_ip, port, converted_use_ssl)
 
  if rv ~= 0 then error(err or "unknown error") end

  client:do_request('GET', uri, headers, nil)
  client:get_response();
  local output = table.concat(output_tbl, "")

  check.available()

  if client.code == nil or client.code ~= 200 then error("HTTP response " .. tostring(code)) end
  if not output:find('^# .*\n.*\n') then error("Invalid CSV '" .. string.sub(output,1,10) .. "'...") end

  local hdr      = {}
  local state    = 0
  local pos      = 3
  local column   = 0
  local rowname  = ''
  local count    = 0
 
  local is_string        = {}
  is_string.status       = 1
  is_string.check_status = 1
  is_string.qlimit       = 1
  is_string.throttle     = 1

  while 1 do
    local nextpos, char = output:match('()([,\n])', pos)
    if nextpos then
      local field = output:sub(pos, nextpos - 1)
      if column == 0 then -- pxname
        rowname = field
      elseif column == 1 then -- svname
        rowname = rowname .. "`" .. field
      elseif state == 0 then -- collecting header line
        hdr[column] = field
      else
        local selectre = mtev.pcre(config.select or '.*')
        if selectre == nil or selectre(rowname) then
          local cname = rowname .. "`" .. hdr[column]
          if is_string[hdr[column]] then
            check.metric(cname,field)
          elseif field ~= '' then
            check.metric_uint64(cname,field)
          end
        end
      end
      if char == '\n' then
        state  = 1
        column = 0
        count  = count + 1
      else
        column = column + 1
      end
      pos = nextpos + 1
    else
      break      
    end
  end

  check.status(string.format("%d stats", count))
  check.good()
end
