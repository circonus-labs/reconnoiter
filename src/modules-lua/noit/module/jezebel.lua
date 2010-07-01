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

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>jezebel</name>
  <description><para>The jezebel module performs services checks against jezebel and simplifies its special-case Resmon output.</para>
  <para><ulink url="https://labs.omniti.com/trac/resmon"><citetitle>Resmon</citetitle></ulink> is a light-weight resource monitor that exposes health of services over HTTP in XML.</para>
  <para>This module rides on the http module and provides a secondary phase of XML parsing on the contents that extracts Resmon status messages into metrics that can be trended.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.jezebel</object>
  <moduleconfig>
    <parameter name="url"
               required="required"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="port"
               required="optional"
               allowed="\d+">The TCP port can be specified to overide the default of 81.</parameter>
  </moduleconfig>
  <checkconfig>
    <parameter name=".+" required="optional" allowed=".*">All check config values are passed through to jezebel for execution.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking resmon services on OmniTI Labs.</title>
      <para>This example checks the Resmon service on OmniTI Labs.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <jezebel>
            <config>
              <url>http://127.0.0.1:8083/dispatch/</url>
            </config>
            <module loader="lua" name="com.omniti.jezebel.SampleCheck"
                    object="noit.module.jezebel"/>
          </jezebel>
        </modules>
        <checks>
          <labs target="8.8.38.5" module="com.omniti.jezebel.SampleCheck">
            <check uuid="36b8ba72-7968-11dd-a67f-d39a2cc3f9de"/>
          </labs>
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

local configs = {}

function config(module, options)
  configs[module.name()] = options
  return 0
end

local HttpClient = require 'noit.HttpClient'

function constructXml(check)
  local doc = noit.parsexml("<check/>")
  local root = doc:root()
  root:attr("target", check.target)
  root:attr("module", check.module)
  root:attr("name", check.name)
  root:attr("period", check.period)
  root:attr("timeout", check.timeout)
  local config = root:addchild("config")
  for key, value in pairs(check.config) do
    config:addchild(key):contents(value)
  end
  return doc:tostring()
end

function initiate(module, check)
    local options = configs[module.name()]
    local url = options.url or 'http:///'
    url = url .. '/' .. module.name()
    local schema, host, port, uri =
        string.match(url, "^(https?)://([^/:]*):?([0-9]*)(.+)$");
    local use_ssl = false
    local good = false

    -- assume the worst.
    check.bad()
    check.unavailable()

    if schema == 'http' then
        port = port or 80
    elseif schema == 'https' then
        port = port or 443
        use_ssl = true
    else
        error(schema .. " not supported")
    end

    local output = ''

    -- callbacks from the HttpClient
    local callbacks = { }
    callbacks.consume = function (str) output = output .. str end
    local client = HttpClient:new(callbacks)
    local rv, err = client:connect(host, port, use_ssl)
   
    if rv ~= 0 then
        if err ~= nil then err = "jezebel: " .. err end
        check.status(err or "jezebel: unknown error")
        return
    end

    -- perform the request
    local headers = {}
    headers.Host = host
    local xml = constructXml(check)
    client:do_request("POST", uri, headers, xml)
    client:get_response()

    -- parse the xml doc
    local doc = noit.parsexml(output)
    if doc == nil then
        noit.log("error", "bad xml: %s\n", output)
        check.status("bad xml from jezebel")
        return
    end
    check.available()

    local services = 0
    local metrics = 0
    local result
    local status
    for result in doc:xpath("/ResmonResults/ResmonResult") do
        services = services + 1
        local obj
        for metric in doc:xpath("metric", result) do
            metrics = metrics + 1
            local name = metric:attr("name") or "DUMMY"
            local type = metric:attr("type") or "DUMMY"
            local value = metric and metric:contents()
            if name == 'jezebel_status' then
                status = value
            elseif type == 'i' then
                check.metric_int32(name, value)
            elseif type == 'I' then
                check.metric_uint32(name, value)
            elseif type == 'l' then
                check.metric_int64(name, value)
            elseif type == 'L' then
                check.metric_uint64(name, value)
            elseif type == 'n' then
                check.metric_double(name, value)
            elseif type == 's' then
                check.metric_string(name, value)
            else
                check.metric(name, value)
            end
        end
    end
    if services == 1 and metrics > 0 then check.good() else check.bad() end
    if status == nil then status = 'results=' .. metrics end
    check.status(status)
end

