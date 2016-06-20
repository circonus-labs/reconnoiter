-- Copyright (c) 2010-2015, Circonus, Inc.
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

local health_state_values = {}
local operational_status_values = {}
local enabled_state_values = {}
local enabled_default_values = {}
local requested_state_values = {}
local usage_restriction_values = {}
local extant_status_values = {}
local dedicated_values = {}
local upgrade_method_values = {}

function onload(image)
  image.xml_description([=[
<module>
  <name>cim</name>
  <description><para>The CIM module checks a CIM system using the CIS XML protocol</para></description>
  <loader>lua</loader>
  <object>noit.module.cim</object>
  <checkconfig>
    <parameter name="url"
               required="required"
               allowed=".+">The URL including schema and hostname (as you would type into a browser's location bar).</parameter>
    <parameter name="port"
               required="optional"
               default="5988"
               allowed="\d+">Specifies the port on which the CIM interface can be reached.</parameter>
    <parameter name="namespace"
               required="optional"
               default="root/cimv2"
               allowed=".*">The namespace to use to connect to the CIM interface</parameter>
    <parameter name="auth_user"
               required="optional"
               allowed="[^:]*">The user to authenticate as.</parameter>
    <parameter name="auth_password"
               required="optional"
               allowed=".*">The password to use during authentication.</parameter>
    <parameter name="classes"
               required="optional"
               default="OMC_SMASHFirmwareIdentity,CIM_Chassis,CIM_Card,CIM_ComputerSystem,CIM_NumericSensor,CIM_Memory,CIM_Processor,CIM_RecordLog,OMC_DiscreteSensor,OMC_Fan,OMC_PowerSupply,VMware_StorageExtent,VMware_Controller,VMware_StorageVolume,VMware_Battery,VMware_SASSATAPort"
               allowed=".+">Comma-separated list of classes to pull information from the CIM server about</parameter>
    <parameter name="fields"
               required="optional"
               default="ALL"
               allowed=".+">Comma-separated list of fields to pull from CIM classes. A value of 'ALL' will pull all fields.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking CIM services at a website</title>
      <para>This example checks CIM service on a random IP address</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="cim" object="noit.module.cim"/>
        </modules>
        <checks>
          <labs target="8.8.38.5" module="cim" period="10000" timeout="5000">
            <check uuid="36b8ba72-7968-11dd-a67f-d39a2cc3f9de" name="cim">
              <config>
                <url>https://www.randomtestwebsite.com</url>
                <port>5989</port>
                <auth_username>test</auth_username>
                <auth_password>test_pw</auth_password>
              </config>
            </check>
          </labs>
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

local HttpClient = require 'mtev.HttpClient'

function set_check_metric(check, name, type, value)
    if string.find(value, ",") ~= nil then
        check.metric_string(name, value)
    elseif type == 'uint8' then
        check.metric_uint32(name, value)
    elseif type == 'sint8' then
        check.metric_int32(name, value)
    elseif type == 'uint16' then
        check.metric_uint32(name, value)
    elseif type == 'sint16' then
        check.metric_int32(name, value)
    elseif type == 'uint32' then
        check.metric_uint32(name, value)
    elseif type == 'sint32' then
        check.metric_int32(name, value)
    elseif type == 'uint64' then
        check.metric_uint64(name, value)
    elseif type == 'sint64' then
        check.metric_int64(name, value)
    elseif type == 'string' then
        check.metric_string(name, value)
    elseif type == 'boolean' then
        check.metric_string(name, value)
    elseif type == 'datetime' then
        check.metric_string(name, value)
    else
        check.metric(name, value)
    end
end

function classname_xml_to_metrics(check, doc)
    local metrics = 0
    local result
    for result in doc:xpath("/CIM/MESSAGE/SIMPLERSP/IMETHODRESPONSE/IRETURNVALUE/CLASSNAME") do
      local name = result:attr("NAME") or "unknown"
      if name ~= "unknown" then
        check.metric_string(name, 'AVAILABLE')
        metrics = metrics + 1
      end
    end
    return metrics
end

function xml_to_metrics(check, doc, classname, fields_to_check)
    local metrics = 0
    local result
    for result in doc:xpath("/CIM/MESSAGE/SIMPLERSP/IMETHODRESPONSE/IRETURNVALUE/VALUE.NAMEDINSTANCE/INSTANCE") do
      local value_array = { }
      for property_array in doc:xpath("PROPERTY.ARRAY", result) do
        local name = property_array:attr("NAME") or "unknown"
        local type = property_array:attr("TYPE") or "string"
        local valuearray_vals = ''
        for valuearray in doc:xpath("VALUE.ARRAY", property_array) do
          for value in doc:xpath("VALUE", valuearray) do
            valuearray_vals = valuearray_vals .. value:contents() .. ", "
          end
        end
        if valuearray_vals ~= '' then
          valuearray_vals = string.sub(valuearray_vals, 1, -3)
          if value_array[name] == nil then
            local result_struct = { }
            result_struct["value"] = valuearray_vals
            result_struct["type"] = type
            value_array[name] = result_struct
          end
        end
      end
      for property in doc:xpath("PROPERTY", result) do
        local name = property:attr("NAME") or "BAD"
        local type = property:attr("TYPE") or "string"
        for value in doc:xpath("VALUE", property) do
          if value_array[name] == nil then
            result_struct = { }
            result_struct["value"] = value:contents()
            result_struct["type"] = type
            value_array[name] = result_struct
          end
        end
      end
      if value_array["ElementName"] ~= nil then
        if fields_to_check[1] == 'ALL' then
          for val, blarg in pairs(value_array) do
            metrics = metrics + create_metric(check, classname, value_array, val)
          end
        else
          for ind, val in ipairs(fields_to_check) do
            metrics = metrics + create_metric(check, classname, value_array, val)
          end
        end
      end
    end
    return metrics
end

function set_value_string(check, classname, value_array, val, string_array)
  local string_val = string_array[math.floor(value_array[val]["value"])] or 'Unknown'
  set_check_metric(check, classname .. '`' .. value_array["ElementName"]["value"] .. '`' .. val .. 'String',
    'string', string_val)
  return 1
end

function create_metric(check, classname, value_array, val)
  local metrics = 0
  if val ~= 'ElementName' and value_array[val] ~= nil then
    metrics = metrics + 1
    set_check_metric(check, classname .. '`' .. value_array["ElementName"]["value"] .. '`' .. val, 
      value_array[val]["type"], value_array[val]["value"])
    if val == 'HealthState' then
      metrics = metrics + set_value_string(check, classname, value_array, val, health_state_values)
    elseif val == 'OperationalStatus' then
      metrics = metrics + set_value_string(check, classname, value_array, val, operational_status_values)
    elseif val == 'EnabledDefault' then
      metrics = metrics + set_value_string(check, classname, value_array, val, enabled_default_values)
    elseif val == 'EnabledState' then
      metrics = metrics + set_value_string(check, classname, value_array, val, enabled_state_values)
    elseif val == 'RequestedState' or val == 'TransitioningToState' then
      metrics = metrics + set_value_string(check, classname, value_array, val, requested_state_values)
    elseif val == 'UsageRestriction' then
      metrics = metrics + set_value_string(check, classname, value_array, val, usage_restriction_values)
    elseif val == 'ExtentStatus' then
      metrics = metrics + set_value_string(check, classname, value_array, val, extent_status_values)
    elseif val == 'Dedicated' then
      metrics = metrics + set_value_string(check, classname, value_array, val, dedicated_values)
    elseif val == 'UpgradeMethod' then
      metrics = metrics + set_value_string(check, classname, value_array, val, upgrade_method_values)
    end
  end
  return metrics
end

function get_simple_req(class, namespace_array)
  local message = ''
  message = message .. '<SIMPLEREQ>\n'
  message = message .. '<IMETHODCALL NAME="EnumerateInstances">\n'
  message = message .. '<LOCALNAMESPACEPATH>\n'
  for i,name in ipairs(namespace_array) do
    if name ~= nil and name ~= '' then
      message = message .. '<NAMESPACE NAME="' .. name .. '"/>\n'
    end
  end
  message = message .. '</LOCALNAMESPACEPATH>\n'
  message = message .. '<IPARAMVALUE NAME="ClassName">\n'
  message = message .. '<CLASSNAME NAME="' .. class .. '"/>\n'
  message = message .. '</IPARAMVALUE>\n'
  message = message .. '</IMETHODCALL>\n'
  message = message .. '</SIMPLEREQ>\n'
  return message
end

function assemble_class_list_xml_message(namespace_array)
  local message = ''
  message = message .. '<?xml version="1.0" encoding="utf-8" ?>\n'
  message = message .. '<CIM CIMVERSION="2.0" DTDVERSION="2.0">\n'
  message = message .. '<MESSAGE ID="1" PROTOCOLVERSION="1.0">\n'
  message = message .. '<SIMPLEREQ>\n'
  message = message .. '<IMETHODCALL NAME="EnumerateClassNames">\n'
  message = message .. '<LOCALNAMESPACEPATH>\n'
  for i,name in ipairs(namespace_array) do
    if name ~= nil and name ~= '' then
      message = message .. '<NAMESPACE NAME="' .. name .. '"/>\n'
    end
  end
  message = message .. '</LOCALNAMESPACEPATH>\n'
  message = message .. '<IPARAMVALUE NAME="DeepInheritance">\n'
  message = message .. '<VALUE>TRUE</VALUE>\n'
  message = message .. '</IPARAMVALUE>\n'
  message = message .. '</IMETHODCALL>\n'
  message = message .. '</SIMPLEREQ>\n'
  message = message .. '</MESSAGE>\n'
  message = message .. '</CIM>\n'
  return message
end

function assemble_xml_message(class, namespace_array)
  local message = ''
  message = message .. '<?xml version="1.0" encoding="utf-8" ?>\n'
  message = message .. '<CIM CIMVERSION="2.0" DTDVERSION="2.0">\n'
  message = message .. '<MESSAGE ID="1" PROTOCOLVERSION="1.0">\n'
  message = message .. get_simple_req(class, namespace_array)
  message = message .. '</MESSAGE>\n'
  message = message .. '</CIM>\n'
  return message
end

function assemble_headers(host, message, auth, cim_method, namespace)
  local headers = {}
  headers["Content-type"] = 'Content-type: application/xml; charset="utf-8"'
  headers["Host"] = host
  headers["Content-length"] = string.len(message)
  if auth ~= nil then
    headers["Authorization"] = "Basic " .. auth
  end
  headers["CIMOperation"] = "MethodCall"
  headers["CIMMethod"] = cim_method
  headers["CIMObject"] = namespace
  return headers
end

function get_enabled_state_values()
  local toreturn = {}
  toreturn[0] = 'Unknown'
  toreturn[1] = 'Other'
  toreturn[2] = 'Enabled'
  toreturn[3] = 'Disabled'
  toreturn[4] = 'Shutting Down'
  toreturn[5] = 'Not Applicable'
  toreturn[6] = 'Enabled But Offline'
  toreturn[7] = 'In Test'
  toreturn[8] = 'Deferred'
  toreturn[9] = 'Quiesce'
  toreturn[10] = 'Starting'
  return toreturn
end

function get_enabled_default_values()
  local toreturn = {}
  toreturn[2] = 'Enabled'
  toreturn[3] = 'Disabled'
  toreturn[5] = 'Not Applicable'
  toreturn[6] = 'Enabled But Offline'
  toreturn[7] = 'No Default'
  toreturn[9] = 'Quiesce'
  return toreturn
end

function get_health_state_values()
  local toreturn = {}
  toreturn[0] = 'Unknown'
  toreturn[5] = 'OK'
  toreturn[10] = 'Degraded'
  toreturn[15] = 'Minor'
  toreturn[20] = 'Major'
  toreturn[25] = 'Critical'
  toreturn[30] = 'Non-Recoverable Error'
  return toreturn
end

function get_operational_status_values()
  local toreturn = {}
  toreturn[0] = 'Unknown'
  toreturn[1] = 'Other'
  toreturn[2] = 'OK'
  toreturn[3] = 'Degraded'
  toreturn[4] = 'Stressed'
  toreturn[5] = 'Predictive Failure'
  toreturn[6] = 'Error'
  toreturn[7] = 'Non-Recoverable Error'
  toreturn[8] = 'Starting'
  toreturn[9] = 'Stopping'
  toreturn[10] = 'Stopped'
  toreturn[11] = 'In Service'
  toreturn[12] = 'No Contact'
  toreturn[13] = 'Lost Communication'
  toreturn[14] = 'Aborted'
  toreturn[15] = 'Dormant'
  toreturn[16] = 'Supporting Entity In Error'
  toreturn[17] = 'Completed'
  toreturn[18] = 'Power Mode'
  return toreturn
end

function get_requested_state_values()
  local toreturn = {}
  toreturn[0] = 'Unknown'
  toreturn[2] = 'Enabled'
  toreturn[3] = 'Disabled'
  toreturn[4] = 'Shut Down'
  toreturn[5] = 'No Change'
  toreturn[6] = 'Offline'
  toreturn[7] = 'Test'
  toreturn[8] = 'Deferred'
  toreturn[9] = 'Quiesce'
  toreturn[10] = 'Reboot'
  toreturn[11] = 'Reset'
  toreturn[12] = 'Not Applicable'
  return toreturn
end

function get_usage_restriction_values()
  local toreturn = {}
  toreturn[0] = 'Unknown'
  toreturn[2] = 'Front-end Only'
  toreturn[3] = 'Back-end Only'
  toreturn[4] = 'Not Restricted'
  return toreturn
end

function get_extent_status_values()
  local toreturn = {}
  toreturn[0] = 'Unknown'
  toreturn[1] = 'Other'
  toreturn[2] = 'None/Not Applicable'
  toreturn[3] = 'Broken'
  toreturn[4] = 'Data Lost'
  toreturn[5] = 'Dynamic Reconfig'
  toreturn[6] = 'Exposed'
  toreturn[7] = 'Fractionally Exposed'
  toreturn[8] = 'Partially Exposed'
  toreturn[9] = 'Protection Disabled'
  toreturn[10] = 'Readying'
  toreturn[11] = 'Rebuild'
  toreturn[12] = 'Recalculate'
  toreturn[13] = 'Spare In Use'
  toreturn[14] = 'Verify In Progress'
  toreturn[15] = 'In-Band Access Granted'
  toreturn[16] = 'Imported'
  toreturn[17] = 'Exported'
  return toreturn
end

function get_dedicated_values()
  local toreturn = {}
  toreturn[0] = 'Not Dedicated'
  toreturn[1] = 'Unknown'
  toreturn[2] = 'Other'
  toreturn[3] = 'Storage'
  toreturn[4] = 'Router'
  toreturn[5] = 'Switch'
  toreturn[6] = 'Layer 3 Switch'
  toreturn[7] = 'Central Office Switch'
  toreturn[8] = 'Hub'
  toreturn[9] = 'Access Server'
  toreturn[10] = 'Firewall'
  toreturn[11] = 'Print'
  toreturn[12] = 'I/O'
  toreturn[13] = 'Web Caching'
  toreturn[14] = 'Management'
  toreturn[15] = 'Block Server'
  toreturn[16] = 'File Server'
  toreturn[17] = 'Mobile User Device'
  toreturn[18] = 'Repeated'
  toreturn[19] = 'Bridge/Extender'
  toreturn[20] = 'Gateway'
  toreturn[21] = 'Storage Virtualizer'
  toreturn[22] = 'Media Library'
  toreturn[23] = 'ExtenderNode'
  toreturn[24] = 'NAS Head'
  toreturn[25] = 'Self-contained NAS'
  toreturn[26] = 'UPS'
  toreturn[27] = 'IP Phone'
  toreturn[28] = 'Management Controller'
  toreturn[29] = 'Chassis Manager'
  toreturn[30] = 'Host-based Raid Controller'
  toreturn[31] = 'Storage Device Enclosure'
  toreturn[32] = 'Desktop'
  toreturn[33] = 'Laptop'
  toreturn[34] = 'Virtual Tape Library'
  toreturn[35] = 'Virtual Library System'
  return toreturn
end

function get_upgrade_method_values()
  local toreturn = {}
  toreturn[1] = 'Other'
  toreturn[2] = 'Unknown'
  toreturn[3] = 'Daughter Board'
  toreturn[4] = 'ZIF Socket'
  toreturn[5] = 'Replacement/Piggy Back'
  toreturn[6] = 'None'
  toreturn[7] = 'LIF Socket'
  toreturn[8] = 'Slot 1'
  toreturn[9] = 'Slot 2'
  toreturn[10] = '370 Pin Socket'
  toreturn[11] = 'Slot A'
  toreturn[12] = 'Slot M'
  toreturn[13] = 'Socket 423'
  toreturn[14] = 'Socket A (Socket 462)'
  toreturn[15] = 'Socket 478'
  toreturn[16] = 'Socket 754'
  toreturn[17] = 'Socket 940'
  toreturn[18] = 'Socket 939'
  toreturn[19] = 'Socket mPGA604'
  toreturn[20] = 'Socket LGA771'
  toreturn[21] = 'Socket LGA775'
  toreturn[22] = 'Socket S1'
  toreturn[23] = 'Socket AM2'
  toreturn[24] = 'Socket F (1207)'
  return toreturn
end

function initiate(module, check)
    local url = check.config.url or 'http:///'
    local schema, host, ignore_port, uri = string.match(url, "^(https?)://([^:/]*):?([0-9]*)(/?.*)$");
    local use_ssl = false

    if schema == nil then
      url = "http://" .. url
      schema, host, ignore_port, uri = string.match(url, "^(https?)://([^/]*)(.+)$");
    end
    
    local codere = mtev.pcre(check.config.code or '^200$')
    local good = false
    local starttime = mtev.timeval.now()
    local encoded = nil
    local class_string = check.config.classes or 'OMC_SMASHFirmwareIdentity,CIM_Chassis,CIM_Card,CIM_ComputerSystem,CIM_NumericSensor,CIM_Memory,CIM_Processor,CIM_RecordLog,OMC_DiscreteSensor,OMC_Fan,OMC_PowerSupply,VMware_StorageExtent,VMware_Controller,VMware_StorageVolume,VMware_Battery,VMware_SASSATAPort'
    local fields_string = check.config.fields or 'ALL'
    local namespace = check.config.namespace or "root/cimv2"

    -- assume the worst.
    check.bad()
    check.unavailable()

    -- Set string representation of status value constants
    health_state_values = get_health_state_values()
    operational_status_values = get_operational_status_values()
    enabled_state_values = get_enabled_state_values()
    enabled_default_values = get_enabled_default_values()
    requested_state_values = get_requested_state_values()
    usage_restriction_values = get_usage_restriction_values()
    extent_status_values = get_extent_status_values()
    dedicated_values = get_dedicated_values()
    upgrade_method_values = get_upgrade_method_values()

    if host == nil then host = check.target end
    if schema == nil then
        schema = 'http'
        uri = '/'
    end
    if schema == 'http' then
        port = check.config.port or 5988
    elseif schema == 'https' then
        port = check.config.port or 5989
        use_ssl = true
    else
        error(schema .. " not supported")
    end 

    local classes = mtev.extras.split(class_string, ",")
    local fields_to_check = mtev.extras.split(fields_string, ",")
    local namespace_array = mtev.extras.split(namespace, "/")

    for ind, val in ipairs(fields_to_check) do
        fields_to_check[ind] = val:gsub("^%s*(.-)%s*$", "%1")
    end

    local output = ''

    -- callbacks from the HttpClient
    local callbacks = { }
    local hdrs_in = { }
    callbacks.consume = function (str) output = output .. str end
    callbacks.headers = function (t) hdrs_in = t end
   
    -- perform the request
    if check.config.auth_method == "Basic" or
        (check.config.auth_method == nil and
            check.config.auth_user ~= nil and
            check.config.auth_password ~= nil) then
        local user = check.config.auth_user or nil
        local pass = check.config.auth_password or nil
        if (user ~= nil and pass ~= nil) then
            encoded = mtev.base64_encode(user .. ':' .. pass)
        end
    elseif check.config.auth_method ~= nil then
        check.status("Unknown auth method: " .. check.config.auth_method)
        return
    end

    local metrics = 0
    if classes ~= nil then
      for index, classname in ipairs(classes) do
        local client = HttpClient:new(callbacks)
        local rv, err = client:connect(check.target_ip, port, use_ssl)
        if rv ~= 0 then
          check.status(err or "unknown error")
          return
        end
        classname = classname:gsub("^%s*(.-)%s*$", "%1")
        if classname ~= nil and classname ~= '' then
          local payload
          local headers
          local cim_method

          if classname == 'PullClassList' then
            -- A Special Case - pull a list of all of the classes available
            payload = assemble_class_list_xml_message(namespace_array)
            cim_method = "EnumerateClassNames"
          else
            payload = assemble_xml_message(classname, namespace_array)
            cim_method = "EnumerateInstances"
          end
          headers = assemble_headers(host, payload, encoded, cim_method, namespace)

          client:do_request("POST", uri, headers, payload)
          client:get_response(1024000)

          -- parse the xml doc
          local doc = mtev.parsexml(output)
          if doc ~= nil then
            if classname == 'PullClassList' then
              metrics = metrics + classname_xml_to_metrics(check, doc)
            else
              metrics = metrics + xml_to_metrics(check, doc, classname, fields_to_check)
            end
          end
          output = ''
        end
      end
    end

    check.available()

    local status = ''
    if metrics > 0 then 
      check.good() 
    else 
      check.bad() 
    end
    check.status("available_metrics=" .. metrics)
end

