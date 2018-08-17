describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new()
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local uuid = mtev.uuid()
  local gseq = 0
  local check_xml = function()
    gseq = gseq + 2
    local id = "foo_" .. gseq
    return
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <seq>]=] .. tostring(gseq) .. [=[</seq>
    <target>]=] .. id .. [=[</target>
    <period>1000</period>
    <timeout>500</timeout>
    <name>thisisauniquename_]=] .. id .. [=[</name>
    <filterset>allowall</filterset>
    <module>http</module>
  </attributes>
  <config><url>https://127.0.0.1:43191/</url></config>
</check>]=]
  end


  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("check", function()
    it("is not present", function()
      local code = api:json("GET", "/checks/show/" .. uuid .. ".json")
      assert.is.equal(404, code)
    end)
    it("adds", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml())
      assert.is.equal(200, code)
    end)
    it("is present", function()
      local code, doc = api:json("GET", "/checks/show/" .. uuid .. ".json")
      assert.is.equal(200, code)
    end)
  end)

  describe("check", function()
    it("deletes", function()
      local code, doc = api:raw("DELETE", "/checks/delete/" .. uuid)
      assert.is.equal(200, code)
    end)
    it("is not present", function()
      local code, doc, data = api:json("GET", "/checks/show/" .. uuid .. ".json")
      assert.is_true(code == 404 or bit.band(doc.flags, tonumber("80", 16)) ~= 0)
    end)
  end)

  describe("check", function()
    it("tortures w/o delete", function()
      for i=1,100,1 do
        local code, doc, data = api:raw("PUT", "/checks/set/" .. uuid, check_xml())
        assert.is.equal(200, code)
      end
    end)
    it("tortures w delete", function()
      for i=1,100,1 do
        local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml())
        assert.is.equal(200, code)
        code, doc = api:raw("DELETE", "/checks/delete/" .. uuid)
        assert.is.equal(200, code)
      end
    end)
  end)
end)
