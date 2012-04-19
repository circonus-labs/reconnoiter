-- Copyright (c) 2009, OmniTI Computer Consulting, Inc.
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
--     * Neither the name OmniTI Computer Consulting, Inc. nor the names
--       of its contributors may be used to endorse or promote products
--       derived from this software without specific prior written
--       permission.
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
  <name>redis</name>
  <description><para>Redis check.</para></description>
  <loader>lua</loader>
  <object>noit.module.redis</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port" required="required" default="6379" allowed="\d+">
        Specifies the port on which redis is running.
    </parameter>
    <parameter name="command" required="required" default="INFO" allowed=".+">
            Command to send to redis server.
    </parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking Redis</title>
      <para>This example checks Redis by issuing the INFO command</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="redis" object="noit.module.redis" />
        </modules>
        <checks>
          <check uuid="052852f2-fd09-4751-8889-a313a70c3c9c" module="redis" target="127.0.0.1" />
        </checks>
      </noit>
    ]]></programlisting>
    </example>
  </examples>

</module>
]=]);
  return 0
end

function init(module)
  return 0
end

function config(module, options)
  return 0
end

function initiate(module, check)
  local host = check.config.host or check.target_ip or check.target
  local port = check.config.port or 6379

  -- default to bad
  check.bad();
  check.unavailable();

  local redis_comm = build_redis_command(check.config.command or "info")

  local conn = noit.socket(host)
  local rv, err = conn:connect(host, port)
  if ( rv ~= 0 ) then
    check.status(err or "connect error")
    return
  end

  conn:write(redis_comm)
  local metric_count = 0

  if ( check.config.command ~= nil and check.config.command:upper() ~= "INFO" ) then
    metric_count = get_command_metrics(conn, check)
  else
    metric_count = get_info_metrics(conn, check)
  end

  if ( metric_count > 0 ) then
      check.status(string.format("%d stats", metric_count))
      check.available()
      check.good()
  end
end

function build_redis_command(command)
  local redis_comm = "*"
  local comm_list = string.split(command, "%s+")

  redis_comm = redis_comm .. table.getn(comm_list) .. "\r\n"

  for c in ipairs(comm_list) do
    redis_comm = redis_comm .. "$" .. comm_list[c]:len() .. "\r\n" .. comm_list[c] .. "\r\n"
  end

  return redis_comm
end

function get_info_metrics(conn, check)
  local count = 0
  local redis_result
  local result_len = conn:read("\r\n")
  local metric_data = {}
  metric_data["metric_name"] = nil
  result_len = string.sub(result_len, 2, -2)
  redis_result = conn:read(result_len)

  local list = string.split(redis_result, "\r\n")

  for line in pairs(list) do
    if ( list[line] == nil) then
      break
    end
    if ( list[line] ~= "" and string.sub(list[line], 1, 1) ~= "#" ) then
      kv = string.split(list[line], ":")

      -- see if this is db* data
      if ( string.find(kv[1], "^db%d+$") ) then
        db_metrics = string.split(kv[2], ",")
        for idx in pairs(db_metrics) do
          count = count + 1
          met = string.split(db_metrics[idx], "=")
          metric_data["metric_name"] = kv[1] .. "`" .. met[1]
          add_check_metric(met[2], check, metric_data)
        end
      elseif ( string.find(kv[1], "^allocation_stats$") ) then
        alloc_metrics = string.split(kv[2], ",")
        for idx in pairs(alloc_metrics) do
          count = count + 1
          met = string.split(alloc_metrics[idx], "=")
          if ( 3 == table.getn(met) ) then
              check.metric_int32("allocation_stats`" .. met[2], met[3])
          else
              check.metric_int32("allocation_stats`" .. met[1], met[2])
          end
        end
      else
        count = count + 1
        metric_data["metric_name"] = kv[1]
        add_check_metric(kv[2], check, metric_data)
      end
    end
  end

  return count
end

