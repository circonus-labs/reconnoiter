-- Copyright (c) 2010, OmniTI Computer Consulting, Inc.
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

-- This connects to a Varnish instance on the management port (8081)
-- It issues the stats comment and translates the output into metrics

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>munin</name>
  <description><para>Monitor metrics exposed by a munin-node instance.</para></description>
  <loader>lua</loader>
  <object>noit.module.munin</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="optional" default="4949"
               allowed="\d+">Specifies the port on which the management interface can be reached.</parameter>
    <parameter name="plugins" required="optional"
               allowed=".+">A list of space separated plugins from which to fetch metrics. If not specified, a list will be retrieved from the munin node.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Monitor a node running munun-node</title>
      <para>The following example pulls all munin metrics from 10.1.2.3 and just "processes" metrics from 10.1.2.4</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="munin" object="noit.module.munin"/>
        </modules>
        <checks>
          <check uuid="535cc224-9f66-11df-b198-8b094b17808a" module="munin" target="10.1.2.3" />
          <check uuid="5acce980-9f66-11df-8027-ebfe9d8b53e1" module="munin" target="10.1.2.4">
            <config><plugins>processes</plugins></config>
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

function initiate(module, check)
  local e = noit.socket()
  local plugins = check.config.plugins
  local rv, err = e:connect(check.target, check.config.port or 4949)

  e:read("\n") -- munin banner
  if plugins == nil then
    e:write("list\r\n")
    plugins = e:read("\n")
  end

  local i = 0
  for p in string.gmatch(plugins, "%s*(%S+)%s*") do
    e:write("fetch " .. p .. "\r\n")
    local rawstats = e:read("\.\n")
    for k, v in string.gmatch(rawstats, "\n?(%S+)\.value%s+([^\r\n]+)") do
      if v == "U" then
        check.metric_double(p .. "`" .. k,nil)
      else
        check.metric(p .. "`" .. k,v)
      end
      i = i + 1
    end
  end

  if rv ~= 0 then
    check.bad()
    check.unavailable()
    check.status(err or str or "unknown error")
    return
  end

  check.status(string.format("%d stats", i))
  check.good()
end

