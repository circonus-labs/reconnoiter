-- Copyright (c) 2013, OmniTI Computer Consulting, Inc.
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
  <name>monit</name>
  <description><para>The Monit module parses XML data from the Monit service and returns the values as metrics.</para>
  </description>
  <loader>lua</loader>
  <object>noit.module.monit</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="port"
               required="optional"
               allowed="\d+">The port on which the Monit server is running.</parameter>
    <parameter name="read_limit"
               required="optional"
               default="0"
               allowed="\d+">Sets an approximate limit on the data read (0 means no limit).</parameter>
    <parameter name="header_(\S+)"
               required="optional"
               allowed=".+">Allows the setting of arbitrary HTTP headers in the request.</parameter>
    <parameter name="auth_user"
               required="optional"
               allowed=".+">The Monit user</parameter>
    <parameter name="auth_password"
               required="optional"
               allowed=".+">The Monit password</parameter>
    <parameter name="ca_chain"
               required="optional"
               allowed=".+">A path to a file containing all the certificate authorities that should be loaded to validate the remote certificate (for SSL checks).</parameter>
    <parameter name="certificate_file"
               required="optional"
               allowed=".+">A path to a file containing the client certificate that will be presented to the remote server (for SSL checks).</parameter>
    <parameter name="key_file"
               required="optional"
               allowed=".+">A path to a file containing key to be used in conjunction with the cilent certificate (for SSL checks).</parameter>
    <parameter name="ciphers"
               required="optional"
               allowed=".+">A list of ciphers to be used in the SSL protocol (for SSL checks).</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking Monit services on localhost</title>
      <para>This example checks the Monit service on localhost.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="monit" object="noit.module.monit"/>
        </modules>
        <checks>
          <check target="127.0.0.1" module="monit" name="monit" uuid="a524f3fb-f60c-434f-98a4-060ab42e1b7d" period="10000" timeout="5000">
            <config>
              <url>http://127.0.0.1:2812/_status?format=xml</url>
              <port>2812</port>
              <auth_user>admin</auth_user>
              <auth_password>monit</auth_password>
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

local error_table = {}
local change_table = {}

--This module was written when Monit 5.6 was the most recent version...
--if the Monit XML Spec changes in the future, it may become necessessary
--to update this module to the latest version

function init(module)
    -- Initialize Error Tables
    error_table[0x00000001] = "Checksum Failed"
    error_table[0x00000002] = "Resource Limit Matched"
    error_table[0x00000004] = "Timeout"
    error_table[0x00000008] = "Timestamp Failed"
    error_table[0x00000010] = "Size Failed"
    error_table[0x00000020] = "Connection Failed"
    error_table[0x00000040] = "Permission Failed"
    error_table[0x00000080] = "UID Failed"
    error_table[0x00000100] = "GID Failed"
    error_table[0x00000200] = "Does Not Exist"
    error_table[0x00000400] = "Invalid Type"
    error_table[0x00000800] = "Data Access Error"
    error_table[0x00001000] = "Execution Failed"
    error_table[0x00002000] = "Filesystem Flags Failed"
    error_table[0x00004000] = "ICMP Failed"
    error_table[0x00008000] = "Content Failed"
    error_table[0x00010000] = "Monit Instance Failed"
    error_table[0x00020000] = "Action Done"
    error_table[0x00040000] = "PID Failed"
    error_table[0x00080000] = "PPID Failed"
    error_table[0x00100000] = "Heartbeat Failed"
    error_table[0x00200000] = "Status Failed"
    error_table[0x00400000] = "Uptime Failed"

    -- Initialize Change Table
    change_table[0x00000001] = "Checksum Changed"
    change_table[0x00000002] = "Resource Limit Changed"
    change_table[0x00000004] = "Timeout Changed"
    change_table[0x00000008] = "Timestamp Changed"
    change_table[0x00000010] = "Size Changed"
    change_table[0x00000020] = "Connection Changed"
    change_table[0x00000040] = "Permission Changed"
    change_table[0x00000080] = "UID Changed"
    change_table[0x00000100] = "GID Changed"
    change_table[0x00000200] = "Existence Changed"
    change_table[0x00000400] = "Type Changed"
    change_table[0x00000800] = "Data Access Changed"
    change_table[0x00001000] = "Execution Changed"
    change_table[0x00002000] = "Filesystem Flags Changed"
    change_table[0x00004000] = "ICMP Changed"
    change_table[0x00008000] = "Content Changed"
    change_table[0x00010000] = "Monit Instance Changed"
    change_table[0x00020000] = "Action Done"
    change_table[0x00040000] = "PID Changed"
    change_table[0x00080000] = "PPID Changed"
    change_table[0x00100000] = "Heartbeat Changed"
    change_table[0x00200000] = "Status Changed"
    change_table[0x00400000] = "Uptime Changed"
    return 0
