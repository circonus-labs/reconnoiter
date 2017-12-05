module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>promtext</name>
  <description><para>The Prometheus-compatible module pulls stats from scraping endpoints</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.resmon</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               default="http:///metrics"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="port"
               required="optional"
               default="9163"
               allowed="\d+">The TCP port can be specified to override the default of 9163.</parameter>
  </checkconfig>
</module>
]=]);
  return 0
end

function fix_config(inconfig)
  local config = {}
  for k,v in pairs(inconfig) do config[k] = v end
  if not config.url then
    config.url = 'http:///metrics'
  end
  if not config.port then
    config.port = 9163
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

function normalize_name(name)
    return name:gsub("(%b{})$", function(tagstr)
      local tags = {}
      for tag,val in tagstr:sub(2,-1):gmatch("([^=]+)=\"([^\"]+)\",?") do
        tag = tag:gsub("[^a-zA-Z0-9._-]", "")
        val = val:gsub("[^a-zA-Z0-9._-]", "")
        tags[#tags+1] = tag .. ":" .. val
      end
      if #tags == 0 then return "" end
      return "|ST[" .. table.concat(tags, ",", 1, #tags) .. "]"
    end)
end

function process(check, output)
  for line in output:gmatch("[^\r\n]+") do
    if line:sub(1,1) ~= "#" then
      line = line:gsub("\\.", "")
      line = line:gsub("\\", "")
      local name, value = line:match("(%S+%b{})%s+(.+)$")
      local time = nil
      if name == nil or value == nil then name, value = line:match("(%S+)%s+(.+)$") end
      value = value:gsub("%s+([-]?%d+)$", function(m, v) time = m return "" end)
      name = normalize_name(name)
      if time ~= nil then
        check.metric_double(name, value, time / 1000, (time % 1000) * 1000)
      else
        check.metric_double(name, value)
      end
    end
  end
end
