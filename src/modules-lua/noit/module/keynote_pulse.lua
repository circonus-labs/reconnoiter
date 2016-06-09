-- Copyright (c) 2014, Circonus, Inc.
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
  <name>keynote_pulse</name>
  <description><para>The keynote module fetches telemetry from Keynote(TM) Pulse API.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.keynote_pulse</object>
  <checkconfig>
    <parameter name="base_url"
               required="optional"
               default="https://datapulse.keynote.com/"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="user"
               required="required"
               allowed=".+">The Keynote-issued Pulse API username.</parameter>
    <parameter name="password"
               required="required"
               allowed=".+">The Keynote-issued Pulse API password.</parameter>
    <parameter name="agreement_id"
               required="required"
               allowed="\d+">The Keynote-issued agreement_id for the service.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking Keynote services.</title>
      <para>This example checks agreement_id: 1.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="keynote_pulse" object="noit.module.keynote_pulse"/>
        </modules>
        <checks>
          <keynote target="datapulse.keynote.com" module="keynote_pulse">
            <config>
              <user>bob</user>
              <password>bobspassword</password>
            </config>
            <check uuid="36b8ba72-7968-11dd-a67f-d39a2cc3f9de" period="300000">
              <config>
                <agreement_id>1</agreement_id>
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

local next_allowed_run = {}

function init(module)
  return 0
end

function config(module, options)
  return 0
end

local HttpClient = require 'mtev.HttpClient'

function xml_to_metrics(check, doc)
    local services = 0
    local agents = {}
    local slots = {}

    check.available()

    local result
    for result in doc:xpath("//TXN_META_DATA/AGENT_META_DATA") do
        agents[result:attr("agent_id")] = result:attr("description")
    end
    for result in doc:xpath("//TXN_META_DATA/SLOT_META_DATA") do
        slots[result:attr("slot_id")] = result:attr("slot_alias")
    end

    for result in doc:xpath("//DP_TXN_MEASUREMENTS/TXN_MEASUREMENT") do
        local slot = slots[result:attr("slot")] or "unknown slot"
        local agent = agents[result:attr("agent")] or "unknown agent"
        local prefix = slot .. '`' .. agent .. '`'
        for summary in doc:xpath("TXN_SUMMARY", result) do
            for i,key in ipairs({"delta_user_msec", "delta_msec",
                                 "content_errors", "estimated_cache_delta_msec",
                                 "resp_bytes", "element_count"}) do
                check.metric_uint32(prefix .. 'summary`' .. key, tonumber(summary:attr(key)))
            end
            break
        end
        local page
        for page in doc:xpath("TXN_PAGE", result) do
            local pagetxn
            prefix = slot .. '`' .. agent .. '`page' .. page:attr("page_seq") .. '`'
            for pagetxn in doc:xpath("TXN_PAGE_PERFORMANCE", page) do
                for i,key in ipairs({"start_msec", "system_delta",
                                     "connect_delta", "dns_delta",
                                     "element_delta", "first_packet_delta",
                                     "request_delta", "remain_packets_delta"}) do
                    check.metric_uint32(prefix .. key, tonumber(pagetxn:attr(key)))
                end
            end
        end
    end
end

function initiate(module, check)
    if next_allowed_run[check.checkid] and
       mtev.timeval.seconds(mtev.timeval.now()) < next_allowed_run[check.checkid] then
      check.bad()
      check.unavailable()
      check.status("internally rate limited")
      return
    end
    local config = check.interpolate(check.config)
    local url = config.base_url or 'https://datapulse.keynote.com/'
    local schema, host, uri = string.match(url, "^(https?)://([^/]*)(.*)$");
    local port
    local use_ssl = false
    local ip = check.target_ip

    local dns = mtev.dns()
    local r = dns:lookup(host or 'datapulse.keynote.com')
    if r and r.a ~= nil then ip = r.a end 

    -- assume the worst.
    check.bad()
    check.unavailable()
    if ip == nil then return end

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

    uri = uri .. 'dps/xmlpost?delta_time=300&service=TRANSACTION&meta_data=true&agreement_id=' .. config.agreement_id

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
    headers.Authorization = "Basic " .. mtev.base64_encode(config.user .. ':' .. config.password)

    local client = HttpClient:new(callbacks)
    local rv, err = client:connect(ip, port, use_ssl, host, "TLSv1")
    if rv ~= 0 then
        check.status(err or "unknown error")
        return
    end

    client:do_request("GET", uri, headers)
    client:get_response()
    next_allowed_run[check.checkid] = mtev.timeval.seconds(mtev.timeval.now()) + 60
    xml_to_metrics(check, mtev.parsexml(output))
end

