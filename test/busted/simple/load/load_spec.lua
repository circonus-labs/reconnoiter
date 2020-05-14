describe("load", function()
  -- The uuids?names for these checks/filtersets are defined in lua-support/reconnoiter.lua
  -- That file defines some good and some bad checks/filtersets - we need to verify here
  -- that the ones we expect to be good are good and that the ones we expect to
  -- be bad are bad
  -- When using an XML backingstore, we'll just accept whatever. We validate with
  -- LMDB, so we need to check those cases separately
  local noit, api
  local use_lmdb_checks = os.getenv('NOIT_LMDB_CHECKS') or "0"
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new()
  end)
  teardown(function()
    if noit ~= nil then
      noit:stop()
    end
  end)
  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)
  it("should fail to find bad checks from load", function()
    local expected_value = 200
    if use_lmdb_checks == "1" then
      expected_value = 404
    end
    local code = api:json("GET", "/checks/show/12345678-1234-1234-1234-123456789010.json")
    assert.is_equal(expected_value, code)
    code = api:json("GET", "/checks/show/12345678-1234-1234-1234-123456789011.json")
    assert.is_equal(expected_value, code)
    code = api:json("GET", "/checks/show/12345678-1234-1234-1234-123456789012.json")
    assert.is_equal(expected_value, code)
    code = api:json("GET", "/checks/show/12345678-1234-1234-1234-123456789013.json")
    assert.is_equal(expected_value, code)
  end)
  it("should find good check from load", function()
    local code = api:json("GET", "/checks/show/12345678-1234-1234-1234-123456789014.json")
    assert.is_equal(200, code)
  end)
  it("should find good filterset from load", function()
    local code = api:xml("GET", "/filters/show/some_test_filterset")
    assert.is_equal(200, code)
  end)
end)
