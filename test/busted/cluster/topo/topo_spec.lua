describe("cluster", function()
  local noit1, noit2, api1, api2
  local cluster = {}
  -- Static uuids here to make tests (hashing) repeatable
  local uuid1 = '5a74f6f2-3125-44de-84e7-6ea7275e5fee'
  local uuid2 = 'ab419eda-6f51-466f-b086-63ae940c8147'

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
  function put_cluster(api, nodes, idx, seq, batch_size)
    local payload
    if batch_size <= 0 then
      payload = '<?xml version="1.0" encoding="utf8"?><cluster name="noit" port="' .. tostring(nodes[idx].port) .. '" period="200" timeout="1000" maturity="2000" key="poison" seq="' ..
      tostring(seq) .. '">'
    else
      payload = '<?xml version="1.0" encoding="utf8"?><cluster name="noit" port="' .. tostring(nodes[idx].port) .. '" period="200" timeout="1000" maturity="2000" key="poison" seq="' ..
        tostring(seq) .. '" batch_size="' .. tostring(batch_size) .. '">'
    end
    for i, node in ipairs(nodes) do
      payload = payload .. '<node id="' .. node.id .. '" cn="noit-test" address="127.0.0.1" port="' .. tostring(node.port) .. '"/>'
    end
    payload = payload .. '</cluster>'
    return api:xmlforcewrite("POST", "/cluster", payload)
  end
  function validate_cluster_xml(code, data, batch_size)
    assert.is_equal(200, code)
  end
  function validate_cluster_json(code, data, batch_size)
    assert.is_equal(200, code)
  end

  it("node1 should start", function()
    assert.is_true(noit1:start():is_booted())
    api1 = noit1:API()
  end)
  it("node2 should start", function()
    assert.is_true(noit2:start():is_booted())
    api2 = noit2:API()
  end)

  describe("topo", function()
    it("installs a basic cluster topology", function()
      local code = put_cluster(api1, cluster, 1, 1, -1)
      assert.is.equal(204, code)
      code = put_cluster(api2, cluster, 2, 1, -1)
      assert.is.equal(204, code)
    end)
    it("validates the cluster topology as xml", function()
      local code, xml, raw = api1:xml("GET", "/cluster/noit")
      validate_cluster_xml(code, xml, -1)
      code, xml, raw = api2:xml("GET", "/cluster/noit")
      validate_cluster_xml(code, xml, -1)
    end)
    it("validates the cluster topology as json", function()
      local code, obj, raw = api1:json("GET", "/cluster/noit.json")
      validate_cluster_json(code, obj, -1)
      code, obj, raw = api2:json("GET", "/cluster/noit.json")
      validate_cluster_json(code, obj, -1)
    end)
    it("updates the cluster topology with batch size", function()
      local code = put_cluster(api1, cluster, 1, 2, 5000)
      assert.is.equal(204, code)
      code = put_cluster(api2, cluster, 2, 2, 5000)
      assert.is.equal(204, code)
    end)
    it("valdates the new cluster topology as xml", function()
      local code, xml, raw = api1:xml("GET", "/cluster/noit")
      validate_cluster_xml(code, xml, 5000)
      code, xml, raw = api2:xml("GET", "/cluster/noit")
      validate_cluster_xml(code, xml, 5000)
    end)
    it("validates the cluster topology as json", function()
      local code, obj, raw = api1:json("GET", "/cluster/noit.json")
      validate_cluster_json(code, obj, 5000)
      code, obj, raw = api2:json("GET", "/cluster/noit.json")
      validate_cluster_json(code, obj, 5000)
    end)
  end)
end)
