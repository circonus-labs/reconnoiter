-- Copyright (c) 2012, OmniTI Computer Consulting, Inc.
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
  <name>pop3</name>
  <description><para>POP3 metrics check.</para></description>
  <loader>lua</loader>
  <object>noit.module.pop3</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="required"
               allowed="\d+">Specifies the port on which the management interface can be reached.</parameter>
    <parameter name="auth_user" required="required"
              allowed=".+">The POP3 user.</parameter>
    <parameter name="auth_password" required="required"
              allowed=".+">The POP3 password.</parameter>
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
    <parameter name="expected_certificate_name"
               required="optional"
               allowed=".+">The expected certificate name to validate against the SSL certificate (for SSL checks).</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking POP3 connection.</title>
      <para>This example checks the POP connection.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="imap" object="noit.module.pop3" />
        </modules>
        <checks>
          <check target="10.0.7.2" module="pop3" name="pop3" uuid="79ba881e-ad2e-11de-9fb0-a322e3288ca7" period="10000" timeout="5000">
            <config>
              <port>110</port>
              <auth_user>bob</auth_user>
              <auth_password>bob</auth_password>
              <use_ssl>false</use_ssl>
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

function elapsed(check, name, starttime, endtime)
    local elapsedtime = endtime - starttime
    local seconds = string.format('%.3f', noit.timeval.seconds(elapsedtime))
    check.metric_uint32(name, math.floor(seconds * 1000 + 0.5))
    return seconds
end

function get_banner(e)
  local line = e:read("\n")
  if line == nil then
    return nil, nil
  end
  local o,eidx,state,rest
  o,eidx,state,rest = line:find('[+,-]%s*(%w+)%s*(.*)')
  return state, rest
end

function issue_cmd(e, str)
  e:write(str .. "\r\n")
  while true do
    local line = e:read("\n")
    line = line:gsub('[\r\n]','')
    local o,eidx,p1,p2
    o,eidx,p1,p2 = line:find('[+,-]%s*(%w+)%s*(.*)')
    if o == 1 then
      return p1, p1 .. " " .. p2
    end
  end
  return nil, ""
end

function initiate(module, check)
  local starttime = noit.timeval.now()
  check.bad()
  check.unavailable()
  check.status("unknown error")
  local port = check.config.port
  local good = false
  local status = ""
  local use_ssl = false
  local expected_certificate_name = check.config.expected_certificate_name or ''

  if check.target_ip == nil then
    check.status("dns resolution failure")
    return
  end

  -- SSL
  if check.config.use_ssl == "true" or check.config.use_ssl == "on" then
    use_ssl = true
  end

  if port == nil then
    if use_ssl then port = 995 else port = 110 end
  end

  local e = noit.socket(check.target_ip)
  local rv, err = e:connect(check.target_ip, port)

  if rv ~= 0 then
    check.status(err or "connect error")
    return
  end

  local ca_chain = 
     noit.conf_get_string("/noit/eventer/config/default_ca_chain")

  if check.config.ca_chain ~= nil and check.config.ca_chain ~= '' then
    ca_chain = check.config.ca_chain
  end

  if use_ssl == true then
    rv, err = e:ssl_upgrade_socket(check.config.certificate_file,
                                        check.config.key_file,
                                        ca_chain,
                                        check.config.ciphers)
  end 

  if rv ~= 0 then
    check.status(err or "connection failed")
    return
  end

  local connecttime = noit.timeval.now()
  elapsed(check, "tt_connect", starttime, connecttime)

  status = "connected"
  check.available()
  good = true

  -- ssl metrics
  local ssl_ctx = e:ssl_ctx()
  if ssl_ctx ~= nil then
    local header_match_error = nil
    if expected_certificate_name ~= '' then
      header_match_error = noit.extras.check_host_header_against_certificate(expected_certificate_name, ssl_ctx.subject, ssl_ctx.san_list)
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
  local state, line

  -- login
  local lstart = noit.timeval.now()
  state, line = issue_cmd(e, "USER " .. check.config.auth_user)
  state, line = issue_cmd(e, "PASS " .. check.config.auth_password)
  elapsed(check, "login`duration", lstart, noit.timeval.now())
  check.metric_string("login`status", line)
  if state ~= "OK" then good = false
  else
    -- Run a STAT command and parse it into metrics
    local estart = noit.timeval.now()
    state, line = issue_cmd(e, "STAT")
    elapsed(check, 'stat`duration', estart, noit.timeval.now())
    check.metric_string('stat`status', line)
    if state ~= "OK" then good = false
    else
      local subfields = noit.extras.split(line, "%s+")
      if subfields ~= nil and subfields[2] ~= nil then
        check.metric_uint32("message_count", subfields[2])
      end
    end
  end

  -- quit 
  if state ~= "OK" then good = false
  else
    state, line = issue_cmd(e, "QUIT")
    check.metric_string('quit', state)
  end

  -- turnaround time
  local endtime = noit.timeval.now()
  local seconds = elapsed(check, "duration", starttime, endtime)
  status = status .. ',rt=' .. seconds .. 's'
  if good then check.good() else check.bad() end
  check.status(status)
end

