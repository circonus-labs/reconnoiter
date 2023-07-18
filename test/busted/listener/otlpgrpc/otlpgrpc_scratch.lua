describe("otlpgrpc #otlpgrpc #listener", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("otlpgrpc", {
      modules = { otlpgrpc = { image = "otlpgrpc", config = { use_grpc_ssl = "false" } } }
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
    <name>otlpgrpc</name>
    <filterset>allowall</filterset>
    <module>otlpgrpc</module>
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

  describe("otlpgrpc", function()
    it("create otlpgrpc check", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      io.write("output:" .. doc)
      mtev.sleep(3000)
      assert.is.equal(200, code)
    end)
  end)
end)
