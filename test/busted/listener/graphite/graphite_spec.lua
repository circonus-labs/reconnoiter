local socket = require("socket")
describe("graphite #graphite #listener", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("graphite", {
      modules = { graphite = { image = "graphite", config = { asynch_metrics = "true" } } }
    })
  end)
  teardown(function()
    if noit ~= nil then noit:stop() end
  end)

  local uuid = mtev.uuid()
  local check_xml =
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <period>1000</period>
    <timeout>500</timeout>
    <name>graphite</name>
    <filterset>allowall</filterset>
    <module>graphite</module>
  </attributes>
  <config>
    <listen_port>2003</listen_port>
  </config>
</check>]=]
  local payload = [=[
graphite.baz.1;app=blarg 3456.78 1557101182.262
graphite.baz.2;app=blarg 3456.78 1557101183.262
graphite.baz.3;app=blarg 3456.78 1557101184.262
graphite.baz.4;app=blarg 3456.78 1557101185.262
graphite.baz.5;app=blarg 3456.78 1557101186.262
]=]

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("graphite", function()
    local key = noit:watchfor(mtev.pcre('B1\t'), true)
    it("create graphite check", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      assert.is.equal(200, code)
    end)
    it("ingests graphite data via TCP plaintext listener", function()
      local conn, err = socket.connect("127.0.0.1", 2003)
      assert.message("Error " .. (err or "?") .. " opening raw socket graphite port").is_not_nil(conn)
      conn:settimeout(60, 't')
      assert.message("Error sending datapoints").is_equal(#payload, conn:send(payload))
      conn:close()
    end)
    it("has matching B1 records", function()
      local record = {}
      local rparts = {}
      -- split by \t, make sure we have the parts, and track #4 as the metric name
      for i=1,5 do
        record[i] = noit:waitfor(key, 2)
        assert.is_not_nil(record[i])
        rparts[i] = record[i]:split('\t')
        assert.is_not_nil(rparts[i][4])
        assert.is_not_nil(rparts[i][5])
      end
    end)
  end)
end)
