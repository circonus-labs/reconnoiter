describe("cluster", function()
  local noit1, noit2, api1, api2
  local cluster = {}
  local global_seq = 1239834
  local global_filter_seq = 1234
  -- Static uuids here to make tests (hashing) repeatable
  local uuid1 = '5a74f6f2-3125-44de-84e7-6ea7275e5fee'
  local uuid2 = 'ab419eda-6f51-466f-b086-63ae940c8147'
  local check_uuid = '39568546-da44-465e-abd8-a348a27b2fd5'
  local basic_cull_true_filterset
  local basic_cull_false_filterset
  setup(function()
    Reconnoiter.clean_workspace()
    noit1 = Reconnoiter.TestNoit:new("node1")
    noit2 = Reconnoiter.TestNoit:new("node2")
    cluster = { { id = uuid1, port = noit1:api_port() }, { id = uuid2, port = noit2:api_port() } }
  end)
  teardown(function()
    if noit1 ~= nil then noit1:stop() end
    if noit2 ~= nil then noit2:stop() end
  end)

  function make_check(api, name, module, uuid, filterset)
    global_seq = global_seq + 1
    filterset = filterset or "allowall"
    return api:xml("PUT", "/checks/set/" .. uuid,
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <seq>]=] .. tostring(global_seq) .. [=[</seq>
    <period>1000</period>
    <timeout>800</timeout>
    <name>]=] .. name .. [=[</name>
    <filterset>]=] .. filterset .. [=[</filterset>
    <module>]=] .. module .. [=[</module>
  </attributes>
  <config/>
</check>]=])
  end
  function make_filter_xml(name, cull, m)
    m = m or "dummy"
    cull = cull or "true"
    global_filter_seq = global_filter_seq + 1
    local xml = [=[<?xml version="1.0" encoding="utf8"?>
<filterset name="]=] .. name .. [=[" cull="]=] .. cull .. [=[" seq="]=] .. global_filter_seq .. [=[">
]=]
    if type(m) == "string" then
      xml = xml .. [=[
  <rule type="allow" metric="]=] .. m .. [=["/>
  <rule type="deny"/>
</filterset>]=]
    else
      xml = xml .. [=[
  <rule type="allow">]=]
      for k,v in pairs(m) do 
        xml = xml .. '<metric>' .. k .. '</metric>'
      end
      xml = xml .. [=[
  </rule>
  <rule type="deny"  />
</filterset>]=]
    end
    return xml
  end
  function put_cluster(api, nodes, idx)
    local payload = '<?xml version="1.0" encoding="utf8"?><cluster name="noit" port="' .. tostring(nodes[idx].port) .. '" period="200" timeout="1000" maturity="2000" key="poison" seq="1">'
    for i, node in ipairs(nodes) do
      payload = payload .. '<node id="' .. node.id .. '" cn="noit-test" address="127.0.0.1" port="' .. tostring(node.port) .. '"/>'
    end
    payload = payload .. '</cluster>'
    return api:xml("POST", "/cluster", payload)
  end

  it("node1 should start", function()
    assert.is_true(noit1:start():is_booted())
    api1 = noit1:API()
  end)
  it("node2 should start", function()
    assert.is_true(noit2:start():is_booted())
    api2 = noit2:API()
  end)

  describe("selfchecks", function()
    it("on node1", function()
      local code = make_check(api1, "selfcheck", "selfcheck", uuid1, "allowall")
      assert.is.equal(200, code)
    end)
    it("on node2", function()
      local code = make_check(api2, "selfcheck", "selfcheck", uuid2, "allowall")
      assert.is.equal(200, code)
    end)
  end)

  describe("topo", function()
    it("is installed", function()
      local code
      code = put_cluster(api1, cluster, 1)
      assert.is.equal(204, code)
      code = put_cluster(api2, cluster, 2)
      assert.is.equal(204, code)
    end)
  end)

  describe("filter", function()
    basic_cull_true_filterset = make_filter_xml("basic_cull_true", "true")
    basic_cull_false_filterset = make_filter_xml("basic_cull_false", "false")

    it("is missing on node1", function()
      local code  = api1:xml("GET", "/filters/show/basic_cull_true")
      assert.is.equal(404, code)
      code  = api1:xml("GET", "/filters/show/basic_cull_false")
      assert.is.equal(404, code)
    end)
    it("is missing on node2", function()
      local code  = api2:xml("GET", "/filters/show/basic_cull_true")
      assert.is.equal(404, code)
      code  = api2:xml("GET", "/filters/show/basic_cull_false")
      assert.is.equal(404, code)
    end)
    it("puts one on node1", function()
      local code  = api1:raw("PUT", "/filters/set/basic_cull_true", basic_cull_true_filterset)
      assert.is_equal(200, code)
    end)
    it("puts other one on node2", function()
      local code = api2:raw("PUT", "/filters/set/basic_cull_false", basic_cull_false_filterset)
      assert.is_equal(200, code)
    end)
    it("has replicated filters across the cluster", function()
      mtev.sleep(5)
      local code = api1:xml("GET", "/filters/show/basic_cull_true")
      assert.is_equal(200, code)
      code = api2:xml("GET", "/filters/show/basic_cull_true")
      assert.is_equal(200, code)
      code = api1:xml("GET", "/filters/show/basic_cull_false")
      assert.is_equal(200, code)
      code = api2:xml("GET", "/filters/show/basic_cull_false")
      assert.is_equal(200, code)
    end)
    it("culls from nodes", function()
      local code = api1:raw("POST", "/filters/cull")
      assert.is_equal(200, code)
      code = api2:raw("POST", "/filters/cull")
      assert.is_equal(200, code)
    end)
    it("culled where cull is true", function()
      local code = api1:xml("GET", "/filters/show/basic_cull_true")
      assert.is_equal(404, code)
      code = api2:xml("GET", "/filters/show/basic_cull_true")
      assert.is_equal(404, code)
    end)
    it("didn't cull where cull is false", function()
      local code = api1:xml("GET", "/filters/show/basic_cull_false")
      assert.is_equal(200, code)
      code = api2:xml("GET", "/filters/show/basic_cull_false")
      assert.is_equal(200, code)
    end)
    it("deletes a filter from nodes", function()
      local code = api1:raw("DELETE", "/filters/delete/basic_cull_false")
      assert.is_equal(200, code)
      code = api2:raw("DELETE", "/filters/delete/basic_cull_false")
      assert.is_equal(200, code)
    end)
    it("is missing from nodes", function()
      local code = api1:raw("GET", "/filters/show/basic_cull_false")
      assert.is_equal(404, code)
      code = api2:raw("GET", "/filters/show/basic_cull_false")
      assert.is_equal(404, code)
    end)
  end)
end)