end

function config(module, options)
    return 0
end

local HttpClient = require 'noit.HttpClient'

function set_string(check, doc, path, result, name)
    local obj = (doc:xpath(path, result))()
    if obj ~= nil then
        check.metric_string(name, obj and obj:contents())
        return true, obj:contents()
    end
    return false, nil
end

function set_float(check, doc, path, result, name)
    local obj = (doc:xpath(path, result))()
    local value

    if (obj == nil) then
        return false, nil
    end
    value = tonumber(obj:contents())
    if value == nil then
        return false, nil
    end
    check.metric_double(name, value)
    return true, value
end

function set_int(check, doc, path, result, name, signed)
    local obj = (doc:xpath(path, result))()
    local value

    if (obj == nil) then
        return false, nil
    end
    value = tonumber(obj:contents())
    if value == nil then
        return false, nil
    end
    if signed == true then
        check.metric_int64(name, value)
    else
        check.metric_uint64(name, value)
    end
    return true, value
end

function server_metrics(check, doc)
    local success

    local obj = (doc:xpath("/monit/server"))()
    if obj == nil then
        return false, nil
    end

    local version = obj:attr("version")
    local id = obj:attr("id")
    local incarnation = obj:attr("incarnation")

    if version == nil then
        success, version = set_string(check, doc, "version", obj, "server`version")
        if success == false then
            return false, version
        end
    else
        check.metric_string("server`version", version)
    end

    version = tonumber(version)
    if version == nil then
        -- We don't know what it is... assume 5.6
        version = 5.6
    end

    if id ==  nil then
        set_string(check, doc, "id", obj, "server`id")
    else
        check.metric_string("server`id", id)
    end

    if incarnation == nil then
        set_int(check, doc, "incarnation", obj, "server`incarnation", false)
    else
        incarnation = tonumber(incarnation)
        if incarnation ~= nil then
            check.metric_uint64("server`incarnation", incarnation)
        end
    end

    set_string(check, doc, "localhostname", obj, "server`localhostname")
    set_string(check, doc, "controlfile", nil, "server`controlfile")
    set_int(check, doc, "uptime", obj, "server`uptime", false)
    set_int(check, doc, "poll", obj, "server`poll", true)
    set_int(check, doc, "startdelay", obj, "server`startdelay", true)

    local node = (doc:xpath("httpd", obj))()
    if node ~= nil then
        set_string(check, doc, "address", node, "server`httpd`address")
        set_int(check, doc, "port", node, "server`httpd`port", true)
        set_int(check, doc, "ssl", node, "server`httpd`use_ssl", true)
    end

    return true, version
end

function platform_metrics(check, doc, version)
    local obj = (doc:xpath("/monit/platform"))()
    if obj == nil then
        return false
    end
    set_string(check, doc, "name", obj, "platform`name")
    set_string(check, doc, "release", obj, "platform`release")
    set_string(check, doc, "version", obj, "platform`version")
    set_string(check, doc, "machine", obj, "platform`machine")
    set_int(check, doc, "cpu", obj, "platform`cpu", true)
    set_int(check, doc, "memory", obj, "platform`memory", false)
    set_int(check, doc, "swap", obj, "platform`swap", false)
end

function extend_error_string(error_string, error_value, error_hint)
    local string
    if bit.band(error_hint, error_value) == error_value then
        string = change_table[error_value]
    else
        string = error_table[error_value]
    end
    if error_string == '' then
        error_string = string
    else
        error_string = error_string .. ", " .. string
    end
    return error_string
end

