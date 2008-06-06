-- This connects to a Varnish instance on the management port (8081)
-- It issues the stats comment and translates the output into metrics

module(..., package.seeall)

function onload(image)
  print("I'm in varnish onload")
  return 0
end

function init(module)
  print("I'm in varnish init")
  return 0
end

function config(module, options)
  print("I'm in varnish config")
  return 0
end

function initiate(module, check)
  local e = noit.socket()
  local rv, err = e.connect(check.target, check.config.port or 8081)

  e.write("stats\r\n")
  str = e.read("\n")

  if rv ~= 0 or not str then
    check.bad()
    check.unavailable()
    check.status(err or str or "unknown error")
    return
  end

  local status, len = string.match(str, "^(%d+)%s*(%d+)%s*$")
  if status then check.available() end
  -- we want status to be a number
  status = 0 + status
  if status ~= 200 then
    check.bad()
    check.status(string.format("status %d", status))
    return
  end

  local rawstats = e.read(len)
  local i = 0
  for v, k in string.gmatch(rawstats, "%s*(%d+)%s+([^\r\n]+)") do
    k = string.gsub(k, "^%s*", "")
    k = string.gsub(k, "%s*$", "")
    k = string.gsub(k, "%s", "_")
    check.metric(k,v)
    i = i + 1
  end
  check.status(string.format("%d stats", i))
  check.good()
end

