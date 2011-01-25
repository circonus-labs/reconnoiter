-- Copyright (c) 2011, OmniTI Computer Consulting, Inc.
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
  <name>imap</name>
  <description><para>IMAP metrics check.</para></description>
  <loader>lua</loader>
  <object>noit.module.imap</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="required"
               allowed="\d+">Specifies the port on which the management interface can be reached.</parameter>
    <parameter name="auth_user" required="required"
              allowed=".+">The IMAP user.</parameter>
    <parameter name="auth_password" required="required"
              allowed=".+">The IMAP password.</parameter>
    <parameter name="folder" required="optional" default="INBOX"
              allowed=".+">The folder that should be examined.</parameter>
    <parameter name="search" required="optional"
              allowed=".+">Specify an optional IMAP SEARCH operation to execute after EXAMINE</parameter>
    <parameter name="fetch" required="optional" default="false"
              allowed="(?:true|false|on|off)">Fetch either that highest UID or last SEARCH result.</parameter>
    <parameter name="use_ssl" required="optional" allowed="^(?:true|false|on|off)$" default="false">Upgrade TCP connection to use SSL.</parameter>
    <parameter name="ca_chain"
               required="optional"
               allowed=".+">A path to a file containing all the certificate authorities that should be loaded to validat
e the remote certificate (for SSL checks).</parameter>
    <parameter name="certificate_file"
               required="optional"
               allowed=".+">A path to a file containing the client certificate that will be presented to the remote serv
er (for SSL checks).</parameter>
    <parameter name="key_file"
               required="optional"
               allowed=".+">A path to a file containing key to be used in conjunction with the cilent certificate (for S
SL checks).</parameter>
    <parameter name="ciphers"
               required="optional"
               allowed=".+">A list of ciphers to be used in the SSL protocol (for SSL checks).</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking IMAP connection.</title>
      <para>This example checks IMAP connection with and without SSL.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="imap" object="noit.module.imap" />
        </modules>
        <checks>
          <imaps target="10.0.7.2" module="imap" period="10000" timeout="5000">
            <check uuid="79ba881e-ad2e-11de-9fb0-a322e3288ca7" name="imap">
              <config>
                <port>143</port>
                <auth_user>bob</auth_user>
                <auth_password>bob</auth_password>
              </config>
            </check>
            <check uuid="a18659c2-add8-11de-bd01-7ff0e1a67246" name="imaps">
              <config>
                <port>993</port>
                <auth_user>bob</auth_user>
                <auth_password>bob</auth_password>
                <use_ssl>true</use_ssl>
              </config>
            </check>
          </imaps>
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
    local elapsedtime = endtime - starttime
    local seconds = string.format('%.3f', noit.timeval.seconds(elapsedtime))
    check.metric_uint32(name, math.floor(seconds * 1000 + 0.5))
    return seconds
end

function get_banner(e)
  local line = e:read("\n")
  local o,eidx,state,rest
  o,eidx,state,rest = line:find('[*]%s+(%w+)%s*(.*)')
  return state, rest
end

function issue_cmd(e, tok, str)
  local t = {}
  local bad = {}
  e:write(tok .. " " .. str .. "\r\n")
  while true do
    local line = e:read("\n")
    line = line:gsub('[\r\n]','')
    local o,eidx,p1,p2
    o,eidx,p1,p2 = line:find(tok .. '%s+(%w+)%s*(.*)')
    if o == 1 then
      table.insert(t,p1.." "..p2)
      return p1, t, bad
    end
    o,eidx,p1 = line:find("\*%s+(.+)")
    if o == 1 then table.insert(t,p1) else table.insert(bad,line) end
  end
  return nil, t, bad
end

