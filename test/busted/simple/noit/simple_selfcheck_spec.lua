describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new()
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
    <name>selfcheck</name>
    <filterset>allowall</filterset>
    <module>selfcheck</module>
  </attributes>
  <config xmlns:histogram="noit://module/histogram">
    <histogram:value name="feed_byte">add</histogram:value>
  </config>
</check>]=]


  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("has capabilities", function()
    local capa, start, elapsed
    it("fetch /capa.json", function()
      start = mtev.timeval.now()
      local code, obj = api:json("GET", "/capa.json")
      elapsed = mtev.timeval.seconds(mtev.timeval.now() - start)
      assert.is_not_nil(obj)
      capa = obj
    end)
    it("version", function()
      -- check version
      assert.is_true(mtev.pcre('[a-f0-9]{40}')(capa.features.noit.version))
    end)
    it("time", function()
      -- check time
      assert.is_true(mtev.pcre('^\\d+$')(capa.current_time))
      local error = tonumber(capa.current_time)/1000 - elapsed/2.0 - mtev.timeval.seconds(start)
      assert.is_true(math.abs(error) < elapsed)
    end)
    it("services", function()
      local expected_comms = {}
      expected_comms['0x50555420'] = { name = 'mtev_wire_rest_api', version = '1.0' }
      expected_comms['0x47455420'] = { name = 'mtev_wire_rest_api', version = '1.0' }
      expected_comms['0x48454144'] = { name = 'mtev_wire_rest_api', version = '1.0' }
      expected_comms['0xda7afeed'] = { name = 'log_transit', version = '1.0' }
      expected_comms['0x44454c45'] = { name = 'mtev_wire_rest_api', version = '1.0' }
      expected_comms['0x7e66feed'] = { name = 'log_transit', version = '1.0' }
      expected_comms['0x52455645'] = { name = 'reverse_socket_accept' }
      expected_comms['0x504f5354'] = { name = 'mtev_wire_rest_api', version = '1.0' }
      expected_comms['0xfa57feed'] = { name = 'livestream_transit', version = '1.0' }
      expected_comms['0x43415041'] = { name = 'capabilities_transit', version = '1.0' }
      expected_comms['0x4d455247'] = { name = 'mtev_wire_rest_api', version = '1.0' }
  
      local has_control = false
      for cmd, svc in pairs(capa.services) do
        if svc.control_dispatch == 'control_dispatch' then
          assert.same(expected_comms, svc.commands)
          has_control = true
        end
      end
      assert.is_true(has_control)
    end)
  end)

  describe("selfcheck", function()
    local key = noit:watchfor(mtev.pcre('selfcheck <-'))
    it("is not present", function()
      local code = api:json("GET", "/checks/show/" .. uuid .. ".json")
      assert.is.equal(404, code)
    end)
    it("put", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      assert.is.equal(200, code)
    end)
    it("has run", function()
      noit:waitfor(key, 5)
    end)
    it("is reporting", function()
      local code, doc = api:json("GET", "/checks/show/" .. uuid .. ".json")
      assert.is.equal(200, code)
      assert.is.equal(true, doc.status.good)
    end)
  end)
end)
