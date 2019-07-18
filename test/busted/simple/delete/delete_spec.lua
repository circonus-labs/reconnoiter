describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new()
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local check_cnt = 50
  local uuid = {}
  for i = 1,check_cnt do uuid[i] = mtev.uuid() end
  local gseq = 0
  local check_xml = function(i)
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
    <name>thisisauniquename_]=] .. tostring(i) .. [=[</name>
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
      for i = 1,check_cnt do
        local code = api:json("GET", "/checks/show/" .. uuid[i] .. ".json")
        assert.is.equal(404, code)
      end
    end)
    it("adds", function()
      for i = 1,check_cnt do
        local code, doc = api:raw("PUT", "/checks/set/" .. uuid[i], check_xml(i))
        assert.is.equal(200, code)
      end
    end)
    it("is present", function()
      for i = 1,check_cnt do
        local code, doc = api:json("GET", "/checks/show/" .. uuid[i] .. ".json")
        assert.is.equal(200, code)
      end
    end)
  end)

  describe("check", function()
    it("tortures w/o delete", function()
      for i=1,100,1 do
        local code, doc, data = api:raw("PUT", "/checks/set/" .. uuid[8], check_xml(8))
        assert.is.equal(200, code)
      end
    end)
    pending("tortures w delete", function()
      for i=1,100,1 do
        local code, doc = api:raw("PUT", "/checks/set/" .. uuid[8], check_xml(8))
        assert.is.equal(200, code)
        code, doc = api:raw("DELETE", "/checks/delete/" .. uuid[8])
        assert.is.equal(200, code)
      end
    end)
  end)
end)