function set_error_string(check, doc, result, basename)
    local error_string = ''
    local count = 0
    local obj = (doc:xpath("status", result))()
    if obj == nil then
        return
    end
    local status_field = tonumber(obj:contents())
    if status_field == 0 then
        check.metric_string(basename, "No Errors")
        return
    end
    obj = (doc:xpath("status_hint", result))()
    local error_hint = 0
    if obj ~= nil then
        error_hint = tonumber(obj:contents())
    end
  
    if bit.band(status_field, 0x00000001) == 0x00000001 then
        error_string = extend_error_string(error_string, 0x1, error_hint)
    end
    if bit.band(status_field, 0x00000002) == 0x00000002 then
        error_string = extend_error_string(error_string, 0x2, error_hint)
    end
    if bit.band(status_field, 0x00000004) == 0x00000004 then
        error_string = extend_error_string(error_string, 0x4, error_hint)
    end
    if bit.band(status_field, 0x00000008) == 0x00000008 then
        error_string = extend_error_string(error_string, 0x8, error_hint)
    end
    if bit.band(status_field, 0x00000010) == 0x00000010 then
        error_string = extend_error_string(error_string, 0x10, error_hint)
    end
    if bit.band(status_field, 0x00000020) == 0x00000020 then
        error_string = extend_error_string(error_string, 0x20, error_hint)
    end
    if bit.band(status_field, 0x00000040) == 0x00000040 then
        error_string = extend_error_string(error_string, 0x40, error_hint)
    end
    if bit.band(status_field, 0x00000080) == 0x00000080 then
        error_string = extend_error_string(error_string, 0x80, error_hint)
    end
    if bit.band(status_field, 0x00000100) == 0x00000100 then
        error_string = extend_error_string(error_string, 0x100, error_hint)
    end
    if bit.band(status_field, 0x00000200) == 0x00000200 then
        error_string = extend_error_string(error_string, 0x200, error_hint)
    end
    if bit.band(status_field, 0x00000400) == 0x00000400 then
        error_string = extend_error_string(error_string, 0x400, error_hint)
    end
    if bit.band(status_field, 0x00000800) == 0x00000800 then
        error_string = extend_error_string(error_string, 0x800, error_hint)
    end
    if bit.band(status_field, 0x00001000) == 0x00001000 then
        error_string = extend_error_string(error_string, 0x1000, error_hint)
    end
    if bit.band(status_field, 0x00002000) == 0x00002000 then
        error_string = extend_error_string(error_string, 0x2000, error_hint)
    end
    if bit.band(status_field, 0x00004000) == 0x00004000 then
        error_string = extend_error_string(error_string, 0x4000, error_hint)
    end
    if bit.band(status_field, 0x00008000) == 0x00008000 then
        error_string = extend_error_string(error_string, 0x8000, error_hint)
    end
    if bit.band(status_field, 0x00010000) == 0x00010000 then
        error_string = extend_error_string(error_string, 0x10000, error_hint)
    end
    if bit.band(status_field, 0x00020000) == 0x00020000 then
        error_string = extend_error_string(error_string, 0x20000, error_hint)
    end
    if bit.band(status_field, 0x00040000) == 0x00040000 then
        error_string = extend_error_string(error_string, 0x40000, error_hint)
    end
    if bit.band(status_field, 0x00080000) == 0x00080000 then
        error_string = extend_error_string(error_string, 0x80000, error_hint)
    end
    if bit.band(status_field, 0x00100000) == 0x00100000 then
        error_string = extend_error_string(error_string, 0x100000, error_hint)
    end
    if bit.band(status_field, 0x00200000) == 0x00200000 then
        error_string = extend_error_string(error_string, 0x200000, error_hint)
    end
    if bit.band(status_field, 0x00400000) == 0x00400000 then
        error_string = extend_error_string(error_string, 0x400000, error_hint)
    end
    check.metric_string(basename, error_string)
end

function set_pending_action(check, doc, result, basename)
    local obj = (doc:xpath("pendingaction", result))()
    if obj == nil then
        return -1
    end
    local action = tonumber(obj:contents())
    if action == 0 then
        check.metric_string(basename, "None")
    elseif action == 1 then
        check.metric_string(basename, "Alert")
    elseif action == 2 then
        check.metric_strng(basename, "Restart")
    elseif action == 3 then
        check.metric_string(basename, "Stop")
    elseif action == 4 then
        check.metric_string(basename, "Exec")
    elseif action == 5 then
        check.metric_string(basename, "Unmonitor")
    elseif action == 6 then
        check.metric_string(basename, "Start")
    elseif action == 7 then
        check.metric_string(basename, "Monitor")
    end
    return action
