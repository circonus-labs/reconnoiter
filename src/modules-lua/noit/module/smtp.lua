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
  <name>smtp</name>
  <description><para>Send an email via an SMTP server.</para></description>
  <loader>lua</loader>
  <object>noit.module.smtp</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="optional" default="25"
               allowed="\d+">Specifies the TCP port to connect to.</parameter>
    <parameter name="ehlo" required="optional" default="noit.local"
               allowed=".+">Specifies the EHLO parameter.</parameter>
    <parameter name="from" required="optional" default=""
               allowed=".+">Specifies the envelope sender.</parameter>
    <parameter name="to" required="optional"
               allowed=".+">Specifies the envelope recipient, if blank issue quit.</parameter>
    <parameter name="payload" required="optional" default="Subject: Testing"
               allowed=".+">Specifies the payload sent (on the wire). CR LF DOT CR LF is appended automatically.</parameter>
    <parameter name="starttls" required="optional" default="false"
               allowed="(?:true|false)">Specified if the client should attempt a STARTTLS upgrade</parameter>
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
    <parameter name="sasl_authentication"
               required="optional"
               default="off"
               allowed="(?:off|login|plain)">Specifies the type of SASL Authentication to use</parameter>
    <parameter name="sasl_user"
               required="optional"
               default=""
               allowed=".+">The SASL Authentication username</parameter>
    <parameter name="sasl_password"
               required="optional"
               default=""
               allowed=".+">The SASL Authentication password</parameter>
    <parameter name="sasl_auth_id"
               required="optional"
               default=""
               allowed=".+">The SASL Authorization Identity</parameter>
    <parameter name="proxy_protocol"
               required="optional"
               default="false"
               allowed="(?:true|false)">Test MTA responses to a PROXY protocol header by setting this to true    </parameter>
    <parameter name="proxy_family"
               required="optional"
               default="TCP4"
               allowed="(?:TCP4|TCP6)">The protocol family to send in the PROXY header</parameter>
    <parameter name="proxy_source_address"
               required="optional"
               default=""
               allowed=".+">The IP (or string) to use as the source address portion of the PROXY protocol.  More on the proxy protocol here: http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt</parameter>
    <parameter name="proxy_dest_address"
               required="optional"
               default=""
               allowed=".+">The IP (or string) to use as the destination address portion of the PROXY protocol.  More on the proxy protocol here: http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt</parameter>
    <parameter name="proxy_source_port"
               required="optional"
               default=""
               allowed="\d+">The port to use as the source port portion of the PROXY protocol.  Defaults to the actual source port of the connection to the target_ip</parameter>
    <parameter name="proxy_dest_port"
               required="optional"
               default=""
               allowed="\d+">The port to use as the dest port portion of the PROXY protocol.  Defaults to the port setting or 25</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Send an email to test SMTP service.</title>
      <para>The following example sends an email via 10.80.117.6 from test@omniti.com to devnull@omniti.com</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="smtp" object="noit.module.smtp"/>
        </modules>
        <checks>
          <check uuid="2d42adbc-7c7a-11dd-a48f-4f59e0b654d3" module="smtp" target="10.80.117.6">
            <config>
              <from>test@omniti.com</from>
              <to>devnull@omniti.com</to>
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

local function read_cmd(e)
  local final_status, out
  final_status, out = 0, ""
  repeat
    local str = e:read("\r\n")
    if not str then
      return 421, "[internal error]"
    end
    local status, c, message = string.match(str, "^(%d+)([-%s])(.+)$")
    if not status then
      return 421, "[internal error]"
    end
    final_status = status
    if string.len(out) > 0 then
      out = string.format( "%s %s", out, message)
    else
      out = message
    end
  until c ~= "-"
  return (final_status+0), out
end

local function write_cmd(e, cmd)
  e:write(cmd);
  e:write("\r\n");
end

local function mkaction(e, check)
  return function (phase, tosend, expected_code, track)
    local start_time = mtev.timeval.now()
    local success = true
    if tosend then
      write_cmd(e, tosend)
    end
    local actual_code, message = read_cmd(e)
    if track then
      check.metric_string("last_cmd", phase)
      check.metric_string("last_code", actual_code)
      check.metric_string("last_message", message and string.gsub(message, "[\r\n]+$", "") or nil)
    end
    if expected_code ~= actual_code then
      check.status(string.format("%d/%d %s", expected_code, actual_code, message))
      check.bad()
      success = false
    else
      check.available()
    end
    local elapsed = mtev.timeval.now() - start_time
    local elapsed_ms = math.floor(tostring(elapsed) * 1000)
    check.metric(phase .. "_time",  elapsed_ms)

    if phase == 'ehlo' and message ~= nil then
      local fields = mtev.extras.split(message, "\r\n")
      if fields ~= nil then
        local response = ""
        local extensions = ""
        if fields[1] ~= nil then
          response = fields[1]
          check.metric("ehlo_response_banner", response)
        end
        if expected_code == actual_code and fields[1] ~= nil then
          table.remove(fields, 1)
          for line, value in pairs(fields) do
            if value ~= nil and value ~= "" then
              value = value:gsub("^%s*(.-)%s*$", "%1")
              local subfields = mtev.extras.split(value, "%s+", 1)
              if subfields ~= nil and subfields[1] ~= nil then
                local header = subfields[1]
                if subfields[2] ~= nil then
                  check.metric("ehlo_response_" .. string.lower(header), subfields[2])
                else
                  check.metric("ehlo_response_" .. string.lower(header), 'true')
                end
              end
            end
          end
        end
      end
    end
    return success
  end
end

