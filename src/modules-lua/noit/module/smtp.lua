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
    <parameter name="to" required="required"
               allowed=".+">Specifies the envelope recipient.</parameter>
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
  return function (phase, tosend, expected_code)
    local start_time = noit.timeval.now()
    local success = true
    if tosend then
      write_cmd(e, tosend)
    end
    local actual_code, message = read_cmd(e)
    if expected_code ~= actual_code then
      check.status(string.format("%d/%d %s", expected_code, actual_code, message))
      check.bad()
      success = false
    else
      check.available()
    end
    local elapsed = noit.timeval.now() - start_time
    local elapsed_ms = math.floor(tostring(elapsed) * 1000)
    check.metric(phase .. "_time",  elapsed_ms)
    return success
  end
end

function initiate(module, check)
  local starttime = noit.timeval.now()
  local e = noit.socket()
  local rv, err = e:connect(check.target, check.config.port or 25)
  check.unavailable()

  if rv ~= 0 then
    check.bad()
    check.status(err or message or "no connection")
    return
  end

  local try_starttls = check.config.starttls == "true" or check.config.starttls == "on"
  local good = true
  local ehlo = string.format("EHLO %s", check.config.ehlo or "noit.local")
  local mailfrom = string.format("MAIL FROM:<%s>", check.config.from or "")
  local rcptto = string.format("RCPT TO:<%s>", check.config.to)
  local payload = check.config.payload or "Subject: Test\n\nHello."
  payload = payload:gsub("\n", "\r\n")
  local status = 'connected'
  local action = mkaction(e, check)

  if     not action("banner", nil, 220)
      or not action("ehlo", ehlo, 250) then return end

  if try_starttls then
    local starttls  = action("starttls", "STARTTLS", 220)
    e:ssl_upgrade_socket(check.config.certificate_file, check.config.key_file,
                         check.config.ca_chain, check.config.ciphers)

    local ssl_ctx = e:ssl_ctx()
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

    if not action("ehlo", ehlo, 250) then return end
  end

  if     action("mailfrom", mailfrom, 250)
     and action("rcptto", rcptto, 250)
     and action("data", "DATA", 354)
     and action("body", payload .. "\r\n.", 250)
     and action("quit", "QUIT", 221)
  then
    status = status .. ',sent'
  else
    return
  end
  check.status(status)
  if good then check.good() end

  local elapsed = noit.timeval.now() - starttime
  local elapsed_ms = math.floor(tostring(elapsed) * 1000)
  check.metric("duration",  elapsed_ms)
end

