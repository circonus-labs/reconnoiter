describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("tags", {
      generics = { check_test = { image = "check_test" },
                   lua_web = { image = "lua_mtev" } },
      modules = { tags = { loader = "lua", object = "noit.test.tags" } }
    })
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local uuid = mtev.uuid()
  local check_no = 1
  local filter_xml = function(m, st)
    return [=[<?xml version="1.0" encoding="utf8"?>
      <filterset>
        <rule type="allow" metric="]=] .. m .. [=[" stream_tags="]=] .. st .. [=[" />
        <rule type="deny"  />
      </filterset>]=]
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
    <filterset>testing</filterset>
    <module>]=] .. module .. [=[</module>
  </attributes>
  <config>]=] .. conf_str .. [=[</config>
</check>]=]
  end


  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  function filter_test(name, m, t, expected)
    local filter = filter_xml(m, t)
    local code, doc, raw = api:raw("PUT", "/filters/set/testing", filter)
    assert.message(raw).is.equal(200, code)
    code, obj, raw = api:json("POST", "/checks/test.json", check_xml("foo", "tags", { }))
    assert.message(raw).is.equal(200, code)
    for k,v in pairs(obj.metrics.current) do
      assert.message(name .. " key " .. k).is_not_nil(expected[k])
      for a,av in pairs(expected[k]) do
        assert.message(name .. " key " .. k .. "." .. a)
          .is.equal(expected[k][a], obj.metrics.current[k][a])
      end
    end
  end

  describe("filter test", function()
    local capa, start, elapsed
    it("filters tags correctly", function()
      local expect = {}
      expect["metric1|ST[env:prod,type:foo]"] = { _filtered = true }
      expect["metric2|ST[env:dev,type:foo]"] = { _filtered = true }
      expect["metric3|ST[env:prod,type:debug]"] = { }
      expect["metric4|ST[env:staging]"] = { _filtered = true }
      expect["metric5"] = { _filtered = true }
      filter_test("metric and tags", "[13579]$", "and(env:prod,not(type:foo))", expect)
    end)
  end)
end)