local function mk_sasllogin(e, check)
  return function (username, password) 
    local start_time = mtev.timeval.now()
    local actual_code = 0
    local message = ""
    local success = "true"
    write_cmd(e, "AUTH LOGIN")
    actual_code, message = read_cmd(e)
    if actual_code ~= 334 then
      success = "false"
    end
    if success == "true" then
      write_cmd(e, username)
      actual_code, message = read_cmd(e)
      if actual_code ~= 334 then
        success = "false"
      end
    end
    if success == "true" then
      write_cmd(e, password)
      actual_code, message = read_cmd(e)
      if actual_code ~= 235 then
        success = "false"
      end
    end
    local elapsed = mtev.timeval.now() - start_time
    local elapsed_ms = math.floor(tostring(elapsed) * 1000)
    check.metric("sasl_login_time",  elapsed_ms)
    check.metric("sasl_login_success", success)
    check.metric("sasl_login_response", message)
    return success
  end
end

local function mk_saslplain(e, check)
  return function (cmd_string)
    local start_time = mtev.timeval.now()
    local actual_code = 0
    local message = ""
    local success = "true"
    write_cmd(e, "AUTH PLAIN")
    actual_code, message = read_cmd(e)
    if actual_code ~= 334 then
      success = "false"
    end
    if success == "true" then
      write_cmd(e, cmd_string)
      actual_code, message = read_cmd(e)
      if actual_code ~= 235 then
        success = "false"
      end
    end
    local elapsed = mtev.timeval.now() - start_time
    local elapsed_ms = math.floor(tostring(elapsed) * 1000)
    check.metric("sasl_plain_time",  elapsed_ms)
    check.metric("sasl_plain_success", success)
    check.metric("sasl_plain_response", message)
    return success
  end
end

function ex_actions(action, check, config, mailfrom, rcptto, payload)
  local status = ''
  -- Only proceed if from is present, empty or not
  if config.from == "" or config.from then
   if not action("mailfrom", mailfrom, 250, true) then
     return status
   end
  else
   return status
  end

  if config.to then
    if not action("rcptto", rcptto, 250, true) then
      return status
    end
  else
    return status
  end

  -- Since the way the protocol works, the to address may be in the payload...
  if payload then
    if action("data", "DATA", 354, true) then
      if action("body", payload .. "\r\n.", 250, true) then
        status = ',sent'
      else
        status = ',unsent'
      end
    end
  end
  return status
end

function initiate(module, check)
  local config = check.interpolate(check.config)
  local starttime = mtev.timeval.now()
  local e = mtev.socket(check.target_ip)
  local rv, err = e:connect(check.target_ip, config.port or 25)
  local action_result
  check.unavailable()

  if rv ~= 0 then
    check.bad()
    check.status(err or message or "no connection")
    return
  end

  if config.proxy_protocol == "true" then
    -- write a PROXY protocol header as very first thing
    -- see: http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
    -- using VERSION 1 of the protocol
    local my_ip, my_port = e:sock_name()
    local proxy_header = string.format("PROXY %s %s %s %s %d", config.proxy_family or "TCP4",
                                       config.proxy_source_address or my_ip,
                                       config.proxy_dest_address or check.target_ip,
                                       config.proxy_source_port or my_port,
                                       config.proxy_dest_port or config.port or 25)
    write_cmd(e, proxy_header)
  end

  local try_starttls = config.starttls == "true" or config.starttls == "on"
  local good = true
  local ehlo = string.format("EHLO %s", config.ehlo or "noit.local")
  local mailfrom = string.format("MAIL FROM:<%s>", config.from or "")
  local rcptto = string.format("RCPT TO:<%s>", config.to or "")
  local payload = config.payload or "Subject: Test\n\nHello."
  payload = payload:gsub("\n", "\r\n")
  local status = 'connected'
  local action = mkaction(e, check)
  local sasl_login = mk_sasllogin(e, check)
  local sasl_plain = mk_saslplain(e, check)

  -- setup SSL info
  local default_ca_chain =
      mtev.conf_get_string("/noit/eventer/config/default_ca_chain")
  local certfile = config.certificate_file or nil
  local keyfile = config.key_file or nil
  local cachain = config.ca_chain or default_ca_chain
  local ciphers = config.ciphers or nil

  if     not action("banner", nil, 220, true)
      or not action("ehlo", ehlo, 250, true) then return end

  if try_starttls then
    local starttls  = action("starttls", "STARTTLS", 220, true)
    if not starttls then
      check.unavailable()
      check.status("Could not start TLS for this target")
      return
    end
    e:ssl_upgrade_socket(certfile, keyfile, cachain, ciphers)
    local ssl_ctx = e:ssl_ctx()
    if ssl_ctx ~= nil then
      if ssl_ctx.error ~= nil then status = status .. ',sslerror' end
      check.metric_string("cert_error", ssl_ctx.error)
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

    if not action("ehlo", ehlo, 250, true) then return end
  end

  if config.sasl_authentication ~= nil then
    if config.sasl_authentication == "login" then
      sasl_login(mtev.base64_encode(config.sasl_user or ""), mtev.base64_encode(config.sasl_password or ""))
    elseif config.sasl_authentication == "plain" then
      sasl_plain(mtev.base64_encode((config.sasl_auth_id or "") .. "\0" .. (config.sasl_user or "") .. "\0" .. (config.sasl_password or "")))
    end
  end

  action_result = ex_actions(action, check, config, mailfrom, rcptto, payload)
  -- Always issue quit
  action("quit", "QUIT", 221, false)

  status = status .. action_result

  check.status(status)
  if good then check.good() end

  local elapsed = mtev.timeval.now() - starttime
  local elapsed_ms = math.floor(tostring(elapsed) * 1000)
  check.metric("duration",  elapsed_ms)
end

