-- Copyright (c) 2010, Michal Taborsky <michal.taborsky@nrholding.com>
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

-- This connects to a Memcache instance on the standard port (11211)
-- It issues the stats command and translates the output into metrics

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>memcached</name>
  <description><para>Monitor memcache usage metrics.</para></description>
  <loader>lua</loader>
  <object>noit.module.memcached</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="optional" default="11211"
               allowed="\d+">Specifies the port on which the memcache interface can be reached.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Monitor two memcache instances with management on port 11211</title>
      <para>The following example pulls all metrics available from memcached running on 10.1.2.3 and 10.1.2.4 using standard port</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="memcached" object="noit.module.memcached"/>
        </modules>
        <checks>
          <check uuid="2d42adbc-7c7a-11dd-a48f-4f59e0b654d3" module="memcached" target="10.1.2.3" />
          <check uuid="324c2234-7c7a-11dd-8585-cbb783f8267f" module="memcached" target="10.1.2.4" />
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

  -- connect to the memcache instance
  local e = noit.socket()
  local rv, err = e:connect(check.target, check.config.port or 11211)

  -- issue the stats command and read everything until the END keyword
  e:write("stats\r\n")
  str = e:read("END")

  if rv ~= 0 or not str then
    -- something went horribly wrong, die with error
    check.bad()
    check.unavailable()
    check.status(err or str or "unknown error")
    return
  end

  local i = 0
  
  -- parse the output and extract the statistics
  -- the stats are in the following format:
  --    STAT metric_name metric_value

  for k, v in string.gmatch(str, "STAT%s+([%w_]+)%s+([%d.]+)%c+") do
    
    -- decide which metric is of what type
    if k == "pid" or k == "accepting_conns" or k == "version" or k == "pointer_size" then  
      -- these look like numbers, but in fact they are better represented as strings
      -- because for example we never want to make an average out of them
      check.metric_string(k,v)
    elseif k == "rusage_user" or k == "rusage_system" then
      -- these are real numbers in seconds and fractions of a second
      check.metric_double(k,v)
    elseif k == "curr_connections" or k == "connection_structures" or k == "threads" then
      -- these are always small positive numbers
      check.metric_uint32(k,v)
    else
      -- everything else there can be potentially a big positive number
      check.metric_uint64(k,v)
    end
      
    i = i + 1
    
  end

  -- all went well, report success
  check.status(string.format("%d stats", i))
  check.available()
  check.good()
end