function initiate(module, check)
  local starttime = noit.timeval.now()
  local folder = check.config.folder or 'INBOX'
  check.bad()
  check.unavailable()
  check.status("unknown error")
  local port = check.config.port
  local good = false
  local status = ""
  local use_ssl = false
  local _tok = 0
  local last_msg = 0

  if check.target_ip == nil then
    check.status("dns resolution failure")
    return
  end

  function tok()
    _tok = _tok + 1
    return '.rN.A.' .. _tok
  end

  -- SSL
  if check.config.use_ssl == "true" or check.config.use_ssl == "on" then
    use_ssl = true
  end

  if port == nil then
    if use_ssl then port = 993 else port = 143 end
  end

  local e = noit.socket(check.target_ip)
  local rv, err = e:connect(check.target_ip, port)

  if rv ~= 0 then
    check.status(err or "connect error")
    return
  end

  if use_ssl == true then
    rv, err = e:ssl_upgrade_socket(check.config.certificate_file,
                                        check.config.key_file,
                                        check.config.ca_chain,
                                        check.config.ciphers)
  end 

  local connecttime = noit.timeval.now()
  elapsed(check, "tt_connect", starttime, connecttime)
  if rv ~= 0 then
    check.status(err or "connection failed")
    return
  end

  status = "connected"
  check.available()
  good = true

  -- ssl metrics
  local ssl_ctx = e:ssl_ctx()
  if ssl_ctx ~= nil then
    if ssl_ctx.error ~= nil then status = status .. ',sslerror' end
    check.metric_string("cert_error", ssl_ctx.error)
    check.metric_string("cert_issuer", ssl_ctx.issuer)
    check.metric_string("cert_subject", ssl_ctx.subject)
    check.metric_uint32("cert_start", ssl_ctx.start_time)
    check.metric_uint32("cert_end", ssl_ctx.end_time)
    if noit.timeval.seconds(starttime) > ssl_ctx.end_time then
      good = false
      status = status .. ',ssl=expired'
    end
  end

  -- match banner
  local ok, banner
  ok, banner = get_banner(e)
  local firstbytetime = noit.timeval.now()
  elapsed(check, "tt_firstbyte", starttime, firstbytetime)
  check.metric_string('banner', ok)
  if ok ~= "OK" then
    check.metric_string('banner', ok .. " " .. banner)
    good = false
  end
  local state, lines, errors

  -- login
  local lstart = noit.timeval.now()
  state, lines, errors = issue_cmd(e, tok(), "LOGIN " ..
                                             check.config.auth_user .. " " ..
                                             check.config.auth_password)
  elapsed(check, "login`duration", lstart, noit.timeval.now())
  check.metric_string("login`status", lines[ # lines ])
  if state ~= "OK" then good = false
  else
    -- Examine the mailbox
    local estart = noit.timeval.now()
    state, lines, errors = issue_cmd(e, tok(), "EXAMINE " .. folder)
    elapsed(check, 'examine`duration', estart, noit.timeval.now())
    check.metric_string('examine`status', lines[ # lines ])
    if ok ~= "OK" then good = false
    else
      local num, type, num_exists, num_recent = nil
      for i,v in ipairs(lines) do
        num, type = v:match("(%d+)%s+(.+)")
        if type == "EXISTS" then num_exists = num
        elseif type == "RECENT" then num_recent = num
        end
      end
      last_msg = num_exists
      check.metric_uint32('messages`total', num_exists)
      check.metric_uint32('messages`recent', num_recent)
    end
  end

  if check.config.search ~= nil then
    last_msg = nil
    local search = check.config.search:gsub("[\r\n]", "")
    local sstart = noit.timeval.now()
    state, lines, errors = issue_cmd(e, tok(), "SEARCH " .. search)
    elapsed(check, "search`duration", sstart, noit.timeval.now())
    if ok ~= "OK" then good = false
    else
      local matches = 0
      for i,v in ipairs(lines) do
        local msgs = v:match("SEARCH%s+(.+)")
        if msgs ~= nil then
          for m in msgs:gmatch("(%d+)") do
            matches = matches + 1
            last_msg = m + 0
          end
        end
      end
      check.metric_uint32('search`total', matches)
    end
  end

  if check.config.fetch ~= nil and
     (check.config.fetch == "true" or check.config.fetch == "on") and
     last_msg ~= nil and
     last_msg > 0 then
    local fstart = noit.timeval.now()
    state, lines, errors = issue_cmd(e, tok(), "FETCH " .. last_msg .. " RFC822")
    elapsed(check, "fetch`duration", fstart, noit.timeval.now())
    check.metric_string('fetch`status', state)
  end

  -- bye
  state, lines, errors = issue_cmd(e, tok(), "LOGOUT")
  check.metric_string('logout', state)

  -- turnaround time
  local endtime = noit.timeval.now()
  local seconds = elapsed(check, "duration", starttime, endtime)
  status = status .. ',rt=' .. seconds .. 's'
  if good then check.good() else check.bad() end
  check.status(status)
end

