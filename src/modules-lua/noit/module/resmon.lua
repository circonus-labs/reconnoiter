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

local HttpClient = require 'noit.HttpClient'
local helpers = {}

function onload(image)
  image.xml_description([=[
<module>
  <name>resmon</name>
  <description><para>The resmon module performs services checks against an HTTP server serving with Resmon XML or JSON.</para>
  <para><link xmlns:xlink="http://www.w3.org/1999/xlink"
      xlink:href="https://labs.omniti.com/trac/resmon"><citetitle>Resmon</citetitle></link> is a light-weight resource monitor that exposes health of services over HTTP in XML.</para>
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
    <parameter name="method"
               required="optional"
               default="GET"
               allowed=".+">The HTTP method to use.</parameter>
    <parameter name="payload"
               required="optional"
               default=""
               allowed=".*">The optional HTTP payload to send with the request.</parameter>
    <parameter name="http_version"
               required="optional"
               default="1.1"
               allowed="^(\d+\.\d+)?$">Sets the HTTP version for the check to use.</parameter>
    <parameter name="auth_method"
               required="optional"
               allowed="^(?:Basic|Digest|Auto)$">HTTP Authentication method to use.</parameter>
    <parameter name="auth_user"
               required="optional"
               allowed="[^:]*">The user to authenticate as.</parameter>
    <parameter name="auth_password"
               required="optional"
               allowed=".*">The password to use during authentication.</parameter>
    <parameter name="ca_chain"
               required="optional"
               allowed=".+">A path to a file containing all the certificate authorities that should be loaded to validate the remote certificate (for SSL checks).</parameter>
    <parameter name="certificate_file"
               required="optional"
               allowed=".+">A path to a file containing the client certificate that will be presented to the remote server (for SSL checks).</parameter>
    <parameter name="key_file"
               required="optional"
               allowed=".+">A path to a file containing key to be used in conjunction with the cilent certificate (for SSL checks).</parameter>
    <parameter name="ciphers"
               required="optional"
               allowed=".+">A list of ciphers to be used in the SSL protocol (for SSL checks).</parameter>
    <parameter name="read_limit"
               required="optional"
               default="0"
               allowed="\d+">Sets an approximate limit on the data read (0 means no limit).</parameter>
    <parameter name="header_(\S+)"
               required="optional"
               allowed=".+">Allows the setting of arbitrary HTTP headers in the request.</parameter>
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

  if image.name() == "resmon" then return 0 end
  local helper = {} 
  local status, err = pcall(function () helper = require ('noit.module.resmon.' .. image.name()) end)
  if not status and (err == nil or string.find(err, "not found") == nil) then
    noit.log("error", "lua require('noit.module.resmon.%s') -> %s\n", image.name(), err or "unknown error")
    return 0
  end
  helpers[image.name()] = helper
  return 0
end

function init(module)
  return 0
end

function config(module, options)
  return 0
end

function set_check_metric(check, name, type, value)
    if type == 'i' then
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

function json_metric(check, prefix, o)
    local cnt = 0
    if type(o) == "table" then
        local has_type, has_value = false, false
        for k, v in pairs(o) do
            if k == "_type" then has_type = true
            elseif k == "_value" then has_value = true
            else cnt = cnt + json_metric(check, prefix and (prefix .. '`' .. k) or k, v) end
        end
        if has_type and has_value then
          set_check_metric(check, prefix, o._type, o._value)
          cnt = cnt + 1
        end
    elseif type(o) == "string" then
        check.metric(prefix, o)
        cnt = cnt + 1
    elseif type(o) == "number" then
        check.metric_double(prefix, o)
        cnt = cnt + 1
    elseif type(o) == "boolean" then
        check.metric_int32(prefix, o and 1 or 0)
        cnt = cnt + 1
    else
        noit.log("debug", "got unknown type: " .. type(o) .. "\n")
        cnt = 0
    end
    return cnt
end

function json_to_metrics(check, doc)
    local services = 0
    check.available()
    local data = doc:document()
    if data ~= nil then
      services = json_metric(check, nil, data)
    end
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
            set_check_metric(check, prefix .. name, type, metric and metric:contents())
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
    local reverse_str = "reverse:check/" .. check.uuid
    local config = check.interpolate(check.config)
    local helper = helpers[module.name()]
    if helper and helper.fix_config then config = helper.fix_config(config) end
    local url = config.url or 'http:///'
    local schema, host, uri = string.match(url, "^(https?)://([^/]*)(.*)$");
    local method = config.method or "GET"
    local http_version = config.http_version
    local payload = config.payload
    local port
    local use_ssl = false
    local codere = noit.pcre(config.code or '^200$')
    local good = false
    local starttime = noit.timeval.now()
    local read_limit = tonumber(config.read_limit) or nil
    local client

    -- assume the worst.
    check.bad()
    check.unavailable()

    if host == nil then host = check.target end
    if schema == nil then
        schema = 'http'
    end
    if uri == nil or uri == '' then
        uri = '/'
    end
    if schema == 'http' then
        port = config.port or 81
    elseif schema == 'https' then
        port = config.port or 443
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

    -- setup SSL info
    local default_ca_chain =
        noit.conf_get_string("/noit/eventer/config/default_ca_chain")
    callbacks.certfile = function () return config.certificate_file end
    callbacks.keyfile = function () return config.key_file end
    callbacks.cachain = function ()
        return config.ca_chain and config.ca_chain
                                      or default_ca_chain
    end
    callbacks.ciphers = function () return config.ciphers end
   
    -- perform the request
    local rv, err
    local headers = {}
    headers.Host = host
    for header, value in pairs(config) do
        hdr = string.match(header, '^header_(.+)$')
        if hdr ~= nil then
          headers[hdr] = value
        end
    end
    headers['X-Reconnoiter-Period'] = check.period

    if config.auth_method == "Basic" or
        (config.auth_method == nil and
            config.auth_user ~= nil and config.auth_user ~= "" and
            config.auth_password ~= nil) then
        local user = config.auth_user or nil
        local pass = config.auth_password or nil
        local encoded = nil
        if (user ~= nil and pass ~= nil) then
            encoded = noit.base64_encode(user .. ':' .. pass)
            headers["Authorization"] = "Basic " .. encoded
        end
    elseif config.auth_method == "Digest" or
           config.auth_method == "Auto" then

        -- this is handled later as we need our challenge.
        client = HttpClient:new(callbacks)
        rv, err = client:connect(reverse_str, port, use_ssl)
        if rv ~= 0 then
            rv, err = client:connect(check.target_ip, port, use_ssl)
        end
        if rv ~= 0 then
            check.status(str or "unknown error")
            return
        end
        local headers_firstpass = {}
        for k,v in pairs(headers) do
            headers_firstpass[k] = v
        end
        client:do_request(method, uri, headers_firstpass, payload, http_version)
        client:get_response(read_limit)

        -- if we got a 200 here, we needed no auth
        if client.code == 200 then goto done_request end

        -- not success.. clear what callbacks created
        hdrs_in = {}
        output = ''

        if client.code ~= 401 or
           client.headers["www-authenticate"] == nil then
            check.status("expected digest challenge, got " .. client.code)
            return
        end
        local user = config.auth_user or ''
        local password = config.auth_password or ''
        local ameth, challenge =
            string.match(client.headers["www-authenticate"], '^(%S+)%s+(.+)$')
        if config.auth_method == "Auto" and ameth == "Basic" then
            local encoded = noit.base64_encode(user .. ':' .. password)
            headers["Authorization"] = "Basic " .. encoded
        elseif ameth == "Digest" then
            headers["Authorization"] =
                "Digest " .. client:auth_digest("GET", uri,
                                         user, password, challenge)
        else
            check.status("Unexpected auth '" .. ameth .. "' in challenge")
            return
        end
    elseif config.auth_method ~= nil then
        check.status("Unknown auth method: " .. config.auth_method)
        return
    end

    client = HttpClient:new(callbacks)
    rv, err = client:connect(reverse_str, port, use_ssl)
    if rv ~= 0 then
        rv, err = client:connect(check.target_ip, port, use_ssl)
    end
    if rv ~= 0 then
        check.status(err or "unknown error")
        return
    end

    client:do_request(method, uri, headers, payload, http_version)
    client:get_response(read_limit)

::done_request::
    if helper and helper.fix_output then output = helper.fix_output(output) end

    if helper and helper.process then
      helper.process(check, output)
      return
    end

    local jsondoc = nil
    if string.find(hdrs_in["content-type"] or '', 'json') ~= nil or
       string.find(hdrs_in["content-type"] or '', 'javascript') ~= nil then
      services = check.metric_json(output)
      if(services >= 0) then
        check.available()
        check.metric_uint32("services", services)
        if services > 0 then check.good() else check.bad() end
        check.status("services=" .. services)
      else
        noit.log("debug", "bad json: %s", output)
        check.status("json parse error")
      end
      return
    end

    -- try xml by "default" (assuming no json-specific content header)

    -- parse the xml doc
    local doc = noit.parsexml(output)
    if doc == nil then
        jsondoc = noit.parsejson(output)
        if jsondoc == nil then
            if output ~= "" then
                noit.log("debug", "bad xml: %s\n", output)
            end
            check.status("xml parse error")
            return
        end
        json_to_metrics(check, jsondoc)
        return
    end
    
    xml_to_metrics(check, doc)
end

