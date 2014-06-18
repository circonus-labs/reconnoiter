module(..., package.seeall)
function onload(image)
  image.xml_description('')
  return 0
end

function init(module)
  return 0
end

function config(module, options)
  return 0
end

function initiate(modules, check)
  local target_type
  local failed = false
  check.bad()
  check.unavailable()
  if check.target == 'fe80::7ed1:c3ff:fedc:ddf7' then
    target_type = 'ipv6'
  elseif check.target == '192.168.19.12' then
    target_type = 'ipv4'
  else
    check.status("target " .. check.config.target .. " unsupported")
    return
  end

  if check.config.key == nil then
    check.status("must set config.key to something")
    return
  end

  local input = {
    broken = 'testing.%[:broken:name]',
    copy = 'testing.%{key}',
    name = 'testing.%[name]',
    module = 'testing.%[module]',
    ccns = 'testing.%[:ccns:target]', -- used b/c ip6 has ::
    reverseip = 'testing.%[:reverseip:target]',
    inaddrarpa = 'testing.%[:inaddrarpa:target]',
    randint = '%[:random:integer]',
    randuuid = '%[:random:uuid]',
    randbroken = '%[:random:magic]'
  }
  local expected = {
    broken = 'testing.%[:broken:name]',
    copy = 'testing.' .. check.config.key,
    name = 'testing.' .. check.name,
    module = 'testing.' .. check.module,
    ccns = target_type == 'ipv6'
       and 'testing.7ed1:c3ff:fedc:ddf7'
        or 'testing.192.168.19.12',
    reverseip = target_type == 'ipv6'
           and 'testing.7.f.d.d.c.d.e.f.f.f.3.c.1.d.e.7.0.0.0.0.0.0.0.0.0.0.0.0.0.8.e.f'
            or 'testing.12.19.168.192',
    inaddrarpa = target_type == 'ipv6'
           and 'testing.7.f.d.d.c.d.e.f.f.f.3.c.1.d.e.7.0.0.0.0.0.0.0.0.0.0.0.0.0.8.e.f.ip6.arpa'
            or 'testing.12.19.168.192.in-addr.arpa',
    randint = function(a) return string.find(a, "^%d+$") ~= nil end,
    randuuid = function(a) return string.find(a, "^%x%x%x%x%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%x%x%x%x%x%x%x%x$") ~= nil end,
    randbroken = 'random_what'
  }
  local output = check.interpolate(input)
  check.status("tested")
  for k, v in pairs(input) do
    local success = false
    if type(expected[k]) == 'function' then
      local f = expected[k]
      success = f(output[k])
    else
      success = output[k] == expected[k]
    end
    if not success then
      check.status("failed")
      noit.log("error", "'%s' ~= '%s'\n", output[k], expected[k])
      check.metric_string(k, "FAILURE: " .. output[k])
      failed = true
    else
      check.metric_string(k, "SUCCESS: " .. output[k])
    end
  end
  if not failed then check.good() end
  check.available()
end
