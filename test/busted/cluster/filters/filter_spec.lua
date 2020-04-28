describe("cluster", function()
  local noit1, noit2, api1, api2
  local cluster = {}
  local global_seq = 1239834
  -- Static uuids here to make tests (hashing) repeatable
  local uuid1 = '5a74f6f2-3125-44de-84e7-6ea7275e5fee'
  local uuid2 = 'ab419eda-6f51-466f-b086-63ae940c8147'
  local check_uuid = '39568546-da44-465e-abd8-a348a27b2fd5'
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

  function make_check(api, name, module, uuid)
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
  <config/>
</check>]=])
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
      local code, o = make_check(api1, "selfcheck", "selfcheck", uuid1, 1)
      assert.is.equal(200, code)
    end)
    it("on node2", function()
      local code, o = make_check(api2, "selfcheck", "selfcheck", uuid2, 1)
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
  end)
end)
