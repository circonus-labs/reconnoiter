describe("noit", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new("noit",
      { noit_ssl_on = "on",
        rest_acls = { { type = "deny", rules = { { type = "allow", url = "^/livestream/" },
                                                 { type = "allow", url = "^/checks/set/" } } } }
      })
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

  describe("selfcheck", function()
    local key = noit:watchfor(mtev.pcre('selfcheck <-'))
    it("put", function()
      local code, doc = api:raw("PUT", "/checks/set/" .. uuid, check_xml)
      assert.is.equal(200, code)
      assert.is_not_nil(noit:waitfor(key,5))
    end)
  end)

  describe("websocket", function()
    local client
    local key = mtev.uuid()
    function cb_ready(wsc)
      client = wsc
      assert.is_true(wsc:send(wsc.BINARY, _J({ period_ms = 500, check_uuid = uuid, metrics = { "check_cnt", "checks_run" } })))
      return true
    end
    function cb_message(wsc, opcode, buf)
      mtev.notify(key, mtev.parsejson(buf):document())
      return true
    end
    it("connects", function()
      mtev.websocket_client_connect("127.0.0.1", noit:api_port(), "/livestream/", "noit_livestream",
                                    { ready = cb_ready,
                                      message = cb_message },
                                    { ca_chain = Reconnoiter.ssl_file("test-ca.crt") })
    end)
    it("gets data", function()
      local expect, rkey, metric = {}
      rkey, metric = mtev.waitfor(key, 5)
      if metric == nil then
        -- mtev.notify bug, doesn't wake up across C/lua resume boundary
        rkey, metric = mtev.waitfor(key, 5)
      end
      assert.is_not_nil(metric)
      assert.is.equal('M', metric.type)
      expect.check_cnt = expect.check_cnt or metric.check_cnt
      expect.checks_run = expect.checks_run or metric.checks_run

      rkey, metric = mtev.waitfor(key, 5)
      assert.is_not_nil(metric)
      assert.is.equal('M', metric.type)
      expect.check_cnt = expect.check_cnt or metric.check_cnt
      expect.checks_run = expect.checks_run or metric.checks_run

      assert.is_not_nil(expect.check_cnt)
      assert.is_not_nil(expect.checks_run)
      if wsc ~= nil then wsc:close() end
    end)
  end)
end)
