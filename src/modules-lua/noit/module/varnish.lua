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

-- This connects to a Varnish instance on the management port (8081)
-- It issues the stats comment and translates the output into metrics

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>varnish</name>
  <description><para>Monitor maagement metrics of a Varnish instance.</para></description>
  <loader>lua</loader>
  <object>noit.module.varnish</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="optional" default="8081"
               allowed="\d+">Specifies the port on which the management interface can be reached.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Monitor two varnish instances with management on port 8081</title>
      <para>The following example pulls are metrics available from Varnish running on 10.1.2.3 and 10.1.2.4</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="varnish" object="noit.module.varnish"/>
        </modules>
        <checks>
          <check uuid="2d42adbc-7c7a-11dd-a48f-4f59e0b654d3" module="varnish" target="10.1.2.3" />
          <check uuid="324c2234-7c7a-11dd-8585-cbb783f8267f" module="varnish" target="10.1.2.4" />
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
  local rv, err = e:connect(check.target, check.config.port or 8081)

  e:write("stats\r\n")
  str = e:read("\n")

  if rv ~= 0 or not str then
    check.bad()
    check.unavailable()
    check.status(err or str or "unknown error")
    return
  end

  local status, len = string.match(str, "^(%d+)%s*(%d+)%s*$")
  if status then check.available() end
  -- we want status to be a number
  status = 0 + status
  if status ~= 200 then
    check.bad()
    check.status(string.format("status %d", status))
    return
  end

  local rawstats = e:read(len)
  local i = 0
  for v, k in string.gmatch(rawstats, "%s*(%d+)%s+([^\r\n]+)") do
    k = string.gsub(k, "^%s*", "")
    k = string.gsub(k, "%s*$", "")
    k = string.gsub(k, "%s", "_")
    check.metric(k,v)
    i = i + 1
  end
  check.status(string.format("%d stats", i))
  check.good()
end