end

function set_monitor(check, doc, result, basename)
    local obj = (doc:xpath("monitor", result))()
    if obj == nil then
        return -1
    end
    local action = tonumber(obj:contents())
    if action == 0x0 then
        check.metric_string(basename, "Not Monitoring")
    elseif action == 0x1 then
        check.metric_string(basename, "Monitoring")
    elseif action == 0x2 then
        check.metric_string(basename, "Initializing Monitoring")
    elseif action == 0x4 then
        check.metric_string(basename, "Monitoring Waiting")
    end
    return action
end

function set_monitor_mode(check, doc, result, basename)
    local obj = (doc:xpath("monitormode", result))()
    if obj == nil then
        return -1
    end
    local action = tonumber(obj:contents())
    if action == 0 then
        check.metric_string(basename, "Active")
    elseif action == 1 then
        check.metric_string(basename, "Passive")
    elseif action == 2 then
        check.metric_string(basename, "Manual")
    end
    return action
end

function set_ports(check, doc, result, basename)
    local i = 0
    for node in doc:xpath("port", result) do
        set_int(check, doc, "portnumber", node, basename .. "`port`" .. i .. "`port")
        set_string(check, doc, "hostname", node, basename .. "`port`" .. i .. "`hostname")
        set_string(check, doc, "request", node, basename .. "`port`" .. i .. "`request")
        set_string(check, doc, "protocol", node, basename .. "`port`" .. i .. "`protocol")
        set_string(check, doc, "type", node, basename .. "`port`" .. i .. "`type")
        set_float(check, doc, "responsetime", node, basename .. "`port`" .. i .. "`response_time")
        i = i + 1
    end
    i = 0
    for node in doc:xpath("unix", result) do
        set_string(check, doc, "path", node, basename .. "`unix`" .. i .. "`path")
        set_string(check, doc, "protocol", node, basename .. "`unix`" .. i .. "`protocol")
        set_string(check, doc, "hostname", node, basename .. "`unix`" .. i .. "`hostname")
    end
end

function set_icmp(check, doc, result, basename)
    local i = 0
    for node in doc:xpath("icmp", result) do
        set_string(check, doc, "type", node, basename .. "`icmp`" .. i .. "`type")
        set_float(check, doc, "responsetime", node, basename .. "`icmp`" .. i .. "`response_time")
        i = i + 1
    end
end

function set_generic_service_metrics(check, doc, result, basename)
    set_int(check, doc, "collected_sec", result, basename .. "`last_collect_time", false)
    set_error_string(check, doc, result, basename .. "`status")
    set_pending_action(check, doc, result, basename .. "`pending_action")
    set_monitor(check, doc, result, basename .. "`monitor")
    set_monitor_mode(check, doc, result, basename .. "`monitor_mode")
end

function process_filesystem(check, doc, version, result, basename)
    set_generic_service_metrics(check, doc, result, basename)
    set_string(check, doc, "mode", result, basename .. "`mode")
    set_string(check, doc, "uid", result, basename .. "`uid")
    set_string(check, doc, "gid", result, basename .. "`gid")
    set_string(check, doc, "flags", result, basename .. "`flags")
    local node = (doc:xpath("block", result))()
    if node ~= nil then
        set_float(check, doc, "percent", node, basename .. "`block`percent")
        if version <= 5.3 then
            local usage_obj = (doc:xpath("usage", node))()
            if usage_obj ~= nil then
                local usage_str = usage_obj:contents()
                -- Remove "MB" from the end
                usage_str = string.sub(usage_str, 0, -4)
                local usage_num = tonumber(usage_str)
                if usage_num ~= nil then
                    check.metric_int64(basename .. "`block`usage", usage_num)
                end
            end
            local total_obj = (doc:xpath("total", node))()
            if total_obj ~= nil then
                local total_str = total_obj:contents()
                -- Remove "MB" from the end
                total_str = string.sub(total_str, 0, -4)
                local total_num = tonumber(total_str)
                if total_num ~= nil then
                    check.metric_int64(basename .. "`block`total", total_num)
                end
            end
        else
            set_int(check, doc, "usage", node, basename .. "`block`usage", true)
            set_int(check, doc, "total", node, basename .. "`block`total", true)
        end
    end
    node = (doc:xpath("inode", result))()
    if node ~= nil then
        set_float(check, doc, "percent", node, basename .. "`inode`percent")
        set_int(check, doc, "usage", node, basename .. "`inode`usage", true)
        set_int(check, doc, "total", node, basename .. "`inode`total", true)
    end
