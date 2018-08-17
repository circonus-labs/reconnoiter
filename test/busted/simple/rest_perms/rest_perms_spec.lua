describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("noit", { rest_acls = { { type = "deny", rules = { { url = "^/checks/show/", type = "allow" } } } } })
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("api", function()
    it("can get check", function()
      local code = api:raw("GET", "/checks/show/f7cea020-f19d-11dd-85a6-cb6d3a2207dc.json")
      assert.is.equal(404, code)
    end)

    it("cannot set check", function()
      local code = api:raw("PUT", "/checks/set/f7cea020-f19d-11dd-85a6-cb6d3a2207dc",
        [=[<?xml version="1.0" encoding="utf8"?><check><attributes><target>127.0.0.1</target>
           <period>5000</period><timeout>1000</timeout><name>selfcheck</name>
           <filterset>allowall</filterset><module>selfcheck</module></attributes><config/></check>]=])
      assert.is.equal(403, code)
    end)
  end)
end)
