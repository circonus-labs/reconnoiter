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
               allowed=".+">This regular expression is matched against the body of the response.  If a match is not found, the check will be marked as "bad."</parameter>
    <parameter name="extract"
               required="optional"
               allowed=".+">This regular expression is matched against the body of the response globally.  The first capturing match is the key and the second capturing match is the value.  Each key/value extracted is registered as a metric for the check.</parameter>
    <parameter name="pcre_match_limit"
               required="optional"
               default="10000"
               allowed="\d+">This sets the PCRE internal match limit (see pcreapi documentation).</parameter>
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

function elapsed(check, name, starttime, endtime)
    local elapsedtime = endtime - starttime
    local seconds = string.format('%.3f', noit.timeval.seconds(elapsedtime))
    check.metric_uint32(name, math.floor(seconds * 1000 + 0.5))
    return seconds
end

function rand_string(t, l)
    local n = table.getn(t)
    local o = ''
    while l > 0 do
      o = o .. t[math.random(1,n)]
      l = l - 1
    end
    return o
end

function auth_digest(method, uri, user, pass, challenge)
    local c = ', ' .. challenge
    local nc = '00000001'
    local cnonce =
        rand_string({'a','b','c','d','e','f','g','h','i','j','k','l','m',
                     'n','o','p','q','r','s','t','u','v','x','y','z','A',
                     'B','C','D','E','F','G','H','I','J','K','L','M','N',
                     'O','P','Q','R','S','T','U','V','W','X','Y','Z','0',
                     '1','2','3','4','5','6','7','8','9'}, 8)
    local p = {}
    for k,v in string.gmatch(c, ',%s+(%a+)="([^"]+)"') do p[k] = v end
    for k,v in string.gmatch(c, ',%s+(%a+)=([^",][^,]*)') do p[k] = v end

    -- qop can be a list
    for q in string.gmatch(p.qop, '([^,]+)') do
        if q == "auth" then p.qop = "auth" end
    end

    -- calculate H(A1)
    local ha1 = noit.md5_hex(user .. ':' .. p.realm .. ':' .. pass)
    if string.lower(p.qop or '') == 'md5-sess' then
        ha1 = noit.md5_hex(ha1 .. ':' .. p.nonce .. ':' .. cnonce)
    end
    -- calculate H(A2)
    local ha2 = ''
    if p.qop == "auth" or p.qop == nil then
        ha2 = noit.md5_hex(method .. ':' .. uri)
    else
        -- we don't support auth-int
        error("qop=" .. p.qop .. " is unsupported")
    end
    local resp = ''
    if p.qop == "auth" then
        resp = noit.md5_hex(ha1 .. ':' .. p.nonce .. ':' .. nc
                                .. ':' .. cnonce .. ':' .. p.qop
                                .. ':' .. ha2)
    else
        resp = noit.md5_hex(ha1 .. ':' .. p.nonce .. ':' .. ha2)
    end
    local o = {}
    o.username = user
    o.realm = p.realm
    o.nonce = p.nonce
    o.uri = uri
    o.cnonce = cnonce
    o.qop = p.qop
    o.response = resp
    o.algorithm = p.algorithm
    if p.opaque then o.opaque = p.opaque end
    local hdr = ''
    for k,v in pairs(o) do
      if hdr == '' then hdr = k .. '="' .. v .. '"' 
      else hdr = hdr .. ', ' .. k .. '="' .. v .. '"' end
    end
    hdr = hdr .. ', nc=' .. nc
    return hdr
end

