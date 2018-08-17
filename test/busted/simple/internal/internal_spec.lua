describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("internal", {
      generics = { check_test = { image = "check_test" },
                   lua_web = { image = "lua_mtev" } },
      modules = { dns = { image = "dns" },
                  interp = { loader = "lua", object = "noit.test.interp" } }
    })
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local check_no = 0
  function munge_metrics(doc)
    local recv = {}
    for k,v in pairs(doc.metrics.current) do
      recv[k] = string.gsub(v._value, ':.*', '')
    end
    return recv
  end
  local check_xml = function(target, module, config)
    local conf_arr = {}
    check_no = check_no + 1
    local testname = "test." .. tostring(check_no)
    for k,v in pairs(config) do table.insert(conf_arr, '<' .. k .. '>' .. v .. '</' .. k .. '>') end
    local conf_str = table.concat(conf_arr, '')
    return
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>]=] .. target .. [=[</target>
    <period>5000</period>
    <timeout>1000</timeout>
    <name>]=] .. testname .. [=[</name>
    <filterset>allowall</filterset>
    <module>]=] .. module .. [=[</module>
  </attributes>
  <config>]=] .. conf_str .. [=[</config>
</check>]=]
  end

  local interp_tests = { 'broken', 'copy', 'name', 'module', 'inaddrarpa', 'reverseip', 'ccns', 'randint', 'randuuid', 'randbroken' }
  local expected = {}
  for i, t in ipairs(interp_tests) do expected[t] = 'SUCCESS' end

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("internal test", function()
    local capa, start, elapsed
    it("ipv4", function()
      local code, obj = api:json("POST", "/checks/test.json", check_xml("192.168.19.12", "interp", { key = "foofoo" }))
      assert.is.equal(200, code)
      assert.same(expected, munge_metrics(obj))
    end)
    it("ipv4", function()
      local code, obj = api:json("POST", "/checks/test.json", check_xml("fe80::7ed1:c3ff:fedc:ddf7", "interp", { key = "quuxer" }))
      assert.is.equal(200, code)
      assert.same(expected, munge_metrics(obj))
    end)
  end)
end)
