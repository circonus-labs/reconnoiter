describe("check config", function()
  local noit, api
  setup(function()
    Reconnoiter.clean_workspace()
    noit = Reconnoiter.TestNoit:new()
  end)
  teardown(function() if noit ~= nil then noit:stop() end end)

  local uuid = mtev.uuid()
  local check_xml = function(seq)
    return
[=[<?xml version="1.0" encoding="utf8"?>
<check>
  <attributes>
    <target>127.0.0.1</target>
    <seq>]=] .. tostring(seq) .. [=[</seq>
    <period>5000</period>
    <timeout>1000</timeout>
    <name>dummy</name>
    <filterset>allowall</filterset>
    <module>selfcheck</module>
  </attributes>
  <config>
    <dummy></dummy>
    <anotherdummy/>
    <setdummy>hello</setdummy>
  </config>
</check>]=]
  end

  function validate_check_config(code, doc)
    assert.is_equal(200, code)
    assert.is_not_nil(doc)
    local attr_count = 0
    local target_count = 0
    local seq_count = 0
    local period_count = 0 
    local timeout_count = 0
    local name_count = 0
    local filterset_count = 0
    local module_count = 0
    local uuid_count = 0
    local config_count = 0
    local dummy_count = 0
    local anotherdummy_count = 0
    local setdummy_count = 0
    for result in doc:xpath('/check/attributes') do
      attr_count = attr_count + 1
      -- We should never hit a second loop here
      assert.is_equal(1, attr_count)
      local child_iter = result:children()
      local unknown_nodes = 0
      local total_nodes = 0
      for child_node in child_iter do
        local node_type = child_node:name()
        local node_value = child_node:contents()
        if node_type == "target" then
          assert.is_equal(0, target_count)
          target_count = target_count + 1
          assert.is_equal("127.0.0.1", node_value)
        elseif node_type == "seq" then
          assert.is_equal(0, seq_count)
          seq_count = seq_count + 1
          assert.is_equal("100", node_value)
        elseif node_type == "period" then
          assert.is_equal(0, period_count)
          period_count = period_count + 1
          assert.is_equal("5000", node_value)
        elseif node_type == "timeout" then
          assert.is_equal(0, timeout_count)
          timeout_count = timeout_count + 1
          assert.is_equal("1000", node_value)
        elseif node_type == "name" then
          assert.is_equal(0, name_count)
          name_count = name_count + 1
          assert.is_equal("dummy", node_value)
        elseif node_type == "filterset" then
          assert.is_equal(0, filterset_count)
          filterset_count = filterset_count + 1
          assert.is_equal("allowall", node_value)
        elseif node_type == "module" then
          assert.is_equal(0, module_count)
          module_count = module_count + 1
          assert.is_equal("selfcheck", node_value)
        elseif node_type == "uuid" then
          assert.is_equal(0, uuid_count)
          uuid_count = uuid_count + 1
          assert.is_equal(uuid, node_value)
        elseif node_type ~= "text" then
          unknown_nodes = unknown_nodes + 1
        end
        if node_type ~= "text" then
          total_nodes = total_nodes + 1
        end
      end
      assert.is_equal(0, unknown_nodes)
      assert.is_equal(8, total_nodes)
    end
    assert.is_equal(1, attr_count)
    for result in doc:xpath('/check/config') do
      config_count = config_count + 1
      -- We should never hit a second loop here
      assert.is_equal(1, config_count)
      local child_iter = result:children()
      local unknown_nodes = 0
      local total_nodes = 0
      for child_node in child_iter do
        local node_type = child_node:name()
        local node_value = child_node:contents()
        if node_type == "dummy" then
          assert.is_equal(0, dummy_count)
          dummy_count = dummy_count + 1
          assert.is_equal("", node_value)
        elseif node_type == "anotherdummy" then
          assert.is_equal(0, anotherdummy_count)
          anotherdummy_count = anotherdummy_count + 1
          assert.is_equal("", node_value)
        elseif node_type == "setdummy" then
          assert.is_equal(0, setdummy_count)
          setdummy_count = setdummy_count + 1
          assert.is_equal("hello", node_value)
        elseif node_type ~= "text" then
          unknown_nodes = unknown_nodes + 1
        end 
        if node_type ~= "text" then
          total_nodes = total_nodes + 1
        end
      end
      assert.is_equal(0, unknown_nodes)
      assert.is_equal(3, total_nodes)
    end
    assert.is_equal(1, config_count)
  end

  it("should start", function()
    assert.is_true(noit:start():is_booted())
    api = noit:API()
  end)

  describe("returns correct config", function()
    local capa, start, elapsed
    it("is missing", function()
      local code, obj = api:raw("GET", "/checks/show/" .. uuid)
      assert.is.equal(404, code)
    end)
    it("returns correct config", function()
      local code, doc, str = api:xml("PUT", "/checks/set/" .. uuid, check_xml(100))
      validate_check_config(code, doc)
      code, doc, str = api:xml("GET", "/checks/show/" .. uuid)
      validate_check_config(code, doc)
    end)
  end)
end)
