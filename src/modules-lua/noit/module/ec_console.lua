-- Copyright (c) 2014, Message Systems, Inc.
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
--     * Neither the name Message Systems, Inc. nor the names
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

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>ec_console</name>
  <description><para>Run an ec_console command, parse its results and return them</para></description>
  <loader>lua</loader>
  <object>noit.module.ec_console</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="optional" default="2025"
               allowed="\d+">Specifies the TCP port to connect to.</parameter>
    <parameter name="command" required="optional" default="xml summary"
               allowed=".+">Specifies the ec_console command to run (must produce XML output).</parameter>
    <parameter name="xpath" required="optional" default="/ServerSummary/*"
               allowed=".+">xpath to fetch objects from, should also include a trailing wildcard when requesting all objects (if the objects option is not defined)</parameter>
    <parameter name="objects" required="optional" default=""
               allowed=".*">Objects to fetch from the xpath, comma-separated list.  If missing all objects will be fetched</parameter>
    <parameter name="sasl_authentication"
               required="optional"
               default="off"
               allowed="(?:off|digest-md5)">Specifies the type of SASL Authentication to use</parameter>
    <parameter name="sasl_user"
               required="optional"
               default=""
               allowed=".+">The SASL Authentication username</parameter>
    <parameter name="sasl_password"
               required="optional"
               default=""
               allowed=".+">The SASL Authentication password</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Read the summary command</title>
      <para>The following example to connect to 10.0.0.1 and read the summary command</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="smtp" object="noit.module.ec_console"/>
        </modules>
        <checks>
          <check uuid="2d42adbc-7c7a-11dd-a48f-4f59e0b654d3" module="ec_console" target="10.0.0.1">
            <config>
              <command>xml summary</command>
              <xpath>/ServerSummary/*</xpath>
              <sasl_authentication>digest-md5</sasl_authentication>
              <sasl_user>admin</sasl_user>
              <sasl_password>admin</sasl_password>
            </config>
          </check>
        </checks>
      </noit>
      ]]></programlisting>
    </example>
  </examples>
</module>
]=])
  return 0
end

function init(module)
  return 0
end

function config(module, options)
  return 0
end

local function run_command(e, command)
  e:write(string.pack(">HH", 1, #command))
  e:write(command)
  local packet = e:read(2)
  if packet then
    local len
    local count, packet_type = string.unpack(packet, ">H")

    if tonumber(packet_type) == 1 then
      count, len = tonumber(string.unpack(e:read(2), ">H"))
    elseif tonumber(packet_type) == 2 then
      count, len = string.unpack(e:read(4), ">I")
    else
      return nil
    end
    return e:read(len) 
  else
    return nil
  end
end

local function walk_xpath(check, root, depth)
  local has_children = false
  local metricname = depth
  local rname = root:name()
  if depth == nil then 
    depth = rname
  else 
    depth = depth .. '`' .. rname
  end

  for obj in root:children() do
    walk_xpath(check, obj, depth)
    has_children = true
  end

  if not has_children and metricname ~= nil then
    -- it was a leaf
    local val = root:contents()

    if tonumber(val) then
      check.metric(metricname, tonumber(val))
    else
      check.metric_string(metricname, tostring(val))
    end
  end
end

function initiate(module, check)
  local config = check.interpolate(check.config)
  local starttime = noit.timeval.now()
  local e = noit.socket(config.target_ip)
  local port = config.port or 2025
  local rv, err = e:connect(check.target_ip, config.port or 2025)
  local action_result
  check.unavailable()

  if rv ~= 0 then
    check.bad()
    check.status(err or message or "no connection")
    return
  end

  local version = run_command(e, "version")

  if string.match(version, "Authorization required") and
     config.sasl_authentication == "digest-md5" then
    local challenge = run_command(e, "auth DIGEST-MD5")
    challenge = noit.base64_decode(challenge)
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
        rand_string({'a','b','c','d','e','f','0',
                     '1','2','3','4','5','6','7','8','9'}, 24)
    local p = {}
    for k,v in string.gmatch(challenge, '(%a+)="*([^",]+)"*,*%s*') do 
      p[k] = v
    end

    -- qop can be a list
    for q in string.gmatch(p.qop, '([^,]+)') do
        if q == "auth" then p.qop = "auth" end
    end
    local uri = "ec_console/" .. check.target_ip .. ":" .. port
    local huri = noit.md5_hex("AUTHENTICATE:" .. uri)
    local secret = noit.md5(config.sasl_user .. ":" .. p.realm .. ":" .. config.sasl_password)
    local hexha1 = noit.md5_hex(secret .. ":" .. p.nonce .. ":" .. cnonce)

    local crypt_response = noit.md5_hex(hexha1 .. ":" .. p.nonce .. ":" .. nc .. ":" .. cnonce .. ":" .. p.qop .. ":" .. huri)
    local response = string.format("charset=%s,username=\"%s\",realm=\"%s\",nonce=\"%s\",nc=\"%08x\",cnonce=\"%s\",digest-uri=\"%s\",response=%s,qop=%s", p.charset, config.sasl_user, p.realm, p.nonce, 1, cnonce, uri, crypt_response, p.qop)

    local response = run_command(e, "auth response " .. noit.base64_encode(response))
    if not string.match(response, "authorized") then
      check.bad()
      check.status(response or "authorization failed")
      return
    end
  end

  local good = true
  local command = config.command or 'xml summary'
  local xpath_str = config.xpath or '/ServerSummary/*'
  local status = 'connected'

  local response = run_command(e, command)
  local doc = noit.parsexml(response)
  if doc then
    check.available();
    local nodes = doc:xpath(xpath_str)
    if nodes then
      if config.objects and string.len(config.objects) > 0 then
        local xpath = nodes()
        for object_name in string.gmatch(config.objects, "([^,]+),*") do
          local obj = (doc:xpath(object_name, xpath))()
          local val = obj and obj:contents()
          if val and tonumber(val) then
            check.metric_double(object_name, tonumber(val))
          elseif val then
            check.metric_string(object_name, tostring(val))
          end
        end
      else
        for root in nodes do
          walk_xpath(check, root)
        end
      end
    else
      good = false
    end
  else 
    good = false
  end 
  if good then check.good() end

  local elapsed = noit.timeval.now() - starttime
  local elapsed_ms = math.floor(tostring(elapsed) * 1000)
  check.metric("duration",  elapsed_ms)
end

