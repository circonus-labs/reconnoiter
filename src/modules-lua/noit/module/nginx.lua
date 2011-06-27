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
  <desccription><para>The nginx module gathers information from the nginx stub_status module</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.nginx</module>
  <checkconfig>
    <parameter name="url"
               required="required"
               allowed=".+">The URL including schema and hostname for the status output from nginx.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Monitor an nginx server with a status page available at http://10.1.2.3/nginx_status</status>
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
      ]]</programlisting>
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
  local seconds = string.format("%.3f", noit.timeval.seconds(e))
  check.metric_uint32(name, math.floor(seconds * 1000 +0.5))
  return seconds
end

local HttpClient = require 'noit.HttpClient'

function initiate(module, check)
  local url = check.config.url
  local host, port, uri = string.match(url, "^http://([^:/]*):?([0-9]*)(/?.*)$");

  local good = false
  local starttime = noit.timeval.now()

  check.bad()
  check.unavailable()

  local output = ''
  local callbacks = {}
  callbacks.consume = function (str)
    output = output .. (str or '') 
  end

  if host == nil then host = check.target end
  if uri == '' then
    uri = '/'
  end
  if (port == nil or port == '') then port = 80 end

  local method = "GET"
  local headers = {}
  headers.Host = host


  local client = HttpClient:new(callbacks)
  local target = check.target_ip

  local rv, err = client:connect(target, port)

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

  local endtime = noit.timeval.now()
  check.available()

  local active, accepted, handled, requests, reading, writing, waiting = string.match(output, "^Active connections: (%d*) %D*(%d*)%s-(%d*)%s-(%d*)%s-Reading: (%d*) Writing: (%d*) Waiting: (%d*)")
  check.metric("active", active)
  check.metric("accepted", accepted)
  check.metric("handled", handled)
  check.metric("requests", requests)
  check.metric("reading", reading)
  check.metric("writing", writing)
  check.metric("waiting", waiting)

  local seconds = elapsed(check, "duration", starttime, endtime)
  
  check.status(string.format("retrieved stats in %d seconds", seconds))
  check.good()
end
