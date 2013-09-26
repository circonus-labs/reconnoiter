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

function onload(image)
  image.xml_description([=[
<module>
  <name>carbon</name>
  <description><para>Carbon (graphite) metrics receiver.</para></description>
  <loader>lua</loader>
  <object>noit.module.carbon</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="optional" default="2003"
               allowed="\d+">Specifies the TCP port on which metrics will be received.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Receiving Carbon data.</title>
      <para>This example receives data sent to a carbon receptor.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="carbon" object="noit.module.carbon" />
        </modules>
        <checks>
            <check uuid="79ba881e-ad2e-11de-9fb0-a322e3288ca7" name="carbon">
              <config>
                <port>9999</port>
              </config>
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

local checks = {}
local _connid = 0

function push_metric(check, metric, value, age)
  if check.config.max_age ~= nil and check.config.max_age > age then return end
  check.good()
  check.available()
  check.metric(metric, value)
end

function count_checks(l)
  local nchecks = 0
  for checkid, v in pairs(l.checks) do
    nchecks = nchecks + 1
  end
  return nchecks
end

function carbon_client(client, co, l)
  _connid = _connid + 1
  local connid = _connid

  -- reference the coroutine or get be gc'd during suspension
  l.clients[connid] = co
  -- this event lives in this coroutine now
  client:own()

  local success, msg = pcall(function() 
    repeat
      local line = client:read("\n")
      if line ~= nil then
        -- split out the data on the line
        local metric, value, whence = string.match(line, "([^%s]+)%s+(%d+%.?%d*)%s+(%d+)")
        if metric ~= nil then
          local now = noit.timeval.now()
          local age = now.sec - whence
          local nchecks = 0

          -- run through each check attached to this listener
          for checkid, v in pairs(l.checks) do
            nchecks = nchecks + 1
            local check = noit.check(checkid)
            -- if the check doesn't exist, it has been turned off,
            -- or the port has changed, then we detach from this listener
            if check == nil or
               check.flags("NP_DISABLED","NP_KILLED") ~= 0 or
               check.config.port ~= l.port then
              l.checks[checkid] = nil
            else
              push_metric(check, metric, value, age)
            end
          end

          -- if we have no checks, I'd bet our listener should be turned off.
          if nchecks < 1 then
            line = nil
          end
        end
      end
    until line == nil
  end)
  if not success then
    noit.log("error", "carbon client errored: " .. msg .. "\n")
  end
  client:close()
  l.clients[connid] = nil
end

function carbon_acceptor(l)
  noit.log("error", "carbon_acceptor() starting.\n")
  local e = noit.socket('inet', 'tcp')
  local success, msg = pcall(function() 
    -- bind
    local err, errno = e:bind('255.255.255.255', l.port)
    if err ~= 0 then error("binding error -> " .. errno .. "\n") end
    -- listen
    local err, errno, msg = e:listen(20)
    if err ~= 0 then error("listen error -> " .. msg .. "\n") end

    -- listen loop
    noit.log("error", "new carbon listener in port " .. l.port .. "\n")
    while count_checks(l) > 0 do
      local client = e:accept()
      local co = coroutine.create(carbon_client)
      coroutine.resume(co, client, co, l);
    end
  end
  )
  if not success then
    noit.log("error", "accept() <- " .. msg .. "\n")
  end
  noit.log("error", "listener on port " .. l.port .. " shutting down\n");
  e:close()
  l.listener = nil
end

function new_listener(port, input_handler)
  local l = {}
  l.port = port;
  l.input_handler = input_handler
  l.checks = {}
  l.clients = {}
  return l
end

function start_listener(l)
  if l.reader == nil then
    l.reader = coroutine.create(carbon_acceptor)
    coroutine.resume(l.reader, l)
  end
end

function initiate(module, check)
  local port = check.config.port or 2003
  check.bad()
  check.unavailable()
  check.status("unknown error")

  local listener = checks[port]
  if check.flags("NP_DISABLED","NP_KILLED") ~= 0 then
    listener.checks[check.checkid] = nil
    return
  end

  if listener == nil then
    checks[port] = new_listener(port, text_input)
    listener = checks[port]
    if listener == nil then
      check.status("could not provision listener")
      return
    end
  end

  if listener.checks[check.checkid] == nil then
    listener.checks[check.checkid] = true
  end

  start_listener(listener)

  return
end

