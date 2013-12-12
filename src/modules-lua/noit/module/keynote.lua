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
  <name>keynote</name>
  <description><para>The keynote module fetches telemetry from Keynote(TM) API calls.</para>
  <para>This module rides on the http module and provides a secondary phase of JSON parsing transformation that turns the Keynote data into something useful.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.keynote</object>
  <checkconfig>
    <parameter name="base_url"
               required="optional"
               default="https://api.keynote.com/keynote/api/"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="api_key"
               required="required"
               allowed=".+">The Keynote-issued API access key.</parameter>
    <parameter name="slotid_list"
               required="required"
               allowed="\d+(?:,\d+)*">A list of Keynote slot ids.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking Keynote services.</title>
      <para>This example checks two slots: 10 and 11.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="keynote" object="noit.module.keynote"/>
        </modules>
        <checks>
          <keynote target="api.keynote.com" module="keynote">
            <config>
              <api_key>917c1660-5136-11e3-afd7-7cd1c3dcddf7</api_key>
            </config>
            <check uuid="36b8ba72-7968-11dd-a67f-d39a2cc3f9de" period="300000">
              <config>
                <slotid_list>10,11</slotid_list>
              </config>
            </check>
          </keynote>
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

local HttpClient = require 'noit.HttpClient'

function json_to_metrics(check, doc)
    local services = 0
    check.available()
    local data = doc:document()
    for i,v in ipairs(data.measurement) do
      check.metric_double(v.id, tonumber(v.bucket_data[0].perf_data.value))
      check.metric_double(v.alias, tonumber(v.bucket_data[0].perf_data.value))
      services = services + 1
    end
    check.metric_uint32("services", services)
    if services > 0 then check.good() else check.bad() end
    check.status("services=" .. services)
end

function initiate(module, check)
    local config = check.interpolate(check.config)
    local url = config.base_url or 'https://api.keynote.com/keynote/api/'
    local schema, host, uri = string.match(url, "^(https?)://([^/]*)(.*)$");
    local port
    local use_ssl = false

    -- assume the worst.
    check.bad()
    check.unavailable()
    if check.target_ip == nil then return end

    if host == nil then host = check.target end
    if schema == nil then
        schema = 'http'
    end
    if uri == nil or uri == '' then
        uri = '/'
    end
    if schema == 'http' then
        port = config.port or 80
    elseif schema == 'https' then
        port = config.port or 443
        use_ssl = true
    else
        error(schema .. " not supported")
    end 

    local newhost, newport = string.match(host, "^(.*):(%d+)$")
    if newport ~= nil then
        host = newhost
        port = newport
    end

    uri = uri .. 'getgraphdata?api_key=' .. config.api_key .. '&format=json&slotidlist=' .. config.slotid_list .. '&timemode=relative&relativehours=300&bucket=300&timezone=UTC'

    local output = ''

    -- callbacks from the HttpClient
    local callbacks = { }
    local hdrs_in = { }
    callbacks.consume = function (str) output = output .. str end
    callbacks.headers = function (t) hdrs_in = t end
   
    -- perform the request
    local headers = {}
    headers.Host = host
    headers.Accept = '*/*'

    local client = HttpClient:new(callbacks)
    local rv, err = client:connect(check.target_ip, port, use_ssl, host)
    if rv ~= 0 then
        check.status(err or "unknown error")
        return
    end

    client:do_request("GET", uri, headers)
    client:get_response()
    json_to_metrics(check, noit.parsejson(output))
end