end

function process_directory(check, doc, result, basename)
    set_generic_service_metrics(check, doc, result, basename)
    set_string(check, doc, "mode", result, basename .. "`mode")
    set_string(check, doc, "uid", result, basename .. "`uid")
    set_string(check, doc, "gid", result, basename .. "`gid")
    set_int(check, doc, "timestamp", result, basename .. "`timestamp", false)
end

function process_file(check, doc, result, basename)
    set_generic_service_metrics(check, doc, result, basename)
    set_string(check, doc, "mode", result, basename .. "`mode")
    set_string(check, doc, "uid", result, basename .. "`uid")
    set_string(check, doc, "gid", result, basename .. "`gid")
    set_int(check, doc, "timestamp", result, basename .. "`timestamp", false)
    set_int(check, doc, "size", result, basename .. "`size", false)
    local obj = (doc:xpath("checksum", result))()
    if obj ~= nil then
        local checksum_type = obj:attr("type")
        local checksum_value = obj:contents()
        if checksum_type ~= nil then
            check.metric_string(basename .. "`checksum`type", checksum_type)
        end
        if checksum_value ~= nil then
            check.metric_string(basename .. "`checksum", checksum_value)
        end
    end
end

function process_process(check, doc, result, basename)
    set_generic_service_metrics(check, doc, result, basename)
    set_int(check, doc, "pid", result, basename .. "`pid", true)
    set_int(check, doc, "ppid", result, basename .. "`ppid", true)
    set_int(check, doc, "uptime", result, basename .. "`uptime", true)
    set_int(check, doc, "children", result, basename .. "`children", true)
    local node = (doc:xpath("memory", result))()
    if node ~= nil then
        set_float(check, doc, "percent", node, basename .. "`memory`percent")
        set_float(check, doc, "percenttotal", node, basename .. "`memory`percent_total")
        set_int(check, doc, "kilobyte", node, basename .. "`memory`kilobytes", true)
        set_int(check, doc, "kilobytetotal", node, basename .. "`memory`kilobytes_total", true)
    end
    node = (doc:xpath("cpu", result))()
    if node ~= nil then
        set_float(check, doc, "percent", node, basename .. "`cpu`percent")
        set_float(check, doc, "percenttotal", node, basename .. "`cpu`percent_total")
    end
    set_ports(check, doc, result, basename)
end

function process_host(check, doc, result, basename)
    set_generic_service_metrics(check, doc, result, basename)
    set_icmp(check, doc, results, basename)
    set_ports(check, doc, result, basename)
end

function process_system(check, doc, result, basename)
    set_generic_service_metrics(check, doc, result, basename)
    local node = (doc:xpath("system", result))()
    if node ~= nil then
        local load = (doc:xpath("load", node))()
        if load ~= nil then
            set_float(check, doc, "avg01", load, basename .. "`load_average`01m")
            set_float(check, doc, "avg05", load, basename .. "`load_average`05m")
            set_float(check, doc, "avg15", load, basename .. "`load_average`15m")
        end
        local cpu = (doc:xpath("cpu", node))()
        if cpu ~= nil then
            set_float(check, doc, "user", cpu, basename .. "`cpu`percent_user")
            set_float(check, doc, "system", cpu, basename .. "`cpu`percent_system")
            set_float(check, doc, "wait", cpu, basename .. "`cpu`percent_wait")
        end
        local memory = (doc:xpath("memory", node))()
        if memory ~= nil then
            set_float(check, doc, "percent", memory, basename .. "`memory`percent")
            set_int(check, doc, "kilobyte", memory, basename .. "`memory`kilobytes", true)
        end
        local swap = (doc:xpath("swap", node))()
        if swap ~= nil then
            set_float(check, doc, "percent", swap, basename .. "`swap`percent")
            set_int(check, doc, "kilobyte", swap, basename .. "`swap`kilobytes", true)
        end
    end
end