function populate_cookie_jar(cookies, host, hdr)
    if hdr ~= nil then
        local name, value, trailer =
            string.match(hdr, "([^=]+)=([^;]+)\;?%s*(.*)")
        if name ~= nil then
            local jar = { }
            jar.name = name;
            jar.value = value;
            for k, v in string.gmatch(trailer, "%s*(%w+)(=%w+)?;?") do
                if v == nil then jar[string.lower(k)] = true
                else jar[string.lower(k)] = v:sub(2)
                end
            end
            if jar.domain ~= nil then host = jar.domain end
            if cookies[host] == nil then cookies[host] = { } end
            table.insert(cookies[host], jar)
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
    for h, jars in pairs(cookies) do
        if has_host(h, host) then
            for i, jar in ipairs(jars) do
                if jar.path == nil or
                   uri:sub(1, jar.path:len()) == jar.path then
                    if headers["Cookie"] == nil then
                        headers["Cookie"] = jar.name .. "=" .. jar.value
                    else
                        headers["Cookie"] = headers["Cookie"] .. "; " ..
                                            jar.name .. "=" .. jar.value
                    end
                end
            end
        end
    end
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

    -- expect the worst
    check.bad()
    check.unavailable()

    if host == nil then host = check.target end
    if schema == nil then
        schema = 'http'
        uri = '/'
    end
    if uri == '' then
        uri = '/'
    end
    if port == '' then
        if schema == 'http' then
            port = check.config.port or 80
        elseif schema == 'https' then
            port = check.config.port or 443
            use_ssl = true
        else
            error(schema .. " not supported")
        end 
    end

    local output = ''
    local connecttime, firstbytetime
    local next_location
    local cookies = { }

    -- callbacks from the HttpClient
    local callbacks = { }
    callbacks.consume = function (str)
        if firstbytetime == nil then firstbytetime = noit.timeval.now() end
        output = output .. (str or '')
    end
    callbacks.headers = function (hdrs)
        next_location = hdrs.location
        populate_cookie_jar(cookies, host, hdrs["set-cookie"])
        populate_cookie_jar(cookies, hdrs["set-cookie2"])
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
        local rv, err = client:connect(check.target, port, use_ssl)
        if rv ~= 0 then
            check.status(str or "unknown error")
            return
        end
        local headers_firstpass = {}
        for k,v in pairs(headers) do
            headers_firstpass[k] = v
        end
        client:do_request(method, uri, headers_firstpass)
        client:get_response()
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
                "Digest " .. auth_digest(method, uri,
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
    local target = check.target
    local payload = check.config.payload
    -- artificially increase redirects as the initial request counts
    redirects = redirects + 1
    repeat
        starttime = noit.timeval.now()
        local optclient = HttpClient:new(callbacks)
        local rv, err = optclient:connect(target, port, use_ssl)
       
        if rv ~= 0 then
            check.status(err or "unknown error")
            return
        end
        optclient:do_request(method, uri, headers, payload)
        optclient:get_response()

        redirects = redirects - 1
        client = optclient

        if next_location ~= nil then
            -- reset some stuff for the redirect
            local prev_port = port
            local prev_host = host
            method = 'GET'
            payload = nil
            schema, host, port, uri =
                string.match(next_location,
                             "^(https?)://([^:/]*):?([0-9]*)(/?.*)$")
            if schema == nil then
                port = prev_port
                host = prev_host
                uri = next_location
            elseif schema == 'http' then 
                use_ssl = false
                if port == "" then port = 80 end
            elseif schema == 'https' then
                use_ssl = true
                if port == "" then port = 443 end
            end
            if host ~= nil then
                headers.Host = host
                local r = dns:lookup(host)
                if r.a == nil then
                    check.status("failed to resolve " + host)
                    return
                end
                target = r.a
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

    -- turnaround time
    local seconds = elapsed(check, "duration", starttime, endtime)
    status = status .. ',rt=' .. seconds .. 's'
    elapsed(check, "tt_connect", starttime, connecttime)
    elapsed(check, "tt_firstbyte", starttime, firstbytetime)

    -- size
    status = status .. ',bytes=' .. client.content_bytes
    check.metric_int32("bytes", client.content_bytes)

    if check.config.extract ~= nil then
      local exre = noit.pcre(check.config.extract)
      local rv = true
      while rv do
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

    -- ssl ctx
    local ssl_ctx = client:ssl_ctx()
    if ssl_ctx ~= nil then
      if ssl_ctx.error ~= nil then status = status .. ',sslerror' end
      check.metric_string("cert_error", ssl_ctx.error)
      check.metric_string("cert_issuer", ssl_ctx.issuer)
      check.metric_string("cert_subject", ssl_ctx.subject)
      check.metric_uint32("cert_start", ssl_ctx.start_time)
      check.metric_uint32("cert_end", ssl_ctx.end_time)
      check.metric_uint32("cert_end_in", ssl_ctx.end_time - os.time())
      if noit.timeval.seconds(starttime) > ssl_ctx.end_time then
        good = false
        status = status .. ',ssl=expired'
      end
    end

    if good then check.good() else check.bad() end
    check.status(status)
end

