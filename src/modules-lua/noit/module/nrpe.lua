-- Copyright (c) 2009, OmniTI Computer Consulting, Inc.
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

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>nrpe</name>
  <description><para>Nagios NRPE (v2) check.</para></description>
  <loader>lua</loader>
  <object>noit.module.nrpe</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="required"
               allowed="\d+">Specifies the port on which the management interface can be reached.</parameter>
    <parameter name="use_ssl" required="optional" allowed="^(?:true|false|on|off)$" default="true">Upgrade TCP connection to use SSL.</parameter>
    <parameter name="command" required="required" allowed=".+">Command to run on the remote node.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking load via NRPE.</title>
      <para>This check runs check_load on the remote host.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="nrpe" object="noit.module.nrpe" />
        </modules>
        <checks>
          <check uuid="79ba881e-ad2e-11de-9fb0-a322e3288ca7" name="load"
                 target="10.0.7.2" module="nrpe" period="10000" timeout="5000">
            <config><comand>check_load</command></config>
          </check>
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

function elapsed(check, name, starttime, endtime)
    local elapsedtime = endtime - starttime
    local seconds = string.format('%.3f', noit.timeval.seconds(elapsedtime))
    check.metric_uint32(name, math.floor(seconds * 1000 + 0.5))
    return seconds
end

function v2_packet(cmd)
  local payload = string.pack(">h", 2324) .. cmd ..
                  string.rep(string.char(0), 1024-string.len(cmd)) .. "SR"
  local crc = noit.crc32(string.char(0,2,0,1,0,0,0,0) .. payload)
  return string.char(0,2,0,1) .. string.pack(">I", crc) .. payload
end

function initiate(module, check)
  local starttime = noit.timeval.now()
  local max_len = 80
  check.bad()
  check.unavailable()
  check.status("unknown error")
  local good = false
  local status = ""
  local use_ssl = true
  local port = 5666

  if check.config.port ~= nil then
    port = check.config.port
  end

  if check.config.command == nil then
    check.status("no command")
    return
  end

  -- SSL
  if check.config.use_ssl == "false" or check.config.use_ssl == "off" then
    use_ssl = false
  end

  local e = noit.socket(check.target_ip)
  local rv, err = e:connect(check.target_ip, port)
  if rv ~= 0 then
    check.status(err or "connect error")
    return
  end

  if use_ssl == true then
    rv, err = e:ssl_upgrade_socket(nil,nil,nil,"ADH-AES256-SHA")
    if rv ~= 0 then
      check.status(err or "ssl error")
      return
    end
  end 

  local connecttime = noit.timeval.now()
  elapsed(check, "tt_connect", starttime, connecttime)
  if rv ~= 0 then
    check.status(err or "connection failed")
    return
  end

  status = "connected"
  check.available()

  -- run the command
  local packet = v2_packet(check.config.command)
  e:write(packet)
  local response = e:read(1036)
  if response == nil or response:len() ~= 1036 then
    check.status("bad packet length " .. (response and response:len() or "0"))
    return
  end

  local cnt, h1, h2, len, r = string.unpack(response, ">HHIH")
  if h1 ~= 2 or h2 ~= 2 or r > 3 then
    check.status(string.format("bad packet integrity[%u,%u,%u,%u]",
                               h1, h2, len, r))
    return
  end

  local result = response:sub(cnt):gsub("%z", "")

  local state = result:match("^(%w+)")
  if state == "OK" then good = true end
  check.metric_string("state", state)

  local message = result:match("^%w+ %- ([^|]+)")
  check.metric_string("message", message)

  local metrics = result:match("|(.+)$")
  if metrics ~= nil then
    -- /(?<key>\S+)=(?<value>[^;\s]+)(?=[;\s])/g
    local exre = noit.pcre('(\\S+)=([^;\\s]+)(?=[;\\s])')
    local rv = true
    while rv do
      rv, m, key, value = exre(metrics)
      if rv and key ~= nil then
        check.metric(key, value)
      end
    end
  end
  -- turnaround time
  local endtime = noit.timeval.now()
  local seconds = elapsed(check, "duration", starttime, endtime)
  status = status .. ',rt=' .. seconds .. 's'
  if good then check.good() else check.bad() end
  check.status(status)
end

