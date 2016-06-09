module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>sparkpost</name>
  <description><para>The sparkpost module pulls JSON stats from Sparkpost</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.resmon</object>
  <checkconfig>
    <parameter name="api_key"
               required="required"
               allowed=".+">The API key for Sparkpost that has "metrics" access</parameter>
    <parameter name="campaigns"
               required="optional"
               allowed=".+">A comma separated list of campaigns to restrict the metrics to.</parameter>
    <parameter name="templates"
               required="optional"
               allowed=".+">A comma separated list of templates to restrict the metrics to.</parameter>
    <parameter name="subaccounts"
               required="optional"
               allowed=".+">A comma separated list of subaccounts to restrict the metrics to.</parameter>
    <parameter name="sending_ips"
               required="optional"
               allowed=".+">A comma separated list of sending IPs to restrict the metrics to.</parameter>
    <parameter name="sending_domains"
               required="optional"
               allowed=".+">A comma separated list of sending domains to restrict the metrics to.</parameter>
    <parameter name="domains"
               required="optional"
               allowed=".+">A comma separated list of receiving domains to restrict the metrics to.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking sparkpost</title>
      <para>This example checks the Sparkpost service.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="sparkpost" object="noit.module.resmon"/>
        </modules>
        <checks>
          <es target="api.sparkpost.com" module="sparkpost">
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

function fix_config(inconfig)
  local config = {}
  local dims = {'campaigns','templates','subaccounts','sending_ips',
                'sending_domains','domains'}
  config.metrics = inconfig.metrics or 'count_accepted,count_admin_bounce,count_block_bounce,count_bounce,count_clicked,count_delayed,count_delayed_first,count_delivered,count_delivered_first,count_delivered_subsequent,count_generation_failed,count_generation_rejection,count_hard_bounce,count_inband_bounce,count_injected,count_outofband_bounce,count_policy_rejection,count_rejected,count_rendered,count_sent,count_soft_bounce,count_spam_complaint,count_targeted,count_undetermined_bounce,count_unique_clicked,count_unique_confirmed_opened,count_unique_rendered,total_delivery_time_first,total_delivery_time_subsequent,total_msg_volume'
  
  config.url = 'https://api.sparkpost.com/api/v1/metrics/deliverability?timezone=UTC&metrics=' .. config.metrics
  for _, p in ipairs(dims) do
    if inconfig[p] ~= nil then
      config.url = config.url .. '&' .. p .. '=' .. inconfig[p]
    end
  end
  local date = os.date("!*t", os.time())
  local tstring = string.format("%04d-%02d-%02dT00:00",
                                date.year, date.month, date.day)
  config.url = config.url .. '&from=' .. tstring
  config['header_Authorization'] = inconfig.api_key
  config['header_Accept'] = 'application/json'
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
      services = json_metric(check, nil, data.results[1])
    end
    if services > 0 then check.good() else check.bad() end
    check.status("services=" .. services)
end

function process(check, output)
  local jsondoc = mtev.parsejson(output)
  json_to_metrics(check, jsondoc)
end
