describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new()
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local uuid = mtev.uuid()
  local check_xml = function(seq)
    return [=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <period>1000</period>
    <timeout>500</timeout>
    <name>selfcheck</name>
    <filterset>specials</filterset>
    <module>selfcheck</module>
    <seq>]=] .. tostring(seq) .. [=[</seq>
  </attributes>
  <config xmlns:histogram="noit://module/histogram">
    <histogram:value name="feed_byte">add</histogram:value>
  </config>
</check>]=]
  end
  local filter_xml = function(seq)
    return [=[<?xml version="1.0" encoding="utf8"?>
    <filterset seq="]=] .. tostring(seq) .. [=["><rule type="allow"><metric>bytes</metric><metric>code</metric><metric>duration</metric></rule><rule type="deny"/><seq>1</seq></filterset>]=]
  end


  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("check", function()
    it("put fails", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml(10))
      assert.is.equal(500, code)
    end)
  end)

  describe("filterset", function()
    it("put", function()
      local code, doc = api:raw("PUT", "/filters/set/foo/specials", filter_xml(9))
      assert.is.equal(200, code)
    end)
    it("put seq", function()
      local code, doc = api:raw("PUT", "/filters/set/foo/specials", filter_xml(9))
      assert.is.equal(409, code)
    end)
  end)

  describe("check", function()
    it("put succeeds", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml(10))
      assert.is.equal(200, code)
    end)
    it("is reporting", function()
      local code, doc = api:json("GET", "/checks/show/" .. uuid .. ".json")
      assert.is.equal(200, code)
    end)
    it("deletes", function()
      local code, doc = api:raw("DELETE", "/checks/delete/" .. uuid)
      assert.is.equal(200, code)
    end)
    it("is gone", function()
      local code, doc, raw = api:json("GET", "/checks/show/" .. uuid .. ".json")
      if code == 200 then
        assert.is.not_equal(0, bit.band(tonumber(doc.flags), 0x80))
      else
        assert.is.equal(404, code)
      end
    end)
    it("is really gone", function()
      local code
      local tries = 0
      repeat
        mtev.sleep(0.1)
        code = api:json("GET", "/checks/show/" .. uuid .. ".json")
        tries = tries + 1
      until code == 404 or tries > 50
      assert.is.equal(404, code)
    end)
  end)

end)
