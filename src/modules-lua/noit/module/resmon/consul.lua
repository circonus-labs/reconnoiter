module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>consul</name>
  <description><para>The consul module pulls JSON stats from Hashicorp Consul health checks</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.resmon</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               default="http://host:port/v1/health/state/any"
               allowed=".+">The Consul health check url, see consul docs: https://www.consul.io/docs/agent/http/health.html</parameter>
    <parameter name="port"
               required="optional"
               default="8500"
               allowed="\d+">The TCP port can be specified to overide the default of 8500.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking health of consul services</title>
      <para>This example checks the health of consul servers service from the c1.int.foo node.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="consul" object="noit.module.resmon"/>
        </modules>
        <checks>
            <check uuid="2503f08c-7a0f-11e3-9ba0-7cd1c3dcddf7" target="c1.int.foo" period="60000" timeout="10000" name="test.consul" module="consul">
             <config>
               <url>http://c1.int.foo:8500/v1/health/state/any</url>
               <port>8500</port>
             </config>
            </check>
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
    config.url = 'http:///v1/health/state/any'
  end
  if not config.port then
    config.port = 8500
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

function split(str, delim, maxNb)
   -- Eliminate bad cases...
   if string.find(str, delim) == nil then
      return { str }
   end
   if maxNb == nil or maxNb < 1 then
      maxNb = 0    -- No limit
   end
   local result = {}
   local pat = "(.-)" .. delim .. "()"
   local nb = 0
   local lastPos
   for part, pos in string.gfind(str, pat) do
      nb = nb + 1
      result[nb] = part
      lastPos = pos
      if nb == maxNb then
         break
      end
   end
   -- Handle the last field
   if nb ~= maxNb then
      result[nb + 1] = string.sub(str, lastPos)
   end
   return result
end

function json_metric(check, prefix, o, index, count_table)
   local cnt = 0
   if type(o) == "table" then
      for k, v in pairs(o) do
         local np
         if type(v) ~= "table" then
            mtev.log("debug", "Have key: " .. k .. ", value: " .. v .. "\n")
            np = prefix .. '`' .. k
            mtev.log("debug", "Metric is: '" .. np .. "'\n")
            set_check_metric(check, np, string.find(k, "Index") and 'L' or 's', v) 
            if count_table ~= nil and k == "Status" then
               if count_table[v] == nil then
                  count_table[v] = 1
               else
                  count_table[v] = count_table[v] + 1
               end
            end
            cnt = cnt + 1
         else
            mtev.log("debug", "Table, recursing\n")
            if string.find(check.config.url, "/health/state") then
               np = "check"
               local l = split(check.config.url, "/")
               local last = #l
               np = np .. '`' .. l[last] .. '`' .. k
            end
            mtev.log("debug", "Prefix is: '" .. np .. "'\n")
            
            cnt = cnt + json_metric(check, np, v, k, count_table)
         end
      end
      return cnt
   end
end

function json_to_metrics(check, doc)
    local services = 0
    check.available()
    local data = doc:document()
    local count_table = {}
    if data ~= nil then
      services = json_metric(check, nil, data, 0, count_table)
    end
    
    for k, v in pairs(count_table) do
       set_check_metric(check, "check`Num`" .. k .. "`services", 'L', v)
    end

    if services > 0 then check.good() else check.bad() end
    check.status("services=" .. services)
end

function process(check, output)
  local jsondoc = mtev.parsejson(output)
  json_to_metrics(check, jsondoc)
end
