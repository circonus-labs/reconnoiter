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

local AWSClient = {};
AWSClient.__index = AWSClient;

local HttpClient = require 'mtev.HttpClient'

-- Certain Cloudwatch metrics have suggested values to pull. This table
-- defined these on a per-metric basis for each namespace. If no value is
-- given for a namespace or a namespace/metric combo, we will default to
-- using the "default" value to pull for that metric. This will only be used
-- if a specific metric isn't specified - we will overwrite these values with
-- any user-specified ones.
--
-- There are based on the AWS Development guide, located here:
-- http://docs.aws.amazon.com/AmazonCloudWatch/latest/DeveloperGuide/CW_Support_For_AWS.html
local default_cloudwatch_values = {
  ["default"]={"Average"},
  ["AWS/ELB"]={
    ["HealthyHostCount"]={"Average"},
    ["UnHealthyHostCount"]={"Average"},
    ["RequestCount"]={"Sum"},
    ["Latency"]={"Average"},
    ["HTTPCode_ELB_4XX"]={"Sum"},
    ["HTTPCode_ELB_5XX"]={"Sum"},
    ["HTTPCode_Backend_2XX"]={"Sum"},
    ["HTTPCode_Backend_3XX"]={"Sum"},
    ["HTTPCode_Backend_4XX"]={"Sum"},
    ["HTTPCode_Backend_5XX"]={"Sum"},
    ["BackendConnectionErrors"]={"Sum"},
    ["SurgeQueueLength"]={"Maximum"},
    ["SpilloverCount"]={"Sum"}
  },
  ["AWS/SNS"]={
    ["NumberOfMessagesPublished"]={"Sum"},
    ["PublishSize"]={"Average"},
    ["NumberOfNotificationsDelivered"]={"Sum"},
    ["NumberOfNotificationsFailed"]={"Sum"}
  },
  ["AWS/SQS"]={
    ["NumberOfMessagesSent"]={"Sum"},
    ["SentMessageSize"]={"Average"},
    ["NumberOfMessagesReceived"]={"Sum"},
    ["NumberOfEmptyReceives"]={"Sum"},
    ["NumberOfMessagesDeleted"]={"Sum"},
    ["ApproximateNumberOfMessagesDelayed"]={"Average"},
    ["ApproximateNumberOfMessagesVisible"]={"Average"},
    ["ApproximateNumberOfMessagesNotVisible"]={"Average"}
  },
  ["AWS/DynamoDB"]={
    ["SuccessfulRequestLatency"]={"Average"},
    ["UserErrors"]={"Sum"},
    ["SystemErrors"]={"Sum"},
    ["ThrottledRequests"]={"Sum"},
    ["ReadThrottleEvents"]={"Sum"},
    ["WriteThrottleEvents"]={"Sum"},
    ["ProvisionedReadCapacityUnits"]={"Average","Sum"},
    ["ProvisionedWriteCapacityUnits"]={"Average","Sum"},
    ["ConsumedWriteCapacityUnits"]={"Average","Sum"},
    ["ReturnedItemCount"]={"Average","Sum"}
  },
  ["AWS/Route53"]={
    ["HealthCheckStatus"]={"Minimum"},
    ["HealthCheckPercentageHealthy"]={"Average"}
  }
}

function __genOrderedIndex( t )
  local orderedIndex = {}
  for key in pairs(t) do
    table.insert( orderedIndex, key )
  end
  table.sort( orderedIndex )
  return orderedIndex
end

function orderedNext(t, state)
  if state == nil then
    -- the first time, generate the index
    t.__orderedIndex = __genOrderedIndex( t )
    key = t.__orderedIndex[1]
    return key, t[key]
  end

  -- fetch the next value
  key = nil
  for i = 1,table.getn(t.__orderedIndex) do
    if t.__orderedIndex[i] == state then
      key = t.__orderedIndex[i+1]
    end
  end

  if key then
    return key, t[key]
  end

  -- no more value to return, cleanup
  t.__orderedIndex = nil
  return
end

function orderedPairs(t)
  return orderedNext, t, nil
end

function escape(s)
  s = string.gsub(s, "([%%,:/&=+%c%s])", function (c)
    return string.format("%%%02X", string.byte(c))
  end)
  return s
end

function find_xml_node(root, tofind)
  for entry in root:children() do
    if entry:name() == tofind then
      return entry
    end
  end

  return nil
end

function update_table(table, entry, ts_value, unit_value, statistics, namespace_table, get_default, metric)
  table["timestamp"] = ts_value
  table["unit"] = unit_value
  table["results"] = {}
  local stats_table = statistics
  if (get_default == 1) then
    stats_table = default_cloudwatch_values["default"]
    if (namespace_table ~= nil and namespace_table[metric] ~= nil) then
      stats_table = namespace_table[metric]
    end
  end
  for num, stat in ipairs(stats_table) do
    stat = string.gsub(stat, "^%s*(.-)%s*$", "%1")
    local node = find_xml_node(entry, stat)
    if node ~= nil then
      table["results"][stat] = node:contents()
    end
  end
