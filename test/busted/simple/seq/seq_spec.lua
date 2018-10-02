describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new()
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local uuid = mtev.uuid()
  local check_xml = function(seq)
    return
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <seq>]=] .. tostring(seq) .. [=[</seq>
    <period>5000</period>
    <timeout>1000</timeout>
    <name>selfcheck</name>
    <filterset>allowall</filterset>
    <module>selfcheck</module>
  </attributes>
  <config/>
</check>]=]
  end

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("selfcheck", function()
    local capa, start, elapsed
    it("is missing", function()
      local code, obj = api:raw("GET", "/checks/show/" .. uuid .. ".json")
      assert.is.equal(404, code)
    end)
    it("is sequenced", function()
      local code, obj, str = api:xml("PUT", "/checks/set/" .. uuid, check_xml(11))
      assert.is.equal(200, code)
      local code, obj = api:json("GET", "/checks/show/" .. uuid .. ".json")
      assert.is.equal(200, code)
      assert.is.equal(11, tonumber(obj.seq))
    end)
    it("cannot seq backwards", function()
      local code, obj, str = api:xml("PUT", "/checks/set/" .. uuid, check_xml(10))
      assert.is.equal(409, code)
    end)
  end)
end)
