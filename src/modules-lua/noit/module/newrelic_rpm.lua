-- Copyright (c) 2010-2015, Circonus, Inc.
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
  <name>newrelic_rpm</name>
  <description><para>The newrelic_rpm module imports Rails application metrics from NewRelic's RPM service.</para>
  <para><ulink url="http://support.newrelic.com/faqs/docs/data-api"><citetitle>NewRelic RPM</citetitle></ulink> profiles Rails applications and stores metrics about the performance of the application.  These metrics are available over NewRelic's RPM data-api.</para>
  <para>This module rides on the http module and provides a secondary phase of XML parsing on the output of NewRelic's RPM data-api into metrics that can be trended.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.newrelic_rpm</object>
  <checkconfig>
    <parameter name="acct_id"
               required="required"
               allowed=".+">The account ID passed to NewRelic RPM's data-api.</parameter>
    <parameter name="application_id"
               required="required"
               allowed=".+">The application ID passed to NewRelic RPM's data-api.</parameter>
    <parameter name="license_key"
               required="optional"
               allowed=".+">The license key passed to NewRelic RPM's data-api.</parameter>
    <parameter name="api_key"
               required="optional"
               allowed=".+">The API key passed to NewRelic's REST API.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Import NewRelic RPM metrics for a test application.</title>
      <para>This example pulls metrics from an imaginary application profiled by the NewRelic RPM service.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="newrelic_rpm" object="noit.module.newrelic_rpm"/>
        </modules>
        <checks>
          <check uuid="36b8ba72-7968-11dd-a67f-d39a2cc3f9de" module="newrelic_rpm" target="65.74.177.194" disable="no" period="60000" timeout="10000">
            <config>
              <acct_id>12345</acct_id>
              <application_id>67890</application_id>
              <license_key>c80c4e6r3ea73f3bc27e1c949c9449663abd2f4d</license_key>
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

local HttpClient = require 'noit.HttpClient'

function initiate(module, check)
    local acct_id = check.config.acct_id or "DUMMY"
    local application_id = check.config.application_id or "DUMMY"
    local license_key = check.config.license_key or "DUMMY"
    local api_key = check.config.api_key or "DUMMY"
    local d = noit.dns()
    local port = 443
    local use_ssl = true
    local uri = '/accounts/' .. acct_id .. '/applications/' .. application_id .. '/threshold_values.xml'
    local codere = noit.pcre(check.config.code or '^200$')
    local good = false
    local starttime = noit.timeval.now()

    local host = ""
    local target = ""
    local a = nil
    if license_key ~= "DUMMY" then
        host = 'rpm.newrelic.com'
        a = {d:lookup('rpm.newrelic.com', 'a')}
    else
        host = 'api.newrelic.com'
        a = {d:lookup('api.newrelic.com', 'a')}
    end
    target = a[1].a

    -- assume the worst.
    check.bad()
    check.unavailable()

    local output = ''

    -- callbacks from the HttpClient
    local callbacks = { }
    callbacks.consume = function (str) output = output .. str end
    local client = HttpClient:new(callbacks)
    local rv, err = client:connect(target, port, use_ssl)
   
    if rv ~= 0 then
        check.status(err or "unknown error")
        return
    end

    -- perform the request
    local headers = {}
    headers.Host = host
    if license_key ~= "DUMMY" then
        headers["x-license-key"] = license_key
    else
        headers["x-api-key"] = api_key
    end
    client:do_request("GET", uri, headers)
    client:get_response()

    -- parse the xml doc
    local doc = noit.parsexml(output)
    if doc == nil then
        noit.log("debug", "bad xml: %s\n", output)
    end
    check.available()

    local metrics = 0
    local result
    for result in doc:xpath("/threshold-values/threshold_value") do
        metrics = metrics + 1
        local name = result:attr("name") or "DUMMY"
        local value = result:attr("metric_value") or nil
        if value == "" then
            -- Seriously, why the fuck are you passing an
            -- empty string when the value doesn't exist?
            -- Override the insanity.
            value = nil
        end
        if name == 'Apdex' then
            check.metric_double(name, result and value)
        elseif name == 'Application Busy' then
            check.metric_double(name, result and value)
        elseif name == 'CPU' then
            check.metric_uint32(name, result and value)
        elseif name == 'Memory' then
            check.metric_uint64(name, result and value)
        elseif name == 'Errors' then
            check.metric_double(name, result and value)
        elseif name == 'Error Rate' then
            check.metric_uint32(name, result and value)
        elseif name == 'Response Time' then
            check.metric_uint32(name, result and value)
        elseif name == 'Throughput' then
            check.metric_double(name, result and value)
        elseif name == 'DB' then
            check.metric_double(name, result and value)
        else
            check.metric(name, result and value)
        end
    end
    local status = ''
    if metrics > 0 then check.good() else check.bad() end
    check.status("metrics=" .. metrics)
end

