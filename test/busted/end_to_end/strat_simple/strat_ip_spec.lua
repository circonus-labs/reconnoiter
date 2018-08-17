local test = describe
if not utils.postgres_reqs() then test = pending end

test("end to end", function()
  local db
  setup(function()
    Reconnoiter.clean_workspace()
    db = Reconnoiter.TestPostgres:new()
  end)
  teardown(function() db:shutdown() end)
  it("starts postgres", function()
    assert.is_nil(db:setup())
  end)
  it("has data", function()
    assert.is_not_nil(db:client())
    local res = db:client():query('select count(*) as rollups from noit.metric_numeric_rollup_config')
    assert.same({ { rollups = 6 } }, res)
  end)

  describe("stratcon", function()
    local strat
    setup(function() strat = Reconnoiter.TestStratcon:new() end)
    teardown(function() if strat ~= nil then strat:stop() end end)
  
    it("starts", function()
      assert.is_true(strat:start({boot_match = 'Finished batch'}):is_booted())
    end)
  
    it("is in DB", function()
      local res = db:client():query("select remote_address from stratcon.current_node_config")
      assert.is_not_nil(0, res[1])
      assert.is.not_equal("0.0.0.0", res[1].remote_address)
      assert.truthy(mtev.pcre("^\\d{1,3}(?:\\.\\d{1,3}){3}$")(res[1].remote_address))
    end)
  end)
end)
