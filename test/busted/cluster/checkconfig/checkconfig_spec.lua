describe("cluster", function()
  local noit1, noit2, api1, api2
  local cluster = {}
  local global_seq = 1239834
  -- Static uuids here to make tests (hashing) repeatable
  local uuid1 = '5a74f6f2-3125-44de-84e7-6ea7275e5fee'
  local uuid2 = 'ab419eda-6f51-466f-b086-63ae940c8147'
  local check_uuid = '39568546-da44-465e-abd8-a348a27b2fd5'
  setup(function()
    local modules = { http = { loader = "lua", object = "noit.module.http" }, selfcheck = { image = "selfcheck" } }
    Reconnoiter.clean_workspace()
    noit1 = Reconnoiter.TestNoit:new("node1", { modules = modules })
    noit2 = Reconnoiter.TestNoit:new("node2", { modules = modules })
    cluster = { { id = uuid1, port = noit1:api_port() }, { id = uuid2, port = noit2:api_port() } }
  end)
  teardown(function()
    if noit1 ~= nil then noit1:stop() end
    if noit2 ~= nil then noit2:stop() end
  end)

  function make_check(api, name, module, uuid, config)
    config = config or '<secret>none</secret>'
    global_seq = global_seq + 1
    return api:xml("PUT", "/checks/set/" .. uuid,
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <seq>]=] .. tostring(global_seq) .. [=[</seq>
    <period>1000</period>
    <timeout>800</timeout>
    <name>]=] .. name .. [=[</name>
    <filterset>allowall</filterset>
    <module>]=] .. module .. [=[</module>
  </attributes>
  <config>]=] .. config .. [=[</config>
</check>]=])
  end
  function put_cluster(api, nodes, idx)
    local payload = '<?xml version="1.0" encoding="utf8"?><cluster name="noit" port="' .. tostring(nodes[idx].port) .. '" period="200" timeout="1000" maturity="2000" key="poison" seq="1">'
    for i, node in ipairs(nodes) do
      payload = payload .. '<node id="' .. node.id .. '" cn="noit-test" address="127.0.0.1" port="' .. tostring(node.port) .. '"/>'
    end
    payload = payload .. '</cluster>'
    return api:xmlforcewrite("POST", "/cluster", payload)
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
      local code, o = make_check(api1, "selfcheck", "selfcheck", uuid1)
      assert.is.equal(200, code)
    end)
    it("on node2", function()
      local code, o = make_check(api2, "selfcheck", "selfcheck", uuid2)
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

  describe("check", function()
    it("is absent on node1", function()
      local code = api1:json("GET", "/checks/show/" .. check_uuid .. ".json")
      assert.is.equal(404, code)
    end)
    it("is absent on node2", function()
      local code = api2:json("GET", "/checks/show/" .. check_uuid .. ".json")
      assert.is.equal(404, code)
    end)
    it("is put on cluster", function()
      local key = noit2:watchfor(mtev.pcre("plague"))
      local code, obj = make_check(api1, "plague", "http", check_uuid, '<url>http://foo/&amp;foo</url>')
      assert.is.equal(200, code)
      assert.is_not_nil(noit2:waitfor(key, 5))
    end)

    local config
    it("is active on node1", function()
      local code, obj = api1:json("GET", "/checks/show/" .. check_uuid .. ".json")
      assert.is.equal(200, code)
      assert.is.equal(true, obj.active_on_cluster_node)
      config = obj.config
    end)
    it("is inactive on node2", function()
      local code, obj = api2:json("GET", "/checks/show/" .. check_uuid .. ".json")
      assert.is.equal(302, code)
      assert.is.equal(false, obj.active_on_cluster_node)
      assert.is.same(config, obj.config)
    end)
    it("is owned by one node", function()
      local codes = {}
      local expected_codes = {}
      expected_codes["204"] = 1
      expected_codes["302"] = 1
      local code, obj = api1:json("GET", "/checks/owner/" .. check_uuid)
      codes["" .. code] = 1
      local code, obj = api2:json("GET", "/checks/owner/" .. check_uuid)
      codes["" .. code] = 1
      assert.is.same(expected_codes, codes)
    end)

  end)
end)
