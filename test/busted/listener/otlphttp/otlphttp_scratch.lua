pending("otelproto #otelproto #listener", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("otlphttp", {
      modules = { otlphttp = { image = "otlphttp", config = { use_grpc_ssl = "false" } } }
    })
  end)
  teardown(function()
    if noit ~= nil then noit:stop() end
  end)

  local uuid = "c7343673-ab9b-4d1b-9775-713c2d1ec5af"
  local check_xml =
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <period>60000</period>
    <timeout>30000</timeout>
    <name>otlphttp</name>
    <filterset>allowall</filterset>
    <module>otlphttp</module>
  </attributes>
  <config>
    <secret>s3cr3tk3y</secret>
  </config>
</check>]=]
  local payload = [=[
]=]

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("otelproto", function()
    local key = noit:watchfor(mtev.pcre('Reformatted otelproto name'), true)

    it("create otelproto check", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      io.write("output:" .. doc)
      mtev.sleep(3000)
      assert.is.equal(200, code)
    end)

    pending("ingests otelproto data via ?", function()
      local conn = mtev.socket('inet','tcp')
      assert.message("Error creating mtev tcp socket").is_not_nil(conn)
      local rv, err = conn:connect('127.0.0.1', '4242')
      assert.message("Error " .. (err or "nil") .. " connecting to port").is_not_equal(-1, rv)
      local bytes, err = conn:send(payload)
      assert.message("Error " .. (err or "nil") .. " sending payload").is_not_equal(-1, bytes)
      assert.message("Incorrect number of bytes sent (" .. bytes .. "/" .. string.len(payload) .. ")").is_equal(string.len(payload), bytes)
      conn:close()
    end)

    it("has matching otelproto records", function()
      for i=1,10 do
        noit:waitfor(key, 2)
      end
    end)
  end)
end)
