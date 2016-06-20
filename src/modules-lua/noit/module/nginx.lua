-- Copyright (c) 2011, Macmillan Digital Science
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

-- This makes an http request to the Nginx status module
-- and parses the output into metrics

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>nginx</name>
  <description><para>The nginx module gathers information from the nginx stub_status module</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.nginx</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               allowed=".+">The URL including schema and hostname for the status output from nginx.</parameter>
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
  </checkconfig>
  <examples>
    <example>
      <title>Monitor an nginx server with a status page available at http://10.1.2.3/nginx_status</title>
      <para>The following example pulls metrics from Nginx's status module (http://wiki.nginx.org/HttpStubStatusModule) from http://10.1.2.3/nginx_status</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="nginx" object="noit.module.nginx"/>
        </modules>
        <checks>
         <check uuid="CAC1A58F-1670-4F71-8D15-21461D3F6624" name="nginx_10123" module="nginx" target="10.1.2.3">
           <config>
             <url>http://10.1.2.3/nginx_status</url>
           </config>
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
  local e = endtime - starttime
  local seconds = string.format("%.3f", mtev.timeval.seconds(e))
  check.metric_uint32(name, math.floor(seconds * 1000 +0.5))
  return seconds
end

local HttpClient = require 'mtev.HttpClient'

function initiate(module, check)
  local config = check.interpolate(check.config)
  local url = config.url
  local schema, host, port, uri = string.match(url, "^(https?)://([^:/]*):?([0-9]*)(/?.*)$");
  local use_ssl = false

  if schema == nil then
    local temp_url = "http://" .. url
    schema, host, port, uri = string.match(temp_url, "^(https?)://([^:/]*):?([0-9]*)(/?.*)$");
    if port ~= nil and port == "443" then
      url = "https://" .. url
      schema = "https"
    else
      url = temp_url
    end
  end

  local expected_certificate_name = host or ''
  local starttime = mtev.timeval.now()

  check.bad()
  check.unavailable()

  local output = ''
  local callbacks = {}
  callbacks.consume = function (str)
    output = output .. (str or '') 
  end

  if host == nil then host = check.target end
  if schema == nil then
      schema = 'http'
      uri = '/'
  end
  if uri == '' then
      uri = '/'
  end
  if port == '' or port == nil then
      if schema == 'http' then
          port = 80
      elseif schema == 'https' then
          port = 443
      else
          error(schema .. " not supported")
      end
  end
  if schema == 'https' then
      use_ssl = true
  else
      use_ssl = false
  end

  local method = "GET"
  local headers = {}
  headers.Host = host

  -- setup SSL info
  local default_ca_chain =
      mtev.conf_get_string("/noit/eventer/config/default_ca_chain")
  callbacks.certfile = function () return config.certificate_file end
  callbacks.keyfile = function () return config.key_file end
  callbacks.cachain = function ()
      return config.ca_chain and config.ca_chain
                                    or default_ca_chain
  end
  callbacks.ciphers = function () return config.ciphers end

  local client = HttpClient:new(callbacks)
  local target = check.target_ip

  local rv, err = client:connect(target, port, use_ssl)

  if rv ~= 0 then
    check.status(string.format("Failed to connect to %s on %d: %s", target, port, err))
    return
  end

  client:do_request(method, uri, headers)
  client:get_response()

  if client.code ~= 200 then
    check.status(string.format("Failed to get stats with a %d", client.code))
    return
  end

  local endtime = mtev.timeval.now()

  local active, accepted, handled, requests, reading, writing, waiting = string.match(output, "^Active connections: (%d*) %D*(%d*)%s-(%d*)%s-(%d*)%s-Reading: (%d*) Writing: (%d*) Waiting: (%d*)")
  check.metric("active", active)
  check.metric("accepted", accepted)
  check.metric("handled", handled)
  check.metric("requests", requests)
  check.metric("reading", reading)
  check.metric("writing", writing)
  check.metric("waiting", waiting)

  local seconds = elapsed(check, "duration", starttime, endtime)
  local status = string.format("retrieved stats in %d seconds", seconds)
  local good = true
  -- ssl ctx
  local ssl_ctx = client:ssl_ctx()
  if ssl_ctx ~= nil then
    local header_match_error = nil
    if expected_certificate_name ~= '' then
      header_match_error = mtev.extras.check_host_header_against_certificate(expected_certificate_name, ssl_ctx.subject, ssl_ctx.san_list)
    end
    if ssl_ctx.error ~= nil then status = status .. ',sslerror' end
    if header_match_error == nil then
      check.metric_string("cert_error", ssl_ctx.error)
    elseif ssl_ctx.error == nil then
      check.metric_string("cert_error", header_match_error)
    else
      check.metric_string("cert_error", ssl_ctx.error .. ', ' .. header_match_error)
    end
    check.metric_string("cert_issuer", ssl_ctx.issuer)
    check.metric_string("cert_subject", ssl_ctx.subject)
    if ssl_ctx.san_list ~= nil then
      check.metric_string("cert_subject_alternative_names", ssl_ctx.san_list)
    end
    check.metric_uint32("cert_start", ssl_ctx.start_time)
    check.metric_uint32("cert_end", ssl_ctx.end_time)
    check.metric_int32("cert_end_in", ssl_ctx.end_time - os.time())
    if mtev.timeval.seconds(starttime) > ssl_ctx.end_time then
      good = false
      status = status .. ',ssl=expired'
    end
  end
  
  check.available()

  if good == false then
    check.bad()
  else
    check.good()
  end

  check.status(status)
end
