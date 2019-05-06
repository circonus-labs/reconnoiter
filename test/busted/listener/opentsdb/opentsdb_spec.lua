local socket = require("socket")
describe("opentsdb #opentsdb #listener", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("opentsdb", {
      modules = { opentsdb = { image = "opentsdb", config = { asynch_metrics = "true" } } }
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
    <name>opentsdb</name>
    <filterset>allowall</filterset>
    <module>opentsdb</module>
  </attributes>
  <config>
    <listen_port>4242</listen_port>
  </config>
</check>]=]
  local payload = [=[
put opentsdb.foo.1 1557109218.043 12345.6 app=flarg parity=even
put opentsdb.foo.2 1557109219.043 12345.6 app=flarg parity=odd
put opentsdb.foo.3 1557109220.043 12345.6 app=flarg parity=even
put opentsdb.foo.4 1557109221.043 12345.6 app=flarg parity=odd
put opentsdb.foo.5 1557109222.043 12345.6 app=flarg parity=even
put opentsdb.foo.6 1557109223.043 12345.6 app=flarg parity=even
put opentsdb.foo.7 1557109224.043 12345.6 app=flarg parity=odd
put opentsdb.foo.8 1557109225.043 12345.6 app=flarg parity=even
put opentsdb.foo.9 1557109226.043 12345.6 app=flarg parity=odd
put opentsdb.foo.10 1557109227.043 12345.6 app=flarg parity=even
]=]

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("opentsdb", function()
    local key = noit:watchfor(mtev.pcre('B1\t'), true)
    it("create opentsdb check", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      assert.is.equal(200, code)
    end)
    it("ingests opentsdb data via TCP plaintext listener", function()
      local conn, err = socket.connect("127.0.0.1", 4242)
      assert.message("Error " .. (err or "?") .. " opening raw socket opentsdb port").is_not_nil(conn)
      conn:settimeout(60, 't')
      assert.message("Error sending datapoints").is_equal(#payload, conn:send(payload))
      conn:close()
    end)
    it("has matching B1 records", function()
      local record = {}
      local rparts = {}
      -- split by \t, make sure we have the parts, and track #4 as the metric name
      for i=1,10 do
        record[i] = noit:waitfor(key, 2)
        assert.is_not_nil(record[i])
        rparts[i] = record[i]:split('\t')
        assert.is_not_nil(rparts[i][4])
        assert.is_not_nil(rparts[i][5])
      end
    end)
  end)
end)
