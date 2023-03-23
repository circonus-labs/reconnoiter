local ffi = require("ffi")
local libnoit = ffi.load("noit")
ffi.cdef([=[
int noit_check_log_b_to_sm(const char *, int, char ***, int);
]=])

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
  local accum_uuid = mtev.uuid()

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

local accum_check_xml =
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <period>900000</period>
    <timeout>5000</timeout>
    <name>httptrap_accum</name>
    <filterset>allowall</filterset>
    <module>httptrap</module>
  </attributes>
  <config xmlns:histogram="noit://module/histogram">
    <secret>accum</secret>
    <histogram:value name="explicit_histogram">add</histogram:value>
  </config>
</check>]=]

  local b64hist = "AAQKAAABFAAAAR4AAAEoAAB4"
  local b64hist_double = "AAQKAAACFAAAAh4AAAIoAADw"
  local payload = [=[{
    "array": [ 1, 1.2, "string", { "_type": "s", "_value": "100" },
               { "_type": "L", "_value": 18446744073709551614 },
               { "_type": "L", "_value": 1, "_value": 2 }],
    "lvl1": {
      "lvl2": {
        "boolean": true
      }
    },
    "explicit_histogram": { "_type": "n", "_value": [ 1,2,3,"H[4]=120" ] },
    "implicit_histogram": { "_type": "h", "_value": [ "H[4]=120", 3, 2, 1 ] },
    "cumulative_b64histogram_1": { "_type": "H", "_value": ["]=] .. b64hist .. [=[", "H[4]=120", 3, 2, 1 ] },
    "cumulative_b64histogram_2": { "_type": "H", "_value": ["H[4]=120", 3, 2, 1, "]=] .. b64hist .. [=["] },
    "implicit_b64histogram_1": { "_type": "h", "_value": ["H[4]=120", 3, 2, 1, "]=] .. b64hist .. [=[" ] }
  }]=]
  local accum_payload = [=[{
    "a": { "_type": "L", "_value": 125, "_fl": "+" },
    "b": { "_type": "L", "_value": 12345 },
    "c": 54321
  }]=]
  local expected_stats = {}
  expected_stats["array`0"] = { _type = "L", _value = "1" }
  expected_stats["array`1"] = { _type = "n", _value = "1.200000000000e+00" }
  expected_stats["array`2"] = { _type = "s", _value = "string" }
  expected_stats["array`3"] = { _type = "s", _value = "100" }
  expected_stats["array`4"] = { _type = "L", _value = "18446744073709551614" }
  expected_stats["array`5"] = { _type = "n", _value = "1.500000000000e+00" }
  expected_stats["explicit_histogram"] = { _type = "h", _value = b64hist }
  expected_stats["implicit_histogram"] = { _type = "h", _value = b64hist }
  expected_stats["cumulative_b64histogram_1"] = { _type = "H", _value = b64hist }
  expected_stats["cumulative_b64histogram_2"] = { _type = "H", _value = b64hist }
  expected_stats["implicit_b64histogram_1"] = { _type = "h", _value = b64hist_double }
  expected_stats["lvl1`lvl2`boolean"] = { _type = "i", _value = "1" }

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("httptrap", function()
    local key = noit:watchfor(mtev.pcre('H[12]\t'), true)
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
    it("has matching H1/2 records", function()
      local record = {}
      local rparts = {}
      local keys = {}
      local hist_cnt = {}
      -- split by \t, make sure we have the parts, and track #4 as the metric name
      for i=1,5 do
        record[i] = noit:waitfor(key, 2)
        assert.is_not_nil(record[i])
        record[i] = record[i]:gsub('\n','')
        rparts[i] = record[i]:split('\t')
        assert.is_not_nil(rparts[i][4])
        assert.is_not_nil(rparts[i][5])
        keys[rparts[i][4]] = true
        hist_cnt[rparts[i][5]] = (hist_cnt[rparts[i][5]] or 0) + 1
      end
      assert.is.same({ explicit_histogram = true, implicit_histogram = true,
                       implicit_b64histogram_1 = true, cumulative_b64histogram_1 = true,
                       cumulative_b64histogram_2 = true }, keys)
      -- the histograms should be the same.
      assert.is.equal(4, hist_cnt[b64hist])
      assert.is.equal(1, hist_cnt[b64hist_double])
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
            local val = node:contents()
            if val ~= nil and val ~= '' then
              xmetrics[node:attr("name")] = { _type = node:attr("type"), _value = node:contents() }
            end
          end
        end
        assert.is.equal(200, code)
        for i, key in ipairs({ 'previous', 'current', 'inprogress' }) do
          if doc.metrics[key] ~= nil then
            for k,v in pairs(doc.metrics[key]) do
              if v._value ~= nil and v._value ~= '' then
                metrics[k] = v
              end
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

    it("supports streaming json", function()
      local code, doc = api:json("PUT", "/module/httptrap/" .. uuid .. "/foofoo",
                                 [=[{"a":1}{"b":2}
                                    {"a":3,"b":4}
                                    {"c":5}]=])
      assert.is.equal(200,code)
      assert.is.equal(5,doc.stats)
    end)

    local tkey
    it("accepts _ts payload", function()
      -- construct a streamed payload.
      local payload = mtev.tojson(
         { timetest = { _ts = 1000, _type = "n", _value = "1000" } }):tostring()
        .. mtev.tojson(
         { timetest = { _ts = 2000, _type = "n", _value = "2000" } }):tostring()
        .. mtev.tojson(
         { timetest = { _ts = 3000, _type = "n", _value = "3000" } }):tostring()
        .. mtev.tojson(
         { timetest = { _ts = 4000, _type = "n", _value = "4000" } }):tostring()
      tkey = noit:watchfor(mtev.pcre('B[F12]\t'), true)
      local code, doc, raw = api:json("POST", "/module/httptrap/" .. uuid .. "/foofoo", payload)
      assert.is.equal(200, code)
    end)
    it("has matching B records", function()
      local record = {}
      local rparts = {}
      local keys = {}
      local rcnt = 0
      for r=1,10 do
        local out = ffi.new("char **[?]", 1)
        local line = noit:waitfor(tkey, 2)
        line = string.gsub(line, "^.+(B[12F]\t)", "%1")
        local cnt = libnoit.noit_check_log_b_to_sm(line, string.len(line), out, -1)
        for i = 1,cnt do
          assert.is_not_nil(out[0][i-1])
          local mrec = ffi.string(out[0][i-1]):split('\t')
          if mrec[1] == 'M' and mrec[4] == "timetest" then
            rcnt = rcnt+1
            rparts[rcnt] = mrec
            assert.is_not_nil(rparts[rcnt][4])
            assert.is_not_nil(rparts[rcnt][5])
            keys[rparts[rcnt][2]] = true
          end
        end
      end
      local expected = {}
      expected["1.000"] = true
      expected["2.000"] = true
      expected["3.000"] = true
      expected["4.000"] = true
      assert.same(keys, expected)
    end)
    it("handles accumulation", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. accum_uuid, accum_check_xml)
      assert.is.equal(200, code)
      for i=1,10 do
        code, doc = api:json("POST", "/module/httptrap/" .. accum_uuid .. "/accum", accum_payload)
        assert.is.equal(200, code)
        assert.is_equal(3, doc["stats"])
        assert.is_equal(0, doc["filtered"])
      end
      local code, xml = api:xml("GET", "/checks/show/" .. accum_uuid)
      assert.is_equal(200, code)
      local xmetrics = {}
      for node in xml:xpath("//metrics/metric") do
        if xmetrics[node:attr("name")] == nil then
          local val = node:contents()
          if val ~= nil and val ~= '' then
            xmetrics[node:attr("name")] = { _type = node:attr("type"), _value = node:contents() }
          end
        end
      end
      local expected = {}
      expected["a"] = {}
      expected["a"]["_type"] = "n"
      expected["a"]["_value"] = "1.250000000000e+03"
      expected["b"] = {}
      expected["b"]["_type"] = "L"
      expected["b"]["_value"] = "12345"
      expected["c"] = {}
      expected["c"]["_type"] = "L"
      expected["c"]["_value"] = "54321"
      assert.same(expected, xmetrics)
    end)
  end)
end)
