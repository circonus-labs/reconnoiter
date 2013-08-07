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
  <name>http</name>
  <description><para>The http module performs GET requests over either HTTP or HTTPS and checks the return code and optionally the body.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.http</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="header_(\S+)"
               required="optional"
               allowed=".+">Allows the setting of arbitrary HTTP headers in the request.</parameter>
    <parameter name="method"
               required="optional"
               allowed="\S+"
               default="GET">The HTTP method to use.</parameter>
    <parameter name="payload"
               required="optional"
               allowed=".*">The information transferred as the payload of an HTTP request.</parameter>
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
    <parameter name="code"
               required="optional"
               default="^200$"
               allowed=".+">The HTTP code that is expected.  If the code received does not match this regular expression, the check is marked as "bad."</parameter>
    <parameter name="redirects"
               required="optional"
               default="0"
               allowed="\d+">The maximum number of Location header redirects to follow.</parameter>
    <parameter name="body"
               required="optional"
               allowed=".+">This regular expression is matched against the body of the response. If a match is not found, the check will be marked as "bad."</parameter>
    <parameter name="body_match_*"
               required="optional"
               allowed=".+">This regular expression is matched against the body of the response. If a match is found it is captured and added as a metric. For example, if setting is named 'body_match_foo_bar' and a match is found new metric called 'foo_bar' will be added.</parameter>
    <parameter name="extract"
               required="optional"
               allowed=".+">This regular expression is matched against the body of the response globally.  The first capturing match is the key and the second capturing match is the value.  Each key/value extracted is registered as a metric for the check.</parameter>
    <parameter name="pcre_match_limit"
               required="optional"
               default="10000"
               allowed="\d+">This sets the PCRE internal match limit (see pcreapi documentation).</parameter>
    <parameter name="include_body"
               required="optional"
               allowed="^(?:true|false|on|off)$"
               default="false">Include whole response body as a metric with the key 'body'.</parameter>
    <parameter name="read_limit"
               required="optional"
               default="0"
               allowed="\d+">Sets an approximate limit on the data read (0 means no limit).</parameter>
    <parameter name="http_version"
               required="optional"
               default="1.1"
               allowed="^(\d+\.\d+)?$">Sets the HTTP version for the check to use.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking an HTTP and HTTPS URL.</title>
      <para>This example checks the OmniTI Labs website over both HTTP and HTTPS.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="http" object="noit.module.http" />
        </modules>
        <checks>
          <labs target="8.8.38.5" module="http">
            <check uuid="fe3e984c-7895-11dd-90c1-c74c31b431f0" name="http">
              <config><url>http://labs.omniti.com/</url></config>
            </check>
            <check uuid="1ecd887a-7896-11dd-b28d-0b4216877f83" name="https">
              <config><url>https://labs.omniti.com/</url></config>
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

local BODY_MATCHES_PREFIX = 'body_match_'

function elapsed(check, name, starttime, endtime)
    local elapsedtime = endtime - starttime
    local seconds = string.format('%.3f', noit.timeval.seconds(elapsedtime))
    check.metric_uint32(name, math.floor(seconds * 1000 + 0.5))
    return seconds
end

function populate_cookie_jar(cookies, host, hdr)
    local path = nil
    if hdr ~= nil then
        local name, value, trailer =
            string.match(hdr, "([^=]+)=([^;]+);?%s*(.*)")
        if name ~= nil then
            local jar = { }
            local fields = noit.extras.split(trailer, ";")
            if fields ~= nil then
                for k, v in pairs(fields) do
                    local pair = noit.extras.split(v, "=", 1);
                    if pair ~= nil and pair[1] ~= nil and pair[2] ~= nil then
                        local name = (string.gsub(pair[1], "^%s*(.-)%s*$", "%1"));
                        local setting = (string.gsub(pair[2], "^%s*(.-)%s*$", "%1"));
                        if name == "path" then
                            path = setting
                        end
                    end
                end
            end
            if string.sub(name, 1, 1) ~= ";" and string.sub(value, 1, 1) ~= ";" then
                if path == nil then path = "/" end
                if cookies[host] == nil then cookies[host] = { } end
                if cookies[host][path] == nil then cookies[host][path] = { } end
                jar.name = name
                jar.value = value
                table.insert(cookies[host][path], jar)
            end
        end
    end
end

