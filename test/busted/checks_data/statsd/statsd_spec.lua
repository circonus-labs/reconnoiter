local ffi = require("ffi")
local libnoit = ffi.load("noit")
ffi.cdef([=[
int noit_check_log_b_to_sm(const char *, int, char ***, int);
]=])

describe("noit", function()
  local port = 44323
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("noit", {
      modules = { statsd = { image = "statsd", config = { port = port } } }
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
    <name>statsd</name>
    <filterset>allowall</filterset>
    <module>statsd</module>
  </attributes>
  <config xmlns:histogram="noit://module/histogram">
  </config>
</check>]=]

  local payload = [=[test;env=prod;node=foo:1|g
test;node=foo,env=prod:1.2|g
test,node=foo,env=prod:18446744073709551614|g
times:1|c
times:9|c
times:1|c|@0.01
explicit_histogram:1|ms
explicit_histogram:2|ms
explicit_histogram:3|ms
explicit_histogram:4|ms|@0.0083333
]=]
  local witnessed_stats = {}
  local expected_stats = {}
  expected_stats["test|ST[env:prod,node:foo,statsd_type:gauge]"] = { _type = "n", _value = "1.844674407371e+19" }
  expected_stats["explicit_histogram|ST[statsd_type:timing]"] = { _type = "h", _value = "AAQKAAABFAAAAR4AAAEoAAB4" }
  expected_stats["times|ST[statsd_type:count]"] = { _type = "h", _value = "AAEAAABu" }

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("statsd", function()
    local key = noit:watchfor(mtev.pcre('H1\t'), true)
    local tkey = noit:watchfor(mtev.pcre('B[F12]\t'), true)
    it("put", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      assert.is.equal(200, code)
    end)
    it("accepts payload", function()
      local s = mtev.socket("127.0.0.1", "udp")
      s:connect("127.0.0.1", port)
      s:send(payload);
    end)
    it("has matching H1 records", function()
      local keys = {}
      -- split by \t, make sure we have the parts, and track #4 as the metric name
      for hc = 1,2 do
        local record = noit:waitfor(key, 2)
        assert.is_not_nil(record)
        local rparts = record:split('\t')
        assert.is_not_nil(rparts[4])
        assert.is_not_nil(rparts[5])
        rparts[5] = string.gsub(rparts[5], "%s$", "")
        keys[rparts[4]] = true
        witnessed_stats[rparts[4]] = { _type = "h", _value = rparts[5] }
      end
    end)

    it("has matching B records", function()
      local rcnt = 0

      for ri = 1,2 do
        local out = ffi.new("char **[?]", 1)
        local line = noit:waitfor(tkey, 2)
        assert.is_not_nil(line)
        line = string.gsub(line, "^.+(B[12F]\t)", "%1")
        local cnt = libnoit.noit_check_log_b_to_sm(line, string.len(line), out, -1)
        for i = 1,cnt do
          assert.is_not_nil(out[0][i-1])
          local mrec = ffi.string(out[0][i-1]):split('\t')
          if mrec[1] == 'M' then
            assert.is_not_nil(mrec[4])
            assert.is_not_nil(mrec[5])
            assert.is_not_nil(mrec[6])
            witnessed_stats[mrec[4]] = { _type = mrec[5], _value = mrec[6] }
          end
        end
      end
      -- if we got our value, we return and miss this
    end)

    it("saw the right values", function()
      assert.is_same(expected_stats, witnessed_stats)
    end)
  end)
end)
