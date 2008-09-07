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
               allowed="\d+">Specifies the EHLO parameter.</parameter>
    <parameter name="from" required="optional" default=""
               allowed="\d+">Specifies the envelope sender.</parameter>
    <parameter name="to" required="required"
               allowed="\d+">Specifies the envelope recipient.</parameter>
    <parameter name="payload" required="optional" default="Subject: Testing"
               allowed="\d+">Specifies the payload sent (on the wire). CR LF DOT CR LF is appended automatically.</parameter>
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
    local str = e.read("\r\n")
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
  e.write(cmd);
  e.write("\r\n");
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
  local e = noit.socket()
  local rv, err = e.connect(check.target, check.config.port or 25)
  check.unavailable()

  if rv ~= 0 then
    check.bad()
    check.status(err or message or "no connection")
    return
  end

  local ehlo = string.format("EHLO %s", check.config.ehlo or "noit.local")
  local mailfrom = string.format("MAIL FROM:<%s>", check.config.from or "")
  local rcptto = string.format("RCPT TO:<%s>", check.config.to)
  local payload = check.config.payload or "Subject: Test\n\nHello."
  payload = payload:gsub("\n", "\r\n")
  local action = mkaction(e, check)
  if     action("banner", nil, 220)
     and action("ehlo", ehlo, 250)
     and action("mailfrom", mailfrom, 250)
     and action("rcptto", rcptto, 250)
     and action("data", "DATA", 354)
     and action("body", payload .. "\r\n.", 250)
     and action("quit", "QUIT", 221)
  then
    check.status("mail sent")
    check.good()
  end
end