function has_host(pat, host)
    if pat == host then return true end
    if pat:sub(1,1) ~= "." then return false end
    local revpat = pat:sub(2):reverse()
    local revhost = host:reverse()
    if revpat == revhost then return true end
    if revpat == revhost:sub(1, revpat:len()) then
        if revhost:sub(pat:len(), pat:len()) == "." then return true end
    end
    return false
end

function apply_cookies(headers, cookies, host, uri)
    local use_cookies = { }
    for h, paths in pairs(cookies) do
        if has_host(h, host) then
            local split_uri = noit.extras.split(uri, "/")
            if split_uri ~= nil then
                local path = ""
                for i, val in pairs(split_uri) do
                    local append = true
                    if val == nil then val = "" end
                    if #split_uri == i and string.find(val, "%.") ~= nil then append = false end
                    if append == true then
                        path = path .. "/" .. val
                        if string.len(path) >= 2 and string.sub(path, 1, 2) == "//" then
                            path = string.sub(path, 2)
                        end
                    end
                    if path == "" then path = "/" end
                    local rindex = string.match(path, '.*()'..'%?')
                    if rindex ~= nil then
                        path = string.sub(path, 1, rindex-1)
                    end
                    if path ~= "/" then
                        while string.find(path, "/", -1) ~= nil do
                            path = string.sub(path, 1, -2)
                        end
                    end
                    if paths[path] ~= nil then
                        local jars = paths[path]
                        for index, jar in ipairs(jars) do
                            use_cookies[jar.name] = jar.value
                        end
                    end
                end
            end
        end
    end
    for name, value in pairs(use_cookies) do
        if headers["Cookie"] == nil then
            headers["Cookie"] = name .. "=" .. value
        else
            headers["Cookie"] = headers["Cookie"] .. "; " .. name .. "=" .. value
        end
    end
end

function get_new_uri(old_uri, new_uri)
    if new_uri == nil then return "/" end
    if new_uri == "/" then return new_uri end
    local toReturn = old_uri
    while string.find(toReturn, "/", -1) ~= nil do
        toReturn = string.sub(toReturn, 1, -2)
    end
    if string.sub(new_uri, 1, 1) == '?' then
        local rindex = string.match(toReturn, '.*()'.."/")
        toReturn = string.sub(toReturn, 1, rindex-1)
        toReturn = toReturn .. new_uri
    elseif string.sub(new_uri, 1, 1) ~= "." then 
        toReturn = new_uri
    else
        toReturn = string.gsub(toReturn, "%/%?", "?")
        while string.sub(new_uri, 1, 1) == "." do
            if string.find(new_uri, "%./") == 1 then
                new_uri = string.gsub("%./", "", 1)
            elseif string.find(new_uri, "%.%./") == 1 then
                --strip out last bit from toReturn
                local rindex = string.match(toReturn, '.*()'.."/")
                toReturn = string.sub(toReturn, 1, rindex-1)
                new_uri = string.gsub(new_uri, "../", "", 1)
            else
                -- bad URI... just return /
                return "/"
            end
        end
        toReturn = toReturn .. "/" .. new_uri
    end
    return toReturn
end

function get_absolute_path(uri)
    if uri == nil or #uri == 0 then return "/" end
    local toReturn = uri
    local go_back = string.find(toReturn, "%.%./")
    while go_back ~= nil do
        local tojoin = go_back + 3
        go_back = go_back - 2
        local back_substring = string.sub(toReturn, 1, go_back)
        local forward_substring = string.sub(toReturn, tojoin)
        local rindex = string.match(back_substring, '.*()' .. "/")
        if rindex ~= nil then
            toReturn = string.sub(toReturn, 1, rindex) .. forward_substring
        end
        go_back = string.find(toReturn, "%.%./")
    end
    toReturn = string.gsub(toReturn, "%./", "")
    return toReturn
end

