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
    if st ~= nil then st = [=[ stream_tags="]=] .. st .. [=[" ]=] else st = '' end
    if type(m) == "string" then
      return [=[<?xml version="1.0" encoding="utf8"?>
        <filterset>
          <rule type="allow" metric="]=] .. m .. [=[" ]=] .. st .. [=[></rule>
          <rule type="deny"  />
        </filterset>]=]
    else
      local xml = [=[<?xml version="1.0" encoding="utf8"?>
        <filterset>
          <rule type="allow"]=] .. st .. [=[>]=]
      for k,v in pairs(m) do xml = xml .. '<metric>' .. k .. '</metric>' end
      xml = xml .. [=[</rule><rule type="deny"  />
        </filterset>]=]
      return xml
    end
  end
  local check_xml = function(target, module, filterset, config)
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
    <filterset>]=] .. filterset .. [=[</filterset>
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
    local uuid = mtev.uuid()
    local code, doc, raw = api:raw("PUT", "/filters/set/" .. uuid, filter)
    assert.message(raw).is.equal(200, code)
    code, obj, raw = api:json("POST", "/checks/test.json", check_xml("localhost", "tags", uuid, { }))
    assert.message(raw).is.equal(200, code)
    for k,v in pairs(obj.metrics.current) do
      assert.message(name .. " key " .. k .. " is in expected set").is_not_nil(expected[k])
      for a,av in pairs(expected[k]) do
        assert.message(name .. " key " .. k .. "." .. a)
          .is.equal(expected[k][a], obj.metrics.current[k][a])
      end
    end
    for k,v in pairs(expected) do
      assert.message(name .. " key " .. k .. " is in results").is_not_nil(obj.metrics.current[k])
      for a,av in pairs(obj.metrics.current[k]) do
        assert.message(name .. " key " .. k .. "." .. a)
          .is.equal(expected[k][a], obj.metrics.current[k][a])
      end
    end
  end

  describe("filter test tags", function()

    it("filters tags correctly with stream expressions", function()
      local expect = {}
      expect["metric1|ST[env:prod,type:foo]"] = { _filtered = true, _type = "i", _value = "1"}
      expect["metric2|ST[env:dev,type:foo]"] = { _filtered = true, _type = "i", _value = "1" }
      expect["metric3|ST[env:prod,type:debug]"] = { _type = "i", _value = "1" }
      expect["metric4|ST[env:staging]"] = { _filtered = true, _type = "i", _value = "1" }
      expect["metric5"] = { _filtered = true, _type = "i", _value = "1" }
      filter_test("metric and tags", "[13579]$", "and(env:prod,not(type:foo))", expect)
    end)

    it("filters tags correctly with stream expressions without metric", function()
      local expect = {}
      expect["metric1|ST[env:prod,type:foo]"] = { _filtered = true, _type = "i", _value = "1"}
      expect["metric2|ST[env:dev,type:foo]"] = { _filtered = true, _type = "i", _value = "1" }
      expect["metric3|ST[env:prod,type:debug]"] = { _type = "i", _value = "1" }
      expect["metric4|ST[env:staging]"] = { _filtered = true, _type = "i", _value = "1" }
      expect["metric5"] = { _filtered = true, _type = "i", _value = "1" }
      filter_test("metric and tags", {}, "and(env:prod,not(type:foo))", expect)
    end)

    it("filters tags correctly with stream expressions", function()
      local expect = {}
      expect["metric1|ST[env:prod,type:foo]"] = { _type = "i", _value = "1"}
      expect["metric2|ST[env:dev,type:foo]"] = { _filtered = true, _type = "i", _value = "1" }
      expect["metric3|ST[env:prod,type:debug]"] = { _type = "i", _value = "1" }
      expect["metric4|ST[env:staging]"] = { _filtered = true, _type = "i", _value = "1" }
      expect["metric5"] = { _type = "i", _value = "1" }
      local allow = {}
      allow["metric1|ST[env:prod,type:foo]"] = true
      allow["metric3|ST[type:debug,env:prod]"] = true -- non-canonical
      allow["metric5|ST[]"] = true
      filter_test("metric canonical names", allow, nil, expect)
    end)

    it("filters auto-add style metrics", function()
      local filter_uuid, check_uuid = mtev.uuid(), mtev.uuid()
      local code, doc, raw = api:raw("PUT", "/filters/set/" .. filter_uuid,
        [=[<?xml version="1.0" encoding="utf8"?>
        <filterset filter_flush_period="1000">
          <rule type="allow" metric_auto_add="2"/>
          <rule type="deny"/>
        </filterset>]=])
      assert.message(raw).is.equal(200, code)

      local key = noit:watchfor(mtev.pcre('B[12F].*' .. check_uuid))
      local flushed = noit:watchfor(mtev.pcre("flushed auto_add rule " .. filter_uuid))

      code, obj, raw = api:raw("PUT", "/checks/set/" .. check_uuid,
        [=[<?xml version="1.0" encoding="utf8"?>
        <check>
          <attributes>
            <target>localhost</target>
            <period>100</period>
            <timeout>50</timeout>
            <name>]=] .. check_uuid .. [=[</name>
            <filterset>]=] .. filter_uuid .. [=[</filterset>
            <module>tags</module>
          </attributes>
          <config></config>
        </check>]=])
      assert.message(raw).is.equal(200, code)

      local row = noit:waitfor(key, 5)
      assert.message("check has run").is_not_nil(row)

      local runs = {}
      local run_count = 0
      for i=1,10 do
        local code, doc = api:json("GET", "/checks/show/" .. check_uuid .. ".json") 
        assert.message("show check").is.equal(200, code)
        if runs[tostring(doc.last_run)] == nil then
          run_count = run_count + 1
          runs[tostring(doc.last_run)] = true
        end
        local allowed = 0
        if doc.metrics ~= nil and doc.metrics.current ~= nil then
          for k,v in pairs(doc.metrics.current) do if v._filtered ~= true then allowed = allowed + 1 end end
        end
        -- We could have flushed the auto_add in the middle of eval and thus got double the allowed metrics.
        assert.message("filters stick").is_true(allowed >= 2 and allowed <= 4)
        mtev.sleep(0.1)
      end

      assert.message("has run more than once").is_true(run_count > 1)

      assert.message("auto_add rule has flushed more than once").is_not_nil(noit:waitfor(flushed, 0))

    end)

  end)
end)
