describe("cluster", function()
  local noit1, noit2, api1, api2
  local cluster = {}
  -- Static uuids here to make tests (hashing) repeatable
  local uuid1 = '5a74f6f2-3125-44de-84e7-6ea7275e5fee'
  local uuid2 = 'ab419eda-6f51-466f-b086-63ae940c8147'
  local seq = 0

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
  function put_cluster(api, idx, set_batch_size, batch_size)
    local payload
    if set_batch_size == false then
      payload = '<?xml version="1.0" encoding="utf8"?><cluster name="noit" port="' .. tostring(cluster[idx].port) .. '" period="200" timeout="1000" maturity="2000" key="poison" seq="' ..
      tostring(seq) .. '">'
    else
      payload = '<?xml version="1.0" encoding="utf8"?><cluster name="noit" port="' .. tostring(cluster[idx].port) .. '" period="200" timeout="1000" maturity="2000" key="poison" seq="' ..
        tostring(seq) .. '" batch_size="' .. tostring(batch_size) .. '">'
    end
    for i, node in ipairs(cluster) do
      payload = payload .. '<node id="' .. node.id .. '" cn="noit-test" address="127.0.0.1" port="' .. tostring(node.port) .. '"/>'
    end
    payload = payload .. '</cluster>'
    return api:xmlforcewrite("POST", "/cluster", payload)
  end
  function validate_cluster_xml(idx, code, data, contains_batch_size, batch_size)
    assert.is_equal(200, code)
    local count = 0
    for cluster_node in data:xpath("//clusters/cluster") do
      count = count + 1
      assert.is_equal("noit", cluster_node:attr("name"))
      assert.is_equal(tostring(cluster[idx].port), cluster_node:attr("port"))
      assert.is_equal("200", cluster_node:attr("period"))
      assert.is_equal("1000", cluster_node:attr("timeout"))
      assert.is_equal("2000", cluster_node:attr("maturity"))
      assert.is_equal(tostring(seq), cluster_node:attr("seq"))
      if contains_batch_size == true then
        assert.is_not_nil(cluster_node:attr("batch_size"))
        assert.is_equal(tostring(batch_size), cluster_node:attr("batch_size"))
      else
        assert.is_nil(cluster_node:attr("batch_size"))
      end
    end
    assert.is_equal(1, count)
  end
  function validate_cluster_json(idx, code, data, contains_batch_size, batch_size)
    assert.is_equal(200, code)
    local clusters = data["clusters"]
    local cluster_count = 0
    for _ in pairs(clusters) do
      cluster_count = cluster_count + 1
    end
    assert.is_equal(1, cluster_count)
    local noit_cluster = clusters["noit"]
    assert.is_not_nil(noit_cluster)
    assert.is_equal(cluster[idx].port, noit_cluster["port"])
    assert.is_equal(200, noit_cluster["period"])
    assert.is_equal(1000, noit_cluster["timeout"])
    assert.is_equal(2000, noit_cluster["maturity"])
    assert.is_equal(seq, noit_cluster["seq"])
    if contains_batch_size == true then
      assert.is_not_nil(noit_cluster["batch_size"])
      assert.is_equal(batch_size, noit_cluster["batch_size"])
    else
      assert.is_nil(noit_cluster["batch_size"])
    end
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
      seq = seq + 1
      local code = put_cluster(api1, 1, false)
      assert.is.equal(204, code)
      code = put_cluster(api2, 2, false)
      assert.is.equal(204, code)
    end)
    it("validates the cluster topology as xml", function()
      local code, xml, raw = api1:xml("GET", "/cluster/noit")
      validate_cluster_xml(1, code, xml, false)
      code, xml, raw = api2:xml("GET", "/cluster/noit")
      validate_cluster_xml(2, code, xml, false)
    end)
    it("validates the cluster topology as json", function()
      local code, obj, raw = api1:json("GET", "/cluster/noit.json")
      validate_cluster_json(1, code, obj, false)
      code, obj, raw = api2:json("GET", "/cluster/noit.json")
      validate_cluster_json(2, code, obj, false)
    end)
    it("updates the cluster topology with batch size", function()
      seq = seq + 1
      local code = put_cluster(api1, 1, true, 5000)
      assert.is.equal(204, code)
      code = put_cluster(api2, 2, true, 5000)
      assert.is.equal(204, code)
    end)
    it("valdates the new cluster topology as xml", function()
      local code, xml, raw = api1:xml("GET", "/cluster/noit")
      validate_cluster_xml(1, code, xml, true, 5000)
      code, xml, raw = api2:xml("GET", "/cluster/noit")
      validate_cluster_xml(2, code, xml, true, 5000)
    end)
    it("validates the cluster topology as json", function()
      local code, obj, raw = api1:json("GET", "/cluster/noit.json")
      validate_cluster_json(1, code, obj, true, 5000)
      code, obj, raw = api2:json("GET", "/cluster/noit.json")
      validate_cluster_json(2, code, obj, true, 5000)
    end)
    it("updates the cluster topology with a bad batch size", function()
      seq = seq + 1
      local code = put_cluster(api1, 1, true, 0)
      assert.is.equal(204, code)
      code = put_cluster(api2, 2, true, 0)
      assert.is.equal(204, code)
    end)
    it("valdates the batch size didn't change as xml", function()
      local code, xml, raw = api1:xml("GET", "/cluster/noit")
      validate_cluster_xml(1, code, xml, true, 5000)
      code, xml, raw = api2:xml("GET", "/cluster/noit")
      validate_cluster_xml(2, code, xml, true, 5000)
    end)
    it("valdates the batch size didn't change as json", function()
      local code, obj, raw = api1:json("GET", "/cluster/noit.json")
      validate_cluster_json(1, code, obj, true, 5000)
      code, obj, raw = api2:json("GET", "/cluster/noit.json")
      validate_cluster_json(2, code, obj, true, 5000)
    end)
    it("restarts nodes", function()
      noit1:stop()
      noit2:stop()
      noit1:start()
      noit2:start()
      api1 = noit1:API()
      api2 = noit2:API()
    end)
    it("valdates the batch size persisted as xml", function()
      local code, xml, raw = api1:xml("GET", "/cluster/noit")
      validate_cluster_xml(1, code, xml, true, 5000)
      code, xml, raw = api2:xml("GET", "/cluster/noit")
      validate_cluster_xml(2, code, xml, true, 5000)
    end)
    it("valdates the batch size persisted as json", function()
      local code, obj, raw = api1:json("GET", "/cluster/noit.json")
      validate_cluster_json(1, code, obj, true, 5000)
      code, obj, raw = api2:json("GET", "/cluster/noit.json")
      validate_cluster_json(2, code, obj, true, 5000)
    end)
  end)
end)