end

function AWSClient:new(params, metrics, statistics, dimensions, get_default)
  local obj = { }
  setmetatable(obj, AWSClient)
  obj.params = params or { }
  obj.metrics = metrics or { }
  obj.statistics = statistics or { }
  obj.dimensions = dimensions or { }
  obj.get_default = get_default or 0
  return obj
end

function AWSClient:get_default_cloudwatch_values()
  return default_cloudwatch_values
end

function AWSClient:constructBaseQuery(baseTable)
  local toReturn = ''

  for key, value in orderedPairs(baseTable) do
    toReturn =  toReturn .. key .. '=' .. escape(value) .. '&'
  end
  toReturn = string.sub(toReturn, 1, -2)

  return toReturn
end

function AWSClient:convert_to_hex(str)
   return ( str:gsub( '.', function ( c )
               return string.format('%02x', string.byte( c ) )
            end ) )
end

function AWSClient:perform(target, cache_table)
  local api_key     = self.params.api_key
  local api_secret  = self.params.api_secret
  local namespace   = self.params.namespace
  local version     = self.params.version
  local granularity = self.params.granularity
  local host        = self.params.host
  local port        = self.params.port
  local use_ssl     = self.params.use_ssl
  local statistics  = self.statistics
  local metrics     = self.metrics
  local get_default = self.get_default
  local region      = self.params.region
  local service     = self.params.service
  local minutes_back = self.params.minutes_back or "10";
  local seconds_back = minutes_back * 60;
  
  local time = os.time()
  local datestamp=os.date("!%Y%m%d", time)
  local timestamp = os.date("!%Y%m%dT%H%M%SZ", time)

  time = time - (time % 60)
  local start_time
  local end_time
  --Billing is a special case.... pull back the last 8 hours, use the most 
  --recent value available
  if (namespace == "AWS/Billing") then
    start_time = os.date("!%Y-%m-%dT%H:%M:%S.000Z", time-(60*60*8))
    end_time = os.date("!%Y-%m-%dT%H:%M:%S.000Z", time+300)
  else
    start_time = os.date("!%Y-%m-%dT%H:%M:%S.000Z", time-seconds_back)
    end_time = os.date("!%Y-%m-%dT%H:%M:%S.000Z", time+300)
  end
  local uri = ""
  local output = ''
  local period = granularity * 60
  local namespace_table = default_cloudwatch_values[namespace]

  for index, metric in ipairs(metrics) do
    metric = string.gsub(metric, "^%s*(.-)%s*$", "%1")
    
    local algorithm="AWS4-HMAC-SHA256"
    
    local baseTable = { Action = 'GetMetricStatistics',
                        Period = period,
                        Version = version,
                        Timestamp = timestamp,
                        StartTime = start_time,
                        EndTime = end_time,
                        MetricName = metric,
                        Namespace = namespace }
  
    output = ''
    local stat_table = statistics
    if (get_default == 1) then
      stat_table = default_cloudwatch_values["default"]
      if (namespace_table ~= nil and namespace_table[metric] ~= nil) then
        stat_table = namespace_table[metric]
      end
    end
    for num, stat in ipairs(stat_table) do
      stat = string.gsub(stat, "^%s*(.-)%s*$", "%1")
      local ind = 'Statistics.member.' .. num
      baseTable[ind] = stat
    end
    for dim_num, dim_value in ipairs(self.dimensions) do
      local index = "Dimensions.member." .. dim_num .. "." .. "Name"
      baseTable[index] = dim_value["Name"]
      index = "Dimensions.member." .. dim_num .. "." .. "Value"
      baseTable[index] = dim_value["Value"]
    end
    
    --Begin AWS4 signature generation.
    uri = ""
    
    --Set canonical URI for AWS4 signature.  
    local canonical_uri = "/"
    
    --Set canonical querystring
    local canonical_querystring = self:constructBaseQuery(baseTable)
    
    --Set canonical headers
    local canonical_headers= "host:"..host.."\n".."x-amz-date:"..timestamp.."\n"
    
    --Set signed headers. This must match the variable names listed in the canonical headers.
    local signed_headers="host;x-amz-date"
    
    --Set payload. For GET requests, this is simply an empty string.
    local payload=""
    
    --Calculate payload hash
    local payload_hash=mtev.sha256_hash(payload)
    
    --Set canonical request.   
    local canonical_request="GET".."\n"..canonical_uri.."\n"..canonical_querystring.."\n"..canonical_headers.."\n"..signed_headers.."\n"..payload_hash
    
    --Set credential scope.
    local credential_scope=datestamp.."/"..region.."/"..service.."/".."aws4_request"
    
    --Set string to sign for signature generation
    local string_to_sign=algorithm.."\n"..timestamp.."\n"..credential_scope.."\n"..mtev.sha256_hash(canonical_request)
    
    --Set AWS4 secret key.
    local kSecret="AWS4"..api_secret

    --Generate AWS4 signature
    local dateStampDigest=mtev.base64_decode(mtev.hmac_sha256_encode(datestamp,kSecret))
    local regionDigest=mtev.base64_decode(mtev.hmac_sha256_encode(region,dateStampDigest))
    local serviceDigest=mtev.base64_decode(mtev.hmac_sha256_encode(service,regionDigest))
    local awsRequestDigest=mtev.base64_decode(mtev.hmac_sha256_encode("aws4_request",serviceDigest))
    local signatureDigest=mtev.base64_decode(mtev.hmac_sha256_encode(string_to_sign,awsRequestDigest))
    local signature=self:convert_to_hex(signatureDigest)
    
    --Append canonical query string to URI.
    uri = "/?"..canonical_querystring
    --End AWS4 signature generation
    
    -- callbacks from the HttpClient
    local callbacks = { }
    callbacks.consume = function (str) output = output .. str end
    local client = HttpClient:new(callbacks)
    local rv, err = client:connect(target, port, use_ssl)
    if rv ~= 0 then return rv, err end
    local headers = {}
    
    --Set x-amz-date and Authorization headers for AWS4 request
    local authorization_header = algorithm.." ".."Credential="..api_key.."/"..credential_scope..", ".."SignedHeaders=".. signed_headers..", ".."Signature="..signature
    headers["x-amz-date"]=timestamp
    headers["Authorization"]=authorization_header
    headers.Host = host
    
    --Make request
    client:do_request("GET", uri, headers)
    client:get_response()

    -- parse the xml doc
    local doc = mtev.parsexml(output)
    if doc ~= nil then
      local root = doc:root()
      if root:name() == "GetMetricStatisticsResponse" then
        local node = find_xml_node(root, "GetMetricStatisticsResult")
        if node ~= nil then
          local datapoints = find_xml_node(node, "Datapoints")
          local label = find_xml_node(node, "Label")
          --A sanity check - make sure that AWS is returning the metric
          --that we think it is.... otherwise, skip it
          if label ~= nil and label:contents() == metric then
            if datapoints ~= nil then
              --Sometimes, the most recent value returned will contain incomplete values.
              --We want to store next most recent value in case we want to use this.
              local most_recent = {timestamp = nil,
                                   value = nil }
              local next_most_recent = {timestamp = nil,
                                        value = nil }
              local count = 0
              for entry in datapoints:children() do
                if entry:name() == "member" then
                  local ts = find_xml_node(entry, "Timestamp")
                  local unit = find_xml_node(entry, "Unit")
                  if ts ~= nil and unit ~= nil then
                    local ts_value = ts:contents()
                    local unit_value = unit:contents()
                    count = count + 1
                    if most_recent["timestamp"] ~= nil and ts_value < most_recent["timestamp"] then
                      if next_most_recent["timestamp"] == nil or ts_value > next_most_recent["timestamp"] then
                        update_table(next_most_recent, entry, ts_value, unit_value, statistics, namespace_table, get_default, metric)
                      end
                    end
                    if most_recent["timestamp"] == nil or ts_value > most_recent["timestamp"] then
                      if most_recent["timestamp"] ~= nil and ts_value > most_recent["timestamp"] then
                        next_most_recent["timestamp"] = most_recent["timestamp"]
                        next_most_recent["unit"] = most_recent["unit"]
                        next_most_recent["results"] = {}
                        for key, value in pairs(most_recent["results"]) do
                          next_most_recent["results"][key] = value
                        end
                      end
                      update_table(most_recent, entry, ts_value, unit_value, statistics, namespace_table, get_default, metric)
                    end
                  end
                end
              end
              --We want the most recent if we only have one data point or if we're
              --billing
              if count == 1 or namespace == "AWS/Billing" then
                cache_table[metric] = most_recent
              elseif next_most_recent["timestamp"] ~= nil then
                cache_table[metric] = next_most_recent
              else
                -- We didn't get back a value for this metric, because AWS didn't have any recent values
                -- which then one can assume that means there were 0 request or whatever, so just mark it 0
                cache_table[metric] = { timestamp = os.time(), value = 0, results = { } }
                for k,v in pairs(statistics) do
                  cache_table[metric]["results"][v] = 0
                end
              end
            else
              -- if there was no datapoints section, I guess still set a value of 0
              cache_table[metric] = { timestamp = os.time(), value = 0, results = { } }
              for k,v in pairs(statistics) do
                cache_table[metric]["results"][v] = 0
              end
            end
          end
        end
      end
    end
  end
  return 0
end

return AWSClient
