describe("stratcon", function()
  local strat, api
  setup(function()
    Reconnoiter.clean_workspace()
    local opts = { generics = { test_ingestor = { image = 'test_ingestor' } } }
    strat = Reconnoiter.TestStratcon:new("strat", opts)
  end)
  teardown(function() if strat ~= nil then strat:stop() end end)

  it("starts", function()
    assert.is_true(strat:start():is_booted())
    api = strat:API()
  end)

  it("has no noits", function()
    local code, doc, raw = api:xml("GET", "/noits/show")
    assert.is.equal(200, code)
    assert.is_not_nil(doc)
    local noits = 0
    for result in doc:xpath("/noits/noit") do noits = noits +1 end
    assert.is.equal(0, noits)
  end)

  it("can add a noit", function()
    local code = api:raw("PUT", "/noits/set/127.0.0.1:1023")
    assert.is.equal(200, code)
  end)

  it("has noits", function()
    local code, doc, raw = api:xml("GET", "/noits/show")
    assert.is.equal(200, code)
    assert.is_not_nil(doc)
    local noits = 0
    for result in doc:xpath("/noits/noit") do noits = noits +1 end
    assert.is.equal(2, noits)
  end)

  it("can remove a noit", function()
    local code = api:raw("DELETE", "/noits/delete/127.0.0.1:1023")
    assert.is.equal(200, code)
  end)

  it("has no noits", function()
    local code, doc, raw = api:xml("GET", "/noits/show")
    assert.is.equal(200, code)
    assert.is_not_nil(doc)
    local noits = 0
    for result in doc:xpath("/noits/noit") do noits = noits +1 end
    assert.is.equal(0, noits)
  end)

end)

