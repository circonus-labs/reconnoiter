describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("trap", {
      modules = { httptrap = { image = "httptrap", config = { asynch_metrics = "true" } } }
    })
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local uuid = mtev.uuid()
  local check_xml =
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <period>1000</period>
    <timeout>500</timeout>
    <name>httptrap</name>
    <filterset>allowall</filterset>
    <module>httptrap</module>
  </attributes>
  <config xmlns:histogram="noit://module/histogram">
    <secret>foofoo</secret>
    <histogram:value name="explicit_histogram">add</histogram:value>
  </config>
</check>]=]

  local payload = [=[{
    "array": [ 1, 1.2, "string", { "_type": "s", "_value": "100" },
               { "_type": "L", "_value": 18446744073709551614 } ],
    "lvl1": {
      "lvl2": {
        "boolean": true
      }
    },
    "explicit_histogram": { "_type": "n", "_value": [ 1,2,3,"H[4]=120" ] },
    "implicit_histogram": { "_type": "h", "_value": [ "H[4]=120", 3, 2, 1 ] }
  }]=]
  local expected_stats = {}
  expected_stats["array`0"] = { _type = "L", _value = "1" }
  expected_stats["array`1"] = { _type = "n", _value = "1.200000000000e+00" }
  expected_stats["array`2"] = { _type = "s", _value = "string" }
  expected_stats["array`3"] = { _type = "s", _value = "100" }
  expected_stats["array`4"] = { _type = "n", _value = "1.844674407371e+19" }
  expected_stats["explicit_histogram"] = { _type = "H", _value = "AAQKAAABFAAAAR4AAAEoAAB4" }
  expected_stats["implicit_histogram"] = expected_stats["explicit_histogram"]
  expected_stats["lvl1`lvl2`boolean"] = { _type = "i", _value = "1" }

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("httptrap", function()
    local key = noit:watchfor(mtev.pcre('H1\t'), true)
    it("put", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      assert.is.equal(200, code)
    end)
    it("rejects payload", function()
      local code, doc = api:json("POST", "/module/httptrap/" .. uuid .. "/nopenope", payload)
      assert.is.not_equal(200, code)
    end)
    it("accepts payload", function()
      local code, doc, raw = api:json("POST", "/module/httptrap/" .. uuid .. "/foofoo", payload)
      assert.is.equal(200, code)
    end)
    it("has matching H1 records", function()
      local record = {}
      local rparts = {}
      local keys = {}
      -- split by \t, make sure we have the parts, and track #4 as the metric name
      for i=1,2 do
        record[i] = noit:waitfor(key, 2)
        assert.is_not_nil(record[i])
        rparts[i] = record[i]:split('\t')
        assert.is_not_nil(rparts[i][4])
        assert.is_not_nil(rparts[i][5])
        keys[rparts[i][4]] = true
      end
      assert.is.same({ explicit_histogram = true, implicit_histogram = true }, keys)
      -- the histograms should be the same.
      assert.is.equal(rparts[1][5], rparts[2][5])
    end)
    it("is reporting xml/json", function()
      local metrics, xmetrics = {}, {}
      local code, doc, xml
      -- we need to line this up to fire right after an aligned 1s boundary b/c
      -- the histogram stats will roll... ten tries at a 1.1 second stutter
      for i=1,10 do
        code, doc = api:json("POST", "/module/httptrap/" .. uuid .. "/foofoo", payload)
        assert.is.equal(200, code)
        mtev.sleep(1.1)
        code, doc = api:json("GET", "/checks/show/" .. uuid .. ".json")
        assert.is.equal(200, code)
        code, xml, stuff = api:xml("GET", "/checks/show/" .. uuid)
        for node in xml:xpath("//metrics/metric") do
          if xmetrics[node:attr("name")] == nil then
            xmetrics[node:attr("name")] = { _type = node:attr("type"), _value = node:contents() }
          end
        end
        assert.is.equal(200, code)
        for i, key in ipairs({ 'previous', 'current', 'inprogress' }) do
          if doc.metrics[key] ~= nil then
            for k,v in pairs(doc.metrics[key]) do
              metrics[k] = v
            end
          end
        end
        if metrics.implicit_histogram ~= nil and metrics.explicit_histogram ~= nil and
           xmetrics.implicit_histogram ~= nil and xmetrics.explicit_histogram ~= nil then
          break
        end
      end
      assert.is.same(expected_stats, metrics)
      assert.is.same(expected_stats, xmetrics)
    end)
  end)
end)
