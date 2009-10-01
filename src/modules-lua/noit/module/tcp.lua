-- Copyright (c) 2009, OmniTI Computer Consulting, Inc.
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
  <name>tcp</name>
  <description><para>TCP metrics check.</para></description>
  <loader>lua</loader>
  <object>noit.module.tcp</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="required"
               allowed="\d+">Specifies the port on which the management interface can be reached.</parameter>
    <parameter name="banner_match" required="optional"
              allowed=".+">This regular expression is matched against the response banner.  If a match is not found, the check will be marked as bad.</parameter>
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
      <title>Checking TCP connection.</title>
      <para>This example checks IMAP connection with and without SSL.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="tcp" object="noit.module.tcp" />
        </modules>
        <checks>
          <imaps target="10.0.7.2" module="tcp" period="10000" timeout="5000">
            <check uuid="79ba881e-ad2e-11de-9fb0-a322e3288ca7" name="imap">
              <config>
                <port>143</port>
                <banner_match>^\* OK</banner_match>
              </config>
            </check>
            <check uuid="a18659c2-add8-11de-bd01-7ff0e1a67246" name="imaps">
              <config>
                <port>993</port>
                <banner_match>^\* OK</banner_match>
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

function initiate(module, check)
  local starttime = noit.timeval.now()
  local max_len = 80
  check.bad()
  check.unavailable()
  check.status("unknown error")
  local good = false
  local status = ""
  local use_ssl = false

  if check.config.port == nil then
    check.status("port is not specified")
    return
  end

  -- SSL
  if check.config.use_ssl == "true" or check.config.use_ssl == "on" then
    use_ssl = true
  end

  local e = noit.socket()
  local rv, err = e:connect(check.target, check.config.port)

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
  if check.config.banner_match ~= nil then
    str = e:read("\n")
    local firstbytetime = noit.timeval.now()
    elapsed(check, "tt_firstbyte", starttime, firstbytetime)

    local bannerre = noit.pcre(check.config.banner_match)
    if bannerre ~= nil then
      local rv, m, m1 = bannerre(str)
      if rv then
        m = m1 or m or str
        if string.len(m) > max_len then
          m = string.sub(m,1,max_len)
        end
        status = status .. ',banner_match=matched'
        check.metric_string('banner_match',m)
      else
        good = false
        status = status .. ',banner_match=failed'
      end
    end
    if string.len(str) > max_len then
      str = string.sub(str,1,max_len)
    end
    check.metric_string('banner',str)
  end

  -- turnaround time
  local endtime = noit.timeval.now()
  local seconds = elapsed(check, "duration", starttime, endtime)
  status = status .. ',rt=' .. seconds .. 's'
  if good then check.good() else check.bad() end
  check.status(status)

end

