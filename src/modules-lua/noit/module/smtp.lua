-- This connects to a Varnish instance on the management port (8081)
-- It issues the stats comment and translates the output into metrics

module(..., package.seeall)

function onload(image)
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
    if tosend then
      write_cmd(e, tosend)
    end
    local actual_code, message = read_cmd(e)
    if expected_code ~= actual_code then
      check.status(string.format("%d/%d %s", expected_code, actual_code, message))
      check.bad()
      return false
    end
    check.available()
    return true
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

  local mailfrom = string.format("MAIL FROM:<%s>", check.config.from or "")
  local rcptto = string.format("RCPT TO:<%s>", check.config.to)
  local action = mkaction(e, check)
  if     action("banner", nil, 220)
     and action("mail from", mailfrom, 250)
     and action("rcpt to", rcptto, 250)
     and action("data", "DATA", 354)
     and action("body", "Subject: Test\r\n\r\nHello.\r\n.", 250)
     and action("quit", "QUIT", 221)
  then
    check.status("mail sent")
    check.good()
  end
end