function process_fifo(check, doc, result, basename)
    set_generic_service_metrics(check, doc, result, basename)
    set_string(check, doc, "mode", result, basename .. "`mode")
    set_string(check, doc, "uid", result, basename .. "`uid")
    set_string(check, doc, "gid", result, basename .. "`gid")
    set_int(check, doc, "timestamp", result, basename .. "`timestamp", false)
end

function process_program(check, doc, result, basename)
    set_generic_service_metrics(check, doc, result, basename)
    local node = (doc:xpath("program", result))()
    if node ~= nil then
        set_int(check, doc, "started", node, basename .. "`started", false)
        set_int(check, doc, "status", node, basename .. "`exit_status", true)
    end
end

function service_metrics(check, doc, version)
    local services = 0
    local status_string = ""
    local parse_string = ""
    local test = (doc:xpath("/monit/services"))()
    if test == nil then
        parse_string = "/monit/service"
    else
        parse_string = "/monit/services/service"
    end
    for result in doc:xpath(parse_string) do
        local type = result:attr("type")
        local name = result:attr("name")
        if name == nil then
            local obj = (doc:xpath("name", result))()
            if obj ~= nil then
                name = obj:contents()
            end
        end
        if type == nil then
            local obj = (doc:xpath("type", result))()
            if obj ~= nil then
                type = obj:contents()
            end
        end

        if type ~= nil and name ~= nil then
            if type == "0" then
                process_filesystem(check, doc, version, result, "service`filesystem`" .. name)
                services = services + 1
            elseif type == "1" then
                process_directory(check, doc, result, "service`directory`" .. name)
                services = services + 1
            elseif type == "2" then
                process_file(check, doc, result, "service`file`" .. name)
                services = services + 1
            elseif type == "3" then
                process_process(check, doc, result, "service`process`" .. name)
                services = services + 1
            elseif type == "4" then
                process_host(check, doc, result, "service`host`" .. name)
                services = services + 1
            elseif type == "5" then
                process_system(check, doc, result, "service`system`" .. name)
                services = services + 1
            elseif type == "6" then
                process_fifo(check, doc, result, "service`fifo`" .. name)
                services = services + 1
            elseif type == "7" then
                process_program(check, doc, result, "service`program`" .. name)
                services = services + 1
            end
        end
    end
    return services
end

function servicegroup_metrics(check, doc, version)
    for result in doc:xpath("/monit/servicegroups/servicegroup") do
        local name = result:attr("name")
        if name ~= nil then
            local metric_string = ""
            for node in doc:xpath("service", result) do
                local service = obj:contents()
                if service ~= nil then
                    if metric_string == "" then
                        metric_string = service
                    else
                        metric_string = metric_string .. ", " .. service
                    end
                end
            end
        end
    end
end

function xml_to_metrics(check, doc)
    local services = 0
    local status = ''
    local valid
    local version

    check.available()

    valid, version = server_metrics(check, doc)
    if valid == true then
        platform_metrics(check, doc, version)
        services = service_metrics(check, doc, version)
        servicegroup_metrics(check, doc, version)
        check.metric_uint32("services", services)
        check.good()
    else
        check.bad()
    end
    return services
end

