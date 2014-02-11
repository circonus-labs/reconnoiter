module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>elasticsearch</name>
  <description><para>The elasticsearch module pulls JSON stats from Elasticsearch</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.resmon</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               default="http:///_cluster/nodes/_local/stats?os=true&amp;process=true&amp;fs=true"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="port"
               required="optional"
               default="9200"
               allowed="\d+">The TCP port can be specified to overide the default of 9200.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking elasticsearch services on a node es1.int.foo_</title>
      <para>This example checks the Elasticsearch service on the es1.int.foo_ node.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="elasticsearch" object="noit.module.resmon"/>
        </modules>
        <checks>
          <es target="es1.int.foo_" module="elasticsearch">
            <check uuid="2503f08c-7a0f-11e3-9ba0-7cd1c3dcddf7"/>
          </es>
        </checks>
      </noit>
    ]]></programlisting>
    </example>
  </examples>
</module>
]=]);
  return 0
end

function fix_config(config)
  if not config.url then
    config.url = 'http:///_cluster/nodes/_local/stats?os=true&process=true&fs=true'
  end
  if not config.port then
    config.port = 9200
  end
  return config
end

function set_check_metric(check, name, type, value)
    if type == 'i' then
        check.metric_int32(name, value)
    elseif type == 'I' then
        check.metric_uint32(name, value)
    elseif type == 'l' then
        check.metric_int64(name, value)
    elseif type == 'L' then
        check.metric_uint64(name, value)
    elseif type == 'n' then
        check.metric_double(name, value)
    elseif type == 's' then
        check.metric_string(name, value)
    else
        check.metric(name, value)
    end
end

function json_metric(check, prefix, o)
    local cnt = 0
    if type(o) == "table" then
        local has_type, has_value = false, false
        for k, v in pairs(o) do
            if k == "_type" then has_type = true
            elseif k == "_value" then has_value = true
            else
                local np = prefix and (prefix .. '`' .. k) or k
                if prefix == "nodes" and v["hostname"] then
                  if check.config.url == nil or string.find(check.config.url, "/_local/") then
                    np = prefix
                  else
                    np = prefix .. '`' .. v["hostname"]
                  end
                end
                cnt = cnt + json_metric(check, np, v)
            end
        end
        if has_type and has_value then
          set_check_metric(check, prefix, o._type, o._value)
          cnt = cnt + 1
        end
    elseif type(o) == "string" then
        check.metric(prefix, o)
        cnt = cnt + 1
    elseif type(o) == "number" then
        check.metric_double(prefix, o)
        cnt = cnt + 1
    elseif type(o) == "boolean" then
        check.metric_int32(prefix, o and 1 or 0)
        cnt = cnt + 1
    else
        noit.log("debug", "got unknown type: " .. type(o) .. "\n")
        cnt = 0
    end
    return cnt
end

function json_to_metrics(check, doc)
    local services = 0
    check.available()
    local data = doc:document()
    if data ~= nil then
      services = json_metric(check, nil, data)
    end
    check.metric_uint32("services", services)
    if services > 0 then check.good() else check.bad() end
    check.status("services=" .. services)
end

function process(check, output)
  local jsondoc = noit.parsejson(output)
  json_to_metrics(check, jsondoc)
end
