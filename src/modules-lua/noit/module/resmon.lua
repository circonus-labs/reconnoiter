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

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>resmon</name>
  <description><para>The resmon module performs services checks against an HTTP server serving with Resmon XML or JSON.</para>
  <para><ulink url="https://labs.omniti.com/trac/resmon"><citetitle>Resmon</citetitle></ulink> is a light-weight resource monitor that exposes health of services over HTTP in XML.</para>
  <para>This module rides on the http module and provides a secondary phase of XML parsing on the contents that extracts Resmon status messages into metrics that can be trended.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.resmon</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="port"
               required="optional"
               allowed="\d+">The TCP port can be specified to overide the default of 81.</parameter>
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
          <module loader="lua" name="resmon" object="noit.module.resmon"/>
        </modules>
        <checks>
          <labs target="8.8.38.5" module="resmon">
            <check uuid="36b8ba72-7968-11dd-a67f-d39a2cc3f9de">
              <config>
                <auth_user>foo</auth_user>
                <auth_password>bar</auth_password>
              </config>
            </check>
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

function config(module, options)
  return 0
end

local HttpClient = require 'noit.HttpClient'

function json_metric(check, prefix, o)
    local cnt = 1
    if type(o) == "table" then
        cnt = 0
        for k, v in pairs(o) do
            cnt = cnt + json_metric(check, prefix and (prefix .. '`' .. k) or k, v)
        end
    elseif type(o) == "string" then
        check.metric(prefix, o)
    elseif type(o) == "number" then
        check.metric_double(prefix, o)
    elseif type(o) == "boolean" then
        check.metric_int32(prefix, o and 1 or 0)
    end
    return cnt
end

function json_to_metrics(check, doc)
    local services = 0
    check.available()
    local data = doc:document()
    services = json_metric(check, nil, data)
    check.metric_uint32("services", services)
    if services > 0 then check.good() else check.bad() end
    check.status("services=" .. services)
end

function xml_to_metrics(check, doc)
    check.available()

    local services = 0
    local result
    for result in doc:xpath("/ResmonResults/ResmonResult") do
        services = services + 1
        local module = result:attr("module") or "DUMMY"
        local service = result:attr("service") or "DUMMY"
        local prefix = module .. '`' .. service .. '`'
        local obj
        obj = (doc:xpath("last_runtime_seconds", result))()
        local ds = tonumber(obj and obj:contents())
        if ds ~= nil then
            ds = math.floor(ds * 1000)
            check.metric_uint32(prefix .. "duration", ds)
        end
        obj = (doc:xpath("state", result))()
        if obj ~= nil then
            check.metric_string(prefix .. "state", obj and obj:contents())
        end
	local metrics = 0
        for metric in doc:xpath("metric", result) do
            metrics = metrics + 1
            local name = metric:attr("name") or "DUMMY"
            local type = metric:attr("type") or "DUMMY"
            if type == 'i' then
                check.metric_int32(prefix .. name, metric and metric:contents())
            elseif type == 'I' then
                check.metric_uint32(prefix .. name, metric and metric:contents())
            elseif type == 'l' then
                check.metric_int64(prefix .. name, metric and metric:contents())
            elseif type == 'L' then
                check.metric_uint64(prefix .. name, metric and metric:contents())
            elseif type == 'n' then
                check.metric_double(prefix .. name, metric and metric:contents())
            elseif type == 's' then
                check.metric_string(prefix .. name, metric and metric:contents())
            else
                check.metric(prefix .. name, metric and metric:contents())
            end
        end
        if metrics == 0 then
            local message = (doc:xpath("message", result))()
            check.metric_string(prefix .. "message", message and message:contents())
        end
    end
    check.metric_uint32("services", services)
    local status = ''
    if services > 0 then check.good() else check.bad() end
    check.status("services=" .. services)
end

function initiate(module, check)
    local url = check.config.url or 'http:///'
    local schema, host, uri = string.match(url, "^(https?)://([^/]*)(.+)$");
    local port
    local use_ssl = false
    local codere = noit.pcre(check.config.code or '^200$')
    local good = false
    local starttime = noit.timeval.now()

    local user = check.config.auth_user or nil
    local pass = check.config.auth_password or nil
    local encoded = nil
    if (user ~= nil and pass ~= nil) then
        encoded = noit.base64_encode(user .. ':' .. pass)
    end

    -- assume the worst.
    check.bad()
    check.unavailable()

    if host == nil then host = check.target end
    if schema == nil then
        schema = 'http'
        uri = '/'
    end
    if schema == 'http' then
        port = check.config.port or 81
    elseif schema == 'https' then
        port = check.config.port or 443
        use_ssl = true
    else
        error(schema .. " not supported")
    end 

    local output = ''

    -- callbacks from the HttpClient
    local callbacks = { }
    local hdrs_in = { }
    callbacks.consume = function (str) output = output .. str end
    callbacks.headers = function (t) hdrs_in = t end
    local client = HttpClient:new(callbacks)
    local rv, err = client:connect(check.target, port, use_ssl)
   
    if rv ~= 0 then
        check.status(err or "unknown error")
        return
    end

    -- perform the request
    local headers = {}
    headers.Host = host
    if encoded ~= nil then
        headers["Authorization"] = "Basic " .. encoded
    end
    client:do_request("GET", uri, headers)
    client:get_response()

    local jsondoc = nil
    if string.find(hdrs_in["content-type"] or '', 'json') ~= nil or
       string.find(hdrs_in["content-type"] or '', 'javascript') ~= nil then
        jsondoc = noit.parsejson(output)
        if jsondoc == nil then
            noit.log("error", "bad json: %s", output)
            check.status("json parse error")
            return
        end
    end

    if jsondoc ~= nil then
        json_to_metrics(check, jsondoc)
        return
    end

    -- try xml by "default" (assuming no json-specific content header)

    -- parse the xml doc
    local doc = noit.parsexml(output)
    if doc == nil then
        jsondoc = noit.parsejson(output)
        if jsondoc == nil then
            noit.log("error", "bad xml: %s", output)
            check.status("xml parse error")
            return
        end
        json_to_metrics(check, jsondoc)
        return
    end
    
    xml_to_metrics(check, doc)
end

