module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>elemental-transcoder</name>
  <description><para>The elemental-transcoder module pulls JSON stats from Elemental Transcoder applicances.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.resmon</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               default="http:///alerts/get_status.json"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="port"
               required="optional"
               default="80"
               allowed="\d+">The TCP port can be specified to overide the default of 80.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking Elemental Transcoder services on a node et1.int.foo_</title>
      <para>This example checks the Elemental Transcoder service on the et1.int.foo_ node.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="elemental-transcoder" object="noit.module.resmon"/>
        </modules>
        <checks>
          <et target="et1.int.foo_" module="elemental-transcoder">
            <check uuid="2503f08c-7a0f-11e3-9ba0-7cdc13dcddf7"/>
          </et>
        </checks>
      </noit>
    ]]></programlisting>
    </example>
  </examples>
</module>
]=]);
  return 0
end

function fix_config(inconfig)
  local config = {}
  for k,v in pairs(inconfig) do config[k] = v end
  if not config.url then
    config.url = 'http:///alerts/get_status.json'
  end
  if not config.port then
    config.port = 80
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
    if prefix == "date" then return 0 end
    if type(o) == "table" then
        local has_type, has_value = false, false
        for k, v in pairs(o) do
            if k == "_type" then has_type = true
            elseif k == "_value" then has_value = true
            else
                if type(k) == "number" and k%1 == 0 then k = k-1 end
                local np = prefix and (prefix .. '`' .. k) or k
                if prefix == "gpu_temperatures" then
                    local idx = string.match(k, "gpu_temperature_(%d+)")
                    if idx ~= nil then
                        np = "gpu`" .. idx .. "`temperature"
                        local temp, units = string.match(v, "(%d+)([CF])")
                        v = tonumber(temp)
                        if v ~= nil then
                            if units == 'F' then v = (v - 32) / 9 * 5 end
                        end
                    end
                    set_check_metric(check, np, "n", v)
                    cnt = cnt + 1
                else
                    cnt = cnt + json_metric(check, np, v)
                end
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
        mtev.log("debug", "got unknown type: " .. type(o) .. "\n")
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
    if services > 0 then check.good() else check.bad() end
    check.status("services=" .. services)
end

function process(check, output)
  local jsondoc = mtev.parsejson(output)
  json_to_metrics(check, jsondoc)
end