function initiate(module, check)
    local config = check.interpolate(check.config)
    local url = config.url or 'http:///'
    local schema, host, uri = string.match(url, "^(https?)://([^/]*)(.*)$");
    local port
    local use_ssl = false
    local codere = noit.pcre(config.code or '^200$')
    local good = false
    local starttime = noit.timeval.now()
    local read_limit = tonumber(config.read_limit) or nil

    -- assume the worst.
    check.bad()
    check.unavailable()

    if host == nil then host = check.target end
    if schema == nil then
        schema = 'http'
    end
    if uri == nil or uri == '' then
        uri = '/'
    end
    if schema == 'http' then
        port = config.port or 81
    elseif schema == 'https' then
        port = config.port or 443
        use_ssl = true
    else
        error(schema .. " not supported")
    end 

    local expected_certificate_name = host or ''
    local output = ''

    -- callbacks from the HttpClient
    local callbacks = { }
    local hdrs_in = { }
    callbacks.consume = function (str) output = output .. str end
    callbacks.headers = function (t) hdrs_in = t end
    
    -- setup SSL info
    local default_ca_chain =
        noit.conf_get_string("/noit/eventer/config/default_ca_chain")
    callbacks.certfile = function () return config.certificate_file end
    callbacks.keyfile = function () return config.key_file end
    callbacks.cachain = function ()
        return config.ca_chain and config.ca_chain
                                      or default_ca_chain
    end
    callbacks.ciphers = function () return config.ciphers end
   
    -- perform the request
    local headers = {}
    headers.Host = host
    for header, value in pairs(config) do
        hdr = string.match(header, '^header_(.+)$')
        if hdr ~= nil then
            headers[hdr] = value
        end
    end

    if config.auth_method == "Basic" or
        (config.auth_method == nil and
            config.auth_user ~= nil and
            config.auth_password ~= nil) then
        local user = config.auth_user or nil
        local pass = config.auth_password or nil
        local encoded = nil
        if (user ~= nil and pass ~= nil) then
            encoded = noit.base64_encode(user .. ':' .. pass)
            headers["Authorization"] = "Basic " .. encoded
        end
    elseif config.auth_method == "Digest" or
           config.auth_method == "Auto" then

        -- this is handled later as we need our challenge.
        local client = HttpClient:new()
        local rv, err = client:connect(check.target_ip, port, use_ssl)
        if rv ~= 0 then
            check.status(str or "unknown error")
            return
        end
        local headers_firstpass = {}
        for k,v in pairs(headers) do
            headers_firstpass[k] = v
        end
        client:do_request("GET", uri, headers_firstpass)
        client:get_response(read_limit)
        if client.code ~= 401 or
           client.headers["www-authenticate"] == nil then
            check.status("expected digest challenge, got " .. client.code)
            return
        end
        local user = config.auth_user or ''
        local password = config.auth_password or ''
        local ameth, challenge =
            string.match(client.headers["www-authenticate"], '^(%S+)%s+(.+)$')
        if config.auth_method == "Auto" and ameth == "Basic" then
            local encoded = noit.base64_encode(user .. ':' .. password)
            headers["Authorization"] = "Basic " .. encoded
        elseif ameth == "Digest" then
            headers["Authorization"] =
                "Digest " .. client:auth_digest("GET", uri,
                                         user, password, challenge)
        else
            check.status("Unexpected auth '" .. ameth .. "' in challenge")
            return
        end
    elseif config.auth_method ~= nil then
        check.status("Unknown auth method: " .. config.auth_method)
        return
    end

    local client = HttpClient:new(callbacks)
    local rv, err = client:connect(check.target_ip, port, use_ssl)
    if rv ~= 0 then
        check.status(err or "unknown error")
        return
    end

    client:do_request("GET", uri, headers)
    client:get_response(read_limit)

    -- parse the xml doc
    local doc = noit.parsexml(output)
    if doc == nil then
        check.status("xml parse error")
        return
    end
    local services = xml_to_metrics(check, doc)
    local status = "services=" .. services
    local good = true
    -- ssl ctx
    local ssl_ctx = client:ssl_ctx()
    if ssl_ctx ~= nil then
        local header_match_error = nil
        if expected_certificate_name ~= '' then
            header_match_error = noit.extras.check_host_header_against_certificate(expected_certificate_name, ssl_ctx.subject, ssl_ctx.san_list)
        end
        if ssl_ctx.error ~= nil then status = status .. ',sslerror' end
        if header_match_error == nil then
            check.metric_string("cert_error", ssl_ctx.error)
        elseif ssl_ctx.error == nil then
            check.metric_string("cert_error", header_match_error)
        else
            check.metric_string("cert_error", ssl_ctx.error .. ', ' .. header_match_error)
        end
        check.metric_string("cert_issuer", ssl_ctx.issuer)
        check.metric_string("cert_subject", ssl_ctx.subject)
        if ssl_ctx.san_list ~= nil then
            check.metric_string("cert_subject_alternative_names", ssl_ctx.san_list)
        end
        check.metric_uint32("cert_start", ssl_ctx.start_time)
        check.metric_uint32("cert_end", ssl_ctx.end_time)
        check.metric_int32("cert_end_in", ssl_ctx.end_time - os.time())
        if noit.timeval.seconds(starttime) > ssl_ctx.end_time then
            good = false
            status = status .. ',ssl=expired'
        end
    end
    check.status(status)
end

