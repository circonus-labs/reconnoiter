describe("cluster", function()
  local noit1, noit2, api1, api2
  local cluster = {}
  local global_seq = 1239834
  local global_filter_seq = 1234
  -- Static uuids here to make tests (hashing) repeatable
  local uuid1 = '5a74f6f2-3125-44de-84e7-6ea7275e5fee'
  local uuid2 = 'ab419eda-6f51-466f-b086-63ae940c8147'
  local check_uuid = '39568546-da44-465e-abd8-a348a27b2fd5'
  local basic_cull_true_filterset, basic_cull_true_filterset_expected
  local basic_cull_false_filterset, basic_cull_false_filterset_expected
  local rule_data_set_one
  setup(function()
    Reconnoiter.clean_workspace()
    noit1 = Reconnoiter.TestNoit:new("node1")
    noit2 = Reconnoiter.TestNoit:new("node2")
    cluster = { { id = uuid1, port = noit1:api_port() }, { id = uuid2, port = noit2:api_port() } }

    -- Set up some dummy sets of rules
    local rule
    rule_data_set_one = {}

    rule = {}
    rule.type = "accept"
    rule.metric = "^dummymetric$"
    rule.target = "^www.github.com$"
    rule.module = "^httptrap$"
    table.insert(rule_data_set_one, rule)
    rule = {}
    rule.type = "accept"
    rule.name = "^blarg$"
    table.insert(rule_data_set_one, rule)
    rule = {}
    rule.type = "accept"
    rule.metric_hash = {}
    rule.module_hash = {}
    table.insert(rule.metric_hash, "a")
    table.insert(rule.metric_hash, "b")
    table.insert(rule.metric_hash, "c")
    table.insert(rule.module_hash, "http")
    table.insert(rule_data_set_one, rule)
    rule = {}
    rule.type = "deny"
    table.insert(rule_data_set_one, rule)
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
  function make_filter_xml(name, cull, rule_data_table)
    local expected = {}
    global_filter_seq = global_filter_seq + 1
    expected.name = name
    expected.seq = tostring(global_filter_seq)
    expected.cull = cull
    expected.rules = {}

    local xml = [=[<?xml version="1.0" encoding="utf8"?>
<filterset name="]=] .. name .. [=[" cull="]=] .. cull .. [=[" seq="]=] .. global_filter_seq .. [=[">
]=]
    for index, rule in ipairs(rule_data_table) do
      local expected_rule = {}
      expected_rule.type = rule.type
      xml = xml .. [=[
  <rule type="]=] .. rule.type .. [=["]=]
      if rule.metric ~= nil then
        xml = xml .. [=[ metric="]=] .. rule.metric .. [=["]=]
        expected_rule.metric = rule.metric
      end
      if rule.target ~= nil then
        xml = xml .. [=[ target="]=] .. rule.target .. [=["]=]
        expected_rule.target = rule.target
      end
      if rule.module ~= nil then
        xml = xml .. [=[ module="]=] .. rule.module .. [=["]=]
        expected_rule.module = rule.module
      end
      if rule.name ~= nil then
        xml = xml .. [=[ name="]=] .. rule.name .. [=["]=]
        expected_rule.name = rule.name
      end
      xml = xml .. [=[>
]=]
      if rule.metric_hash ~= nil then
        expected_rule.metric_hash = {}
        for i, metric in ipairs(rule.metric_hash) do
          xml = xml .. [=[
    <metric>]=] .. metric .. [=[</metric>
]=]
          table.insert(expected_rule.metric_hash, metric)
        end
      end
      if rule.target_hash ~= nil then
        expected_rule.target_hash = {}
        for i, target in ipairs(rule.target_hash) do
          xml = xml .. [=[
    <target>]=] .. target .. [=[</target>
]=]
          table.insert(expected_rule.target_hash, target)
        end
      end
      if rule.module_hash ~= nil then
        expected_rule.module_hash = {}
        for i, module in ipairs(rule.module_hash) do
          xml = xml .. [=[
    <module>]=] .. module .. [=[</module>
]=]
          table.insert(expected_rule.module_hash, module)
        end
      end
      if rule.name_hash ~= nil then
        expected_rule.name_hash = {}
        for i, name in ipairs(rule.name_hash) do
          xml = xml .. [=[
    <name>]=] .. name .. [=[</name>
]=]
          table.insert(expected_rule.name_hash, name)
        end
      end
      xml = xml .. [=[
  </rule>
]=]
      table.insert(expected.rules, expected_rule)
    end
    xml = xml .. [=[
</filterset>]=]
    return xml, expected
  end
  function put_cluster(api, nodes, idx)
    local payload = '<?xml version="1.0" encoding="utf8"?><cluster name="noit" port="' .. tostring(nodes[idx].port) .. '" period="200" timeout="1000" maturity="2000" key="poison" seq="1">'
    for i, node in ipairs(nodes) do
      payload = payload .. '<node id="' .. node.id .. '" cn="noit-test" address="127.0.0.1" port="' .. tostring(node.port) .. '"/>'
    end
    payload = payload .. '</cluster>'
    return api:xml("POST", "/cluster", payload)
  end

  function check_filter_value(expected_code, code, expected_doc, doc)
    assert.is_equal(expected_code, code)
    if expected_doc == nil then
      assert.is_nil(doc)
    else
      assert.is_not_nil(doc)
      local rootnode = doc:root()
      local name = rootnode:attr("name")
      assert.is_equal(expected_doc["name"], name)
      local cull = rootnode:attr("cull")
      assert.is_equal(expected_doc["cull"], cull)
      local seq = rootnode:attr("seq")
      assert.is_equal(expected_doc["seq"], seq)
      local rule
      local rule_count = 0
      for rule in doc:xpath("/filterset/rule") do
        rule_count = rule_count + 1
      end
      local expected_rule_count = #expected_doc["rules"]
      assert.is_equal(expected_rule_count, rule_count)
      rule_count = 0
      for rule in doc:xpath("/filterset/rule") do
        rule_count = rule_count + 1
        local expect_rule = expected_doc["rules"][rule_count]
        assert.is_not_nil(expect_rule)
        local rule_type = rule:attr("type")
        assert.is_not_nil(rule_type)
        assert.is_equal(expect_rule["type"], rule_type)
        local child_iter = rule:children()
        for child_node in child_iter do
        end
      end
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
    basic_cull_true_filterset, basic_cull_true_filterset_expected = make_filter_xml("basic_cull_true", "true", rule_data_set_one)
    basic_cull_false_filterset, basic_cull_false_filterset_expected = make_filter_xml("basic_cull_false", "false", rule_data_set_one)

    it("is missing on node1", function()
      local code, doc  = api1:xml("GET", "/filters/show/basic_cull_true")
      check_filter_value(404, code, nil, doc)
      code, doc  = api1:xml("GET", "/filters/show/basic_cull_false")
      check_filter_value(404, code, nil, doc)
    end)
    it("is missing on node2", function()
      local code  = api2:xml("GET", "/filters/show/basic_cull_true")
      check_filter_value(404, code, nil, doc)
      code  = api2:xml("GET", "/filters/show/basic_cull_false")
      check_filter_value(404, code, nil, doc)
    end)
    it("puts one on node1", function()
      local code, doc  = api1:xml("PUT", "/filters/set/basic_cull_true", basic_cull_true_filterset)
      check_filter_value(200, code, basic_cull_true_filterset_expected, doc)
    end)
    it("puts other one on node2", function()
      local code, doc = api2:xml("PUT", "/filters/set/basic_cull_false", basic_cull_false_filterset)
      check_filter_value(200, code, basic_cull_false_filterset_expected, doc)
    end)
    it("has replicated filters across the cluster", function()
      mtev.sleep(5)
      local code, doc = api1:xml("GET", "/filters/show/basic_cull_true")
      check_filter_value(200, code, basic_cull_true_filterset_expected, doc)
      code, doc = api2:xml("GET", "/filters/show/basic_cull_true")
      check_filter_value(200, code, basic_cull_true_filterset_expected, doc)
      code, doc = api1:xml("GET", "/filters/show/basic_cull_false")
      check_filter_value(200, code, basic_cull_false_filterset_expected, doc)
      code, doc = api2:xml("GET", "/filters/show/basic_cull_false")
      check_filter_value(200, code, basic_cull_false_filterset_expected, doc)
    end)
    it("culls from nodes", function()
      local code = api1:raw("POST", "/filters/cull")
      assert.is_equal(200, code)
      code = api2:raw("POST", "/filters/cull")
      assert.is_equal(200, code)
    end)
    it("culled where cull is true", function()
      local code = api1:xml("GET", "/filters/show/basic_cull_true")
      check_filter_value(404, code, nil, doc)
      code = api2:xml("GET", "/filters/show/basic_cull_true")
      check_filter_value(404, code, nil, doc)
    end)
    it("didn't cull where cull is false", function()
      local code, doc = api1:xml("GET", "/filters/show/basic_cull_false")
      check_filter_value(200, code, basic_cull_false_filterset_expected, doc)
      code, doc = api2:xml("GET", "/filters/show/basic_cull_false")
      check_filter_value(200, code, basic_cull_false_filterset_expected, doc)
    end)
    it("deletes a filter from nodes", function()
      local code = api1:raw("DELETE", "/filters/delete/basic_cull_false")
      assert.is_equal(200, code)
      code = api2:raw("DELETE", "/filters/delete/basic_cull_false")
      assert.is_equal(200, code)
    end)
    it("is missing from nodes", function()
      local code = api1:xml("GET", "/filters/show/basic_cull_false")
      check_filter_value(404, code, nil, doc)
      code = api2:xml("GET", "/filters/show/basic_cull_false")
      check_filter_value(404, code, nil, doc)
    end)
    it("still has allowall on all nodes", function()
      local code = api1:xml("GET", "/filters/show/allowall")
      check_filter_value(200, code)
      code = api2:xml("GET", "/filters/show/allowall")
      check_filter_value(200, code)
    end)
  end)
end)
