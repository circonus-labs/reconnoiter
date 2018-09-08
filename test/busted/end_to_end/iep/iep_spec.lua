local test = describe
if not utils.postgres_reqs() then test = pending end

test("end to end #db #iep", function()
  local db, strat, noit, stratapi, noitapi, firehose, alerts
  local firehose_messages, alert_messages = {}, {}
  
  local uuid_re = mtev.pcre('^[0-9a-fA-F]{4}(?:[0-9a-fA-F]{4}-){4}[0-9a-fA-F]{12}$')
  local selfcheck_uuid = '9c2163aa-f4bd-11df-851b-979bd290a553'
  local selfcheck_sid = -1
  local selfcheck_xml = [=[<?xml version="1.0" encoding="utf8"?>
                           <check><attributes><target>127.0.0.1</target>
                           <period>500</period>
                           <timeout>400</timeout>
                           <name>selfcheck</name>
                           <filterset>allowall</filterset>
                           <module>selfcheck</module>
                           </attributes><config/></check>]=]

  local function mq_siphon(mq, tgt, func)
    mq:own()
    if func == nil then func = function(a) return a end end
    repeat
      local payload = mq:receive()
      local data = func(payload)
      if data ~= nil then table.insert(tgt, data) end
    until data == nil
  end

  setup(function()
    Reconnoiter.clean_workspace()
    db = Reconnoiter.TestPostgres:new()
  end)
  teardown(function()
    if strat then strat:stop() end
    if noit then noit:stop() end
    db:shutdown()
  end)
  it("starts postgres", function()
    assert.is_nil(db:setup())
  end)


  it("has data", function()
    assert.is_not_nil(db:client())
    local res = db:client():query('select count(*) as rollups from noit.metric_numeric_rollup_config')
    assert.same({ { rollups = 6 } }, res)
  end)

  describe("noit", function()
    it("gets started", function()
      noit = Reconnoiter.TestNoit:new()
      assert.is_true(noit:start():is_booted())
      noitapi = noit:API()
    end)
  end)

  describe("stratcon", function()
    local stratconfig = { noits = { { address = '127.0.0.1', port = noit:api_port() } } }
    it("gets configured", function() strat = Reconnoiter.TestStratcon:new("strat", stratconfig) end)
    it("boots to iep", function()
      -- We change the conditions for start as we have a unlikely race condition
      -- we cant wait for that before we start (proc is nil), so we risk it
      -- starting and printing before we register our watchfor.
      assert.is_true(strat:start({timeout = 60, boot_match = 'Loaded all (%d+) check state'}):is_booted()) 
      stratapi = strat:API()
      assert.is_not_nil(stratapi)
    end)
    it("exposes firehose", function()
      firehose = Stomp:new()
      firehose:set_timeout(60)
      firehose:connect('127.0.0.1', strat:stomp_port())
      firehose:subscribe( { destination = "/queue/noit.firehose", ack = "auto" } )
      mtev.coroutine_spawn(mq_siphon, firehose, firehose_messages)
    end)
    it("exposes alerts", function()
      alerts = Stomp:new()
      alerts:set_timeout(60)
      alerts:connect('127.0.0.1', strat:stomp_port())
      alerts:subscribe( { destination = "/topic/noit.alerts.numeric", ack = "auto" } )
      mtev.coroutine_spawn(mq_siphon, alerts, alert_messages, mtev.parsejson)
    end)
  end)

  describe("noit", function()
    it("adds selfcheck", function()
      local key = noit:watchfor(mtev.pcre("selfcheck <-"))
      local code, doc, raw = noitapi:xml("PUT", "/checks/set/" .. selfcheck_uuid, selfcheck_xml)
      assert.is_equal(200, code)
      assert.is_not_nil(noit:waitfor(key,5))
    end)
  end)

  describe("stratcon", function()
    it("loads some data", function()
      local batch_key = strat:watchfor(mtev.pcre("Finished batch.*/noit-test to"))
      assert.is_not_nil(strat:waitfor(batch_key, 5))
    end)
  end)

  describe("postres", function()
    local pg = db:client()
    it("has uuid->sid mapping", function()
      local res = pg:query('select sid, id from stratcon.map_uuid_to_sid where id = ' .. pg:escape_literal(selfcheck_uuid))
      assert.is_not_nil(res[1])
      assert.is_not_nil(res[1].sid)
      assert.is.not_equal(-1, res[1].sid)
      selfcheck_sid = res[1].sid
    end)

    it("has metric data", function()
      local res
      local attempts = 0
      repeat
        attempts = attempts + 1
        res = pg:query('select (select count(*) from noit.metric_text_archive where sid = ' .. selfcheck_sid ..') as text_count, ' ..
                           '       (select count(*) from noit.metric_numeric_archive where sid = ' .. selfcheck_sid .. ') as numeric_count')
        if attempts > 1 then mtev.sleep(0.5) end
      until res[1].text_count + res[1].numeric_count > 0 or attempts > 5
      assert.is.not_equal(0, res[1].text_count + res[1].numeric_count) 
    end)
  end)

  describe("stratcon", function()
    local iep_count, storage_count = 0, 0
    it("sees noit", function()
      local code, doc, xml = stratapi:xml("GET", "/noits/show")
      assert.is_equal(200, code)
      assert.is_not_nil(doc)
      local feeds = 0
      for i, name in ipairs({"transient/iep", "durable/storage"}) do
        for result in doc:xpath('//noits/noit[@type="' .. name .. '"]') do
          assert.is_not_nil(result)
          assert.is.not_equal(0, tonumber(result:attr("session_events")))
          feeds = feeds + 1
        end
      end
      assert.is_equal(2, feeds)
    end)

    it("send IEP data", function()
      assert.is_not_nil(strat:waitfor(strat:watchfor(mtev.pcre("STOMP send succeeded")), 2))
    end)
    it("sees firehose data", function()
      local start = mtev.timeval.now()
      repeat mtev.sleep(0.1)
      until #firehose_messages ~= 0 or mtev.timeval.seconds(mtev.timeval.now() - start) > 5
      assert.is_not_equal(0, #firehose_messages)
    end)
    it("sees alert data", function()
      local start = mtev.timeval.now()
      repeat mtev.sleep(1)
      until #alert_messages ~= 0 or mtev.timeval.seconds(mtev.timeval.now() - start) > 10 
      assert.is_not_equal(0, #alert_messages)
      local taglist = alert_messages[1]:document().tags or {}
      local tags = {}
      for i, tag in ipairs(taglist) do
        local s,f = string.find(tag, ':')
        if s ~= nil then
          tags[tag:sub(1,s-1)] = tag:sub(s+1,-1)
        else
          tags[tag] = 'true'
        end
      end
      assert.is.equal(tags.reconnoiter, 'true')
      assert.is_not_nil(uuid_re(tags.check))
      assert.is.equal(tags.module, 'selfcheck')
      assert.is.equal(tags['type'], 'numeric')
    end)
  end)

end)
