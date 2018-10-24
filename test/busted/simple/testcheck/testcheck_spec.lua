describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("internal", {
      generics = { check_test = { image = "check_test" },
                   lua_web = { image = "lua_mtev" } },
      modules = { dns = { image = "dns" },
                  http = { loader = "lua", object = "noit.module.http" }, 
                  interp = { loader = "lua", object = "noit.test.interp" } }
    })
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local check_no = 0
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

  describe("https", function()
    local capa, start, elapsed
    it("resolves ip", function()
      local code, obj = api:json("POST", "/checks/test.json", check_xml("localhost", "interp", { url = "https://localhost:43191/" }))
      assert.is.equal(200, code)
      assert.same("127.0.0.1", obj.target_ip)
    end)
  end)
end)
