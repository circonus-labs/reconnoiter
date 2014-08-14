-- Copyright (c) 2014, Circonus, Inc.
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are
-- met:
--
--     * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--     * Redistributions in binary form must reproduce the above
--       copyright notice, this list of conditions and the following
--       disclaimer in the documentation and/or other materials provided
--       with the distribution.
--     * Neither the name Circonus, Inc. nor the names of its contributors 
--       may be used to endorse or promote products derived from this 
--       software without specific prior written permission.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
-- "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
-- LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
-- A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
-- OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
-- SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
-- LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
-- DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
-- THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
-- (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
-- OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>cloudwatch</name>
  <description>The Cloudwatch module gathers metrics via the AWS Cloudwatch API
  </description>
  <loader>lua</loader>
  <object>noit.module.cloudwatch</object>
  <moduleconfig>
  </moduleconfig>
  <checkconfig>
    <parameter name="url"
               required="required"
               allowed=".+"
               default="https://monitoring.amazonaws.com">The URL including schema and hostname for the Cloudwatch monitoring server.</parameter>
    <parameter name="api_key"
               required="required"
               allowed=".+">The AWS API Key</parameter>
    <parameter name="api_secret"
               required="required"
               allowed=".+">The AWS API Secret</parameter>
    <parameter name="namespace"
               required="required"
               allowed=".+">The namespace to pull parameters from</parameter>
    <parameter name="cloudwatch_metrics"
               required="required"
               allowed=".+">A comma-delimited list of metrics to pull data for</parameter>
    <parameter name="granularity"
               required="optional"
               allowed="1|5"
               default="5">The granularity of cloudwatch data to pull - one or five minutes</parameter>
    <parameter name="statistics"
               required="optional"
               allowed=".+"
               default="Average">A comma-delimited list of statistics to pull per metric (Choices are: Average, Sum, SampleCount, Maximum, Minimum - "Default" may also be specified to pull recommended values)</parameter>
    <parameter name="version"
               required="optional"
               allowed=".+"
               default="2010-08-01">The version of the Cloudwatch API to use.</parameter>
    <parameter name="dim_.+"
               required="optional"
               allowed=".+">The dimensions to query for each metric. dim_foo will set a metric with dimension "foo".</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking Cloudwatch data</title>
      <para>This example checks some Cloudwatch data</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="cloudwatch" object="noit.module.cloudwatch"/>
        </modules>
        <checks>
          <check uuid="2d42adbc-7c7a-11dd-a48f-4f59e0b654d3" module="cloudwatch" period="300000" target="176.32.99.241">
            <api_key>this_is_a_dummy_key</api_key>
            <api_secret>this_is_a_dummy_secret</api_secret>
            <namespace>AWS/EC2</namespace>
            <cloudwatch_metrics>CPUUtilization</cloudwatch_metrics>
            <statistics>Average,Sum</statistics>
            <dim_InstanceId>dummyInstance</dim_InstanceId>
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

local AWSClient = require 'noit.AWSClient'

local cache_table = { }

function init(module)
  cache_table = { }
  noit.register_dns_ignore_domain("_aws", "true")
  return 0
end

function initiate(module, check)
  local config = check.interpolate(check.config)
  local get_default = 0
  local params = {
    api_key            = config.api_key or "",
    api_secret         = config.api_secret or "",
    namespace          = config.namespace or "",
    version            = config.version or "2010-08-01",
    granularity        = config.granularity or "5"
  }
  local use_ssl = false
  local statistics = "Average"
  if config.statistics and config.statistics ~= "Default" then
    statistics = config.statistics
  else
    statistics="Average"
    get_default = 1
  end
  local default_cloudwatch_values = AWSClient:get_default_cloudwatch_values()
  local namespace_table = default_cloudwatch_values[config.namespace]
  local url = config.url or 'https://monitoring.amazonaws.com'
  local schema, host, port, uri = string.match(url, "^(https?)://([^:/]*):?([0-9]*)(/?.*)$");
  if schema == nil then
    schema = 'http'
  end
  if uri == nil then
    uri = '/'
  end
  if port == '' or port == nil then
    if schema == 'http' then
      port = 80
    elseif schema == 'https' then
      port = 443
    else
      error(schema .. " not supported")
    end
  end
  if schema == "https" then
    use_ssl = true
  end
  params["port"] = port
  params["host"] = host
  params["use_ssl"] = use_ssl
  local metrics    = noit.extras.split(config.cloudwatch_metrics, ",")
  local stats = noit.extras.split(statistics, ",")
  local dimensions = { }
    
  local uuid         = check.checkid
  local table_hit    = cache_table[uuid]
  local current_time = os.time()

  if not table_hit then
    local new_hash = {}
    new_hash['timestamp'] = 0;
    new_hash['metrics'] = {};
    cache_table[uuid] = new_hash;
  end

  -- assume the worst.
  check.bad()
  check.unavailable()

  local metric_count = 0
  local check_metric = function (name,hash,statistics)
    if hash ~= nil then
      if hash["results"] ~= nil then
        for k, v in pairs(hash["results"]) do
          local metric_name = name
          if k ~= 'Average' then
            metric_name = name .. "_" .. k
          end
          if value == nil then
            check.metric_double(metric_name, v)
          elseif tonumber(value) ~= nil then
            if string.find(value, "%.") ~= nil then
              check.metric_double(metric_name, v)
            else
              check.metric_uint64(metric_name, v)
            end
          else
            check.metric_string(metric_name, v)
          end
          metric_count = metric_count + 1
        end
      else
        local stat_table = statistics
        if (get_default == 1) then
          stat_table = default_cloudwatch_values["default"]
          if (namespace_table ~= nil and namespace_table[name] ~= nil) then
            stat_table = namespace_table[name]
          end
        end
        for k, v in ipairs(stat_table) do
          local met_name = name
          if v ~= 'Average' then
            met_name = name .. "_" .. v
          end
          check.metric_double(met_name, nil)
          metric_count = metric_count + 1
        end
      end
    end
  end

  --Make sure we don't pull data from the API more frequently than the
  --period
  local cache_timeout = (params.granularity * 60) - 1
  if current_time - cache_table[uuid]['timestamp'] >= cache_timeout then
    -- We've gone over the cache timeout... get new values
    local dimension_count = 1
    for key, value in pairs(check.config) do
      _, _, dimension_type = string.find(key, "^%s*dim_(.*)%s*$")
      if dimension_type ~= nil then
        dimensions[dimension_count] = {}
        dimensions[dimension_count]["Name"] = dimension_type
        dimensions[dimension_count]["Value"] = value
        dimension_count = dimension_count+1
      end
    end
    cache_table[uuid]['metrics'] = {}
    local dns = noit.dns()
    local r = dns:lookup(host)
    if not r or r.a == nil then
      check.status("failed to resolve " .. host)
      cache_table[uuid]['timestamp'] = 0
      return
    end

    local aws = AWSClient:new(params, metrics, stats, dimensions, get_default)
    local rv, err = aws:perform(r.a, cache_table[uuid]['metrics'])
    if rv ~= 0 then
      check.status(err or "unknown error")
      cache_table[uuid]['timestamp'] = 0
      return
    end
    cache_table[uuid]['timestamp'] = current_time
  end

  for k,v in pairs(cache_table[uuid]['metrics']) do
    check_metric(k, v, stats)
  end

  check.available()
  if metric_count > 0 then check.good() else check.bad() end
  check.status("metrics=" .. metric_count)
end

