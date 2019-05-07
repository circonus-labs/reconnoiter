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
    local key = noit:watchfor(mtev.pcre('Reformatted graphite name'), true)
    it("create graphite check", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      assert.is.equal(200, code)
    end)
    it("ingests graphite data via TCP plaintext listener", function()
      local conn = mtev.socket('inet','tcp')
      assert.message("Error creating mtev tcp socket").is_not_nil(conn)
      local rv, err = conn:connect('127.0.0.1', '2003')
      assert.message("Error " .. (err or "nil") .. " connecting to port").is_not_equal(-1, rv)
      local bytes, err = conn:send(payload)
      assert.message("Error " .. (err or "nil") .. " sending payload").is_not_equal(-1, bytes)
      assert.message("Incorrect number of bytes sent (" .. bytes .. "/" .. string.len(payload) .. ")").is_equal(string.len(payload), bytes)
      conn:close()
    end)
    it("has matching graphite records", function()
      for i=1,5 do
        noit:waitfor(key, 2)
      end
    end)
  end)
end)