function initiate(module, check)
    local url = check.config.url or 'http:///'
    local schema, host, port, uri = string.match(url, "^(https?)://([^:/]*):?([0-9]*)(/?.*)$");
    local use_ssl = false
    local codere = noit.pcre(check.config.code or '^200$')
    local good = false
    local starttime = noit.timeval.now()
    local method = check.config.method or "GET"
    local max_len = 80
    local pcre_match_limit = check.config.pcre_match_limit or 10000
    local redirects = check.config.redirects or 0
    local include_body = false
    local read_limit = tonumber(check.config.read_limit) or nil
    local host_header = check.config.header_Host
    local http_version = check.config.http_version or '1.1'

    -- expect the worst
    check.bad()
    check.unavailable()

    if host == nil then host = check.target end
    if host_header == nil then host_header = host end
    if schema == nil then
        schema = 'http'
        uri = '/'
    end
    if uri == '' then
        uri = '/'
    end
    if port == '' or port == nil then
        if schema == 'http' then
            port = check.config.port or 80
        elseif schema == 'https' then
            port = check.config.port or 443
        else
            error(schema .. " not supported")
        end
    end
    if schema == 'https' then
        use_ssl = true
    end

    -- Include body as a metric
    if check.config.include_body == "true" or check.config.include_body == "on" then
        include_body = true
    end

    local output = ''
    local connecttime, firstbytetime
    local next_location
    local cookies = { }
    local setfirstbyte = 1

    -- callbacks from the HttpClient
    local callbacks = { }
    callbacks.consume = function (str)
        if setfirstbyte == 1 then
            firstbytetime = noit.timeval.now()
            setfirstbyte = 0
        end
        output = output .. (str or '')
    end
    callbacks.headers = function (hdrs, setcookies)
        next_location = hdrs.location
        for key, value in pairs(setcookies) do
            populate_cookie_jar(cookies, host, value)
        end
    end

    callbacks.connected = function () connecttime = noit.timeval.now() end

    -- setup SSL info
    local default_ca_chain =
        noit.conf_get_string("/noit/eventer/config/default_ca_chain")
    callbacks.certfile = function () return check.config.certificate_file end
    callbacks.keyfile = function () return check.config.key_file end
    callbacks.cachain = function ()
        return check.config.ca_chain and check.config.ca_chain
                                      or default_ca_chain
    end
    callbacks.ciphers = function () return check.config.ciphers end

    -- set the stage
    local headers = {}
    headers.Host = host
    for header, value in pairs(check.config) do
        hdr = string.match(header, '^header_(.+)$')
        if hdr ~= nil then
          headers[hdr] = value
        end
    end
    if check.config.auth_method == "Basic" then
        local user = check.config.auth_user or ''
        local password = check.config.auth_password or ''
        local encoded = noit.base64_encode(user .. ':' .. password)
        headers["Authorization"] = "Basic " .. encoded
    elseif check.config.auth_method == "Digest" or
           check.config.auth_method == "Auto" then
        -- this is handled later as we need our challenge.
        local client = HttpClient:new()
        local rv, err = client:connect(check.target_ip, port, use_ssl, host_header)
        if rv ~= 0 then
            check.status(err or "unknown error in HTTP connect for Auth")
            return
        end
        local headers_firstpass = {}
        for k,v in pairs(headers) do
            headers_firstpass[k] = v
        end
        client:do_request(method, uri, headers_firstpass, http_version)
        client:get_response(read_limit)
        if client.code ~= 401 or
           client.headers["www-authenticate"] == nil then
            check.status("expected digest challenge, got " .. client.code)
            return
        end
        local user = check.config.auth_user or ''
        local password = check.config.auth_password or ''
        local ameth, challenge =
            string.match(client.headers["www-authenticate"], '^(%S+)%s+(.+)$')
        if check.config.auth_method == "Auto" and ameth == "Basic" then
            local encoded = noit.base64_encode(user .. ':' .. password)
            headers["Authorization"] = "Basic " .. encoded
        elseif ameth == "Digest" then
            headers["Authorization"] =
                "Digest " .. client:auth_digest(method, uri,
                                         user, password, challenge)
        else
            check.status("Unexpected auth '" .. ameth .. "' in challenge")
            return
        end
    elseif check.config.auth_method ~= nil then
      check.status("Unknown auth method: " .. check.config.auth_method)
      return
    end

    -- perform the request
    local client
    local dns = noit.dns()
    local target = check.target_ip
    local payload = check.config.payload
    -- artificially increase redirects as the initial request counts
    redirects = redirects + 1
    starttime = noit.timeval.now()
    repeat
        local optclient = HttpClient:new(callbacks)
        local rv, err = optclient:connect(target, port, use_ssl, host_header)
        if rv ~= 0 then
            check.status(err or "unknown error in HTTP connect")
            return
        end
        optclient:do_request(method, uri, headers, payload, http_version)
        optclient:get_response(read_limit)
        setfirstbyte = 1

        redirects = redirects - 1
        client = optclient

        if redirects > 0 and next_location ~= nil then
            -- reset some stuff for the redirect
            local prev_port = port
            local prev_host = host
            local prev_uri = uri
            method = 'GET'
            payload = nil
            schema, host, port, uri =
                string.match(next_location,
                             "^(https?)://([^:/]*):?([0-9]*)(/?.*)$")
            if schema == nil then
                port = prev_port
                host = prev_host
                uri = get_new_uri(prev_uri, next_location)
            elseif schema == 'http' then
                use_ssl = false
                if port == "" then port = 80 end
            elseif schema == 'https' then
                use_ssl = true
                if port == "" then port = 443 end
            end
            uri = get_absolute_path(uri)
            if host ~= nil then
                headers.Host = host
                host_header = host
                local r = dns:lookup(host)
                if not r or r.a == nil then
                    check.status("failed to resolve " .. host)
                    return
                end
                target = r.a
            end
            while string.find(host, "/", -1) ~= nil do
                host = string.sub(host, 1, -2)
            end
            headers["Cookie"] = check.config["header_Cookie"]
            apply_cookies(headers, cookies, host, uri)
        end
    until redirects <= 0 or next_location == nil

    local endtime = noit.timeval.now()
    check.available()

    local status = ''
    -- setup the code
    check.metric_string("code", client.code)
    status = status .. 'code=' .. client.code
    if codere ~= nil and codere(client.code) then
      good = true
    end

    -- truncated response
    check.metric_uint32("truncated", client.truncated and 1 or 0)

    -- turnaround time
    local seconds = elapsed(check, "duration", starttime, endtime)
    status = status .. ',rt=' .. seconds .. 's'
    elapsed(check, "tt_connect", starttime, connecttime)

    if firstbytetime ~= nil then
      elapsed(check, "tt_firstbyte", starttime, firstbytetime)
    end

    -- size
    status = status .. ',bytes=' .. client.content_bytes
    check.metric_int32("bytes", client.content_bytes)

    if check.config.extract ~= nil then
      local exre = noit.pcre(check.config.extract)
      local rv = true
      local m = nil
      while rv and m ~= '' do
        rv, m, key, value = exre(output or '', { limit = pcre_match_limit })
        if rv and key ~= nil then
          check.metric(key, value)
        end
      end
    end

    -- check body
    if check.config.body ~= nil then
      local bodyre = noit.pcre(check.config.body)
      local rv, m, m1 = bodyre(output or '')
      if rv then
        m = m1 or m or output
        if string.len(m) > max_len then
          m = string.sub(m,1,max_len)
        end
        status = status .. ',body=matched'
        check.metric_string('body_match', m)
      else
        status = status .. ',body=failed'
        check.metric_string('body_match', nil)
        good = false
      end
    end

    -- check body matches
    local matches = 0
    has_body_matches = false
    for key, value in pairs(check.config) do
      local match = string.find(key, BODY_MATCHES_PREFIX)

      if match == 1 then
        has_body_matches = true
        key = string.gsub(key, BODY_MATCHES_PREFIX, '')

        local bodyre = noit.pcre(value)
        local rv, m, m1 = bodyre(output or '')

        if rv then
          matches = matches + 1
          m = m1 or m or output
          if string.len(m) > max_len then
            m = string.sub(m,1,max_len)
          end
          check.metric_string('body_match_' .. key, m)
        else
          check.metric_string('body_match_' .. key, nil)
        end
      end
    end

    if has_body_matches then
      status = status .. ',body_matches=' .. tostring(matches) .. ' matches'
    end

    -- Include body
    if include_body then
        check.metric_string('body', output or '')
    end

    -- ssl ctx
    local ssl_ctx = client:ssl_ctx()
    if ssl_ctx ~= nil then
      local header_match_error = nil
      if host_header ~= '' then
        header_match_error = noit.extras.check_host_header_against_certificate(host_header, ssl_ctx.subject, ssl_ctx.san_list)
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
      if noit.timeval.seconds(starttime) > ssl_ctx.end_time then
        good = false
        status = status .. ',ssl=expired'
      end
    end

    if good then check.good() else check.bad() end
    check.status(status)
end