function get_command_metrics(conn, check)
  -- the only metric name we know is what we are sending to redis
  local metric_data = {}
  metric_data["hash_key"] = nil
  metric_data["metric_name"] = nil
  metric_data["need_key"] = 0
  metric_data["names"] = {}

  local cs = string.split(check.config.command, "%s+")
  if ( string.find(cs[1]:upper(), "HGET") ) then
    metric_data["hash_key"] = cs[2]
  elseif ( string.find(cs[1]:upper(), "MGET") ) then
    for idx in pairs(cs) do
      if ( idx > 1 ) then
        if ( string.find(cs[1]:upper(), "HMGET") and idx > 2 ) then
          metric_data["metric_names"][idx-3] = cs[2] .. '`' .. cs[idx]
        else
          metric_data["metric_names"][idx-2] = cs[idx]
        end
      end
    end
  end
  metric_data["metric_name"] = cs[2]
  return read_redis_response(conn, check, metric_data)
end

function read_redis_response(conn, check, metric_data)
  local response_type = conn:read(1);
  if ( response_type == "+" ) then
    return redis_response_string(conn, check, metric_data)
  elseif ( response_type == "-" ) then
    return redis_response_error(conn, check, metric_data)
  elseif ( response_type == ":" ) then
    return redis_response_integer(conn, check, metric_data)
  elseif ( response_type == "$" ) then
    return redis_bulk_response(conn, check, metric_data)
  elseif ( response_type == "*" ) then
    return redis_multibulk_response(conn, check, metric_data)
  end
end

function redis_bulk_response(conn, check, metric_data)
  local response
  local response_len = conn:read("\r\n")
  response_len = string.sub(response_len, 1, -2)
  if ( -1 == tonumber(response_len) ) then
    return redis_response_null(check)
  else
    response = conn:read(response_len)
    if ( 1 == metric_data["need_key"] ) then
      metric_data["metric_name"] = metric_data["hash_key"] .. '`' .. response
    else
      add_check_metric(response, check, metric_data)
    end
    -- clean out rest of response
    conn:read("\r\n")
    return 1
  end
end

function redis_multibulk_response(conn, check, metric_data)
  local responses = conn:read("\r\n")
  responses = string.sub(responses, 1, -2)

  for i = 1, responses, 1 do
    if ( metric_data["hash_key"] ~= nil ) then
      metric_data["need_key"] = (metric_data["need_key"] + 1) % 2
    else
      metric_data["metric_name"] = metric_data["metric_name"] .. '`' .. (i-1)
      if ( metric_data["metric_names"][i-1] ) then
        metric_data["metric_name"] = metric_data["metric_names"][i-1]
      end
    end
    read_redis_response(conn, check, metric_data)
  end
  return tonumber(responses)
end

function redis_response_integer(conn, check, metric_data)
  local response = conn:read("\r\n")
  response = string.sub(response, 1, -2)
  add_check_metric(response, check, metric_data)
  return 1
end

function redis_response_string(conn, check, metric_data)
  local response = conn:read("\r\n")
  response = string.sub(response, 1, -2)
  add_check_metric(response, check, metric_data)
  return 1
end

function redis_response_error(conn, check)
  local response = conn:read("\r\n")
  response = string.sub(response, 1, -2)
  check.status(response)
  check.bad()
  return 0
end

function redis_response_null(check, metric_data)
  check.metric_string(metric_data["metric_name"], nil)
  return 1
end

function add_check_metric(value, check, metric_data)
  if ( string.find(value, "^%d+$") ) then
    check.metric_uint64(metric_data["metric_name"], value)
  elseif ( string.find(value, "^%d+?.%d+$") ) then
    check.metric_double(metric_data["metric_name"], value)
  else
    check.metric(metric_data["metric_name"], value)
  end
end

-- from http://www.wellho.net/resources/ex.php4?item=u108/split
function string:split(delimiter)
  local result = { }
  local from  = 1
  local delim_from, delim_to = string.find( self, delimiter, from  )
  while delim_from do
    table.insert( result, string.sub( self, from , delim_from-1 ) )
    from  = delim_to + 1
    delim_from, delim_to = string.find( self, delimiter, from  )
  end
  table.insert( result, string.sub( self, from  ) )
  return result
end
