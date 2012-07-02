-- Copyright (c) 2012, OmniTI Computer Consulting, Inc.
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
  <name>dhcp</name>
  <description><para>DHCP metrics check.</para></description>
  <loader>lua</loader>
  <object>noit.module.dhcp</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="send_port" required="optional" default="67"
               allowed="\d+">Specifies the port to send DHCP request packets to</parameter>
    <parameter name="recv_port" required="optional" default="68"
               allowed="\d+">Specifies the port to receive DHCP response packets to</parameter>
    <parameter name="hardware_addr" required="required" default="00:00:00:00:00:00"
               allowed=".+">The hardware address of the host computer</parameter>
    <parameter name="host_ip" required="optional" default="0.0.0.0"
               allowed=".+">The IP address of the host computer</parameter>
    <parameter name="request_type" required="optional" default="1"
               allowed="^(?:1|8)">The type of DHCP request message to send</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Checking DHCP connection.</title>
      <para>This example checks DHCP connection</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="dhcp" object="noit.module.dhcp" />
        </modules>
        <checks>
            <check uuid="79ba881e-ad2e-11de-9fb0-a322e3288ca7" name="dhcp">
              <config>
                <hardware_addr>00:00:00:00:00:00</hardware_addr>
                <host_ip>10.80.1.2</host_ip>
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

function init(module)
  return 0
end

function config(module, options)
  return 0
end

function elapsed(check, name, starttime, endtime)
    local elapsedtime = endtime - starttime
    local seconds = string.format('%.3f', noit.timeval.seconds(elapsedtime))
    check.metric_uint32(name, math.floor(seconds * 1000 + 0.5))
    return seconds
end

function get_dhcp_option_names()
  local dhcp_option_names = {}
  -- This isn't complete - just the ones we're likely to see from a DHCPDISCOVER message
  dhcp_option_names[1] = "subnet_mask"
  dhcp_option_names[2] = "time_offset"
  dhcp_option_names[3] = "router"
  dhcp_option_names[6] = "domain_name_server"
  dhcp_option_names[15] = "domain_name"
  dhcp_option_names[28] = "broadcast_address"
  dhcp_option_names[42] = "ntp_servers"
  dhcp_option_names[51] = "ip_address_lease_time"
  dhcp_option_names[53] = "dhcp_message_type"
  dhcp_option_names[54] = "server_identifier"
  dhcp_option_names[58] = "renew_time_value"
  dhcp_option_names[59] = "rebinding_time_value"
  dhcp_option_names[119] = "dns_domain_search_list"
  return dhcp_option_names
end

function get_dhcp_message_types()
  local dhcp_message_types = {}
  dhcp_message_types[1] = "DHCPDiscover"
  dhcp_message_types[2] = "DHCPOffer"
  dhcp_message_types[3] = "DHCPRequest"
  dhcp_message_types[4] = "DHCPDecline"
  dhcp_message_types[5] = "DHCPAck"
  dhcp_message_types[6] = "DHCPNak"
  dhcp_message_types[7] = "DHCPRelease"
  dhcp_message_types[8] = "DHCPInform"
  return dhcp_message_types
end

function convert_integer_to_ip(int)
  return string.format("%d.%d.%d.%d", bit.rshift(bit.band(int, 0xFF000000), 24), bit.rshift(bit.band(int, 0x00FF0000), 16), 
    bit.rshift(bit.band(int, 0x0000FF00), 8), bit.band(int, 0x000000FF))
end

function convert_binary_string_to_ip(str)
  local pos, first, second, third, fourth = string.unpack(str, "<bbbb")
  return string.format("%d.%d.%d.%d", first, second, third, fourth)
end

function convert_binary_string_to_mac(str)
  local pos, first, second, third, fourth, fifth, sixth = string.unpack(str, "<bbbbbb")
  return string.upper(string.format("%02x:%02x:%02x:%02x:%02x:%02x", first, second, third, fourth, fifth, sixth))
end

function pack_hardware_address(hardware_addr)
  local ret = ''
  local count = 0
  for elem in hardware_addr:gmatch("%x+") do
    elem = tonumber("0x" .. elem, 10)
    ret = ret .. string.pack(">b", elem)
    count = count + 1
  end
  while count < 16 do
    ret = ret .. string.pack(">b", 0)
    count = count + 1
  end
  return ret
end

function make_dhcp_request(host_ip, hardware_addr, request_type)
    local packet = ''
    local host_ip_number = noit.extras.iptonumber(host_ip)

    packet = packet .. string.pack(">bbbb", 1, 1, 6, 0)
    packet = packet .. string.pack(">I", math.random(0, 99999999))
    packet = packet .. string.pack(">HH", 0, 0x0000)
    packet = packet .. string.pack(">IIII", 0, 0, 0, 0)
    packet = packet .. pack_hardware_address(hardware_addr) -- Client MAC address
    packet = packet .. string.rep(string.char(0), 192) -- Not used - just fill in zeroes
    packet = packet .. string.pack(">bbbb", 99, 130, 83, 99) -- Magic Cookie - required for this to work
    packet = packet .. string.pack(">bbb", 53, 1, request_type)
    if host_ip_number ~= 0 then
      packet = packet .. string.pack(">bbI", 50, 4, host_ip_number)
    end
    packet = packet .. string.pack(">bbbbbbbbbbbbbbb", 55, 13, 1, 2, 3, 6, 15, 28, 42, 51, 53, 54, 58, 59, 119)
    packet = packet .. string.pack(">b", 0xFF)
    return packet
end

function parse_option(options, dhcp_option_names, dhcp_message_types, check)
  local data_type = 'string'
  if options:len() <= 1 then
    return 0
  end
  local pos, type, length = string.unpack(options, "<bb")
  local data = ''
  local result
  if length > 0 then
    data = string.sub(options, 3, 3+length)
  end
  -- Lua doesn't do switch, so time for a lengthy if statement
  if type == 1 or type == 3 or type == 6 or type == 28 or type == 42 or type == 54  then
    -- IP Address data types
    result = ''
    while data:len() >= 4 do
      local ip = convert_binary_string_to_ip(string.sub(data, 1, 4))
      if result == '' then
        result = ip
      else
        result = result .. ' ' .. ip
      end
      if data:len() > 5 then
        data = string.sub(data, 5)
      else
        data = ''
      end
    end
  elseif type == 2 or type == 51 or type == 58 or type == 59 then
    -- Integer data types
    pos, result = string.unpack(data, ">I")
    data_type = 'int32'
  elseif type == 15 or type == 119 then
    -- String data types
    result = string.sub(data, 1, length) .. '\0'
  elseif type == 53 then
    -- Message type
    local pos, dhcp_type = string.unpack(data, "<b")
    result = ''
    if dhcp_type >= 1 and dhcp_type <= 8 then
      result = dhcp_type .. " (" .. dhcp_message_types[dhcp_type] .. ")"
    else
      result = dhcp_type .. "(UNKNOWN)"
    end
  end
  return 3+length, dhcp_option_names[type], result, data_type
end

function parse_buffer(buf, dhcp_option_names, dhcp_message_types, check, target_ip, hardware_addr, readaddr)
  local option_strings = { }
  local option_ints = { }
  --First, unpack the buffer
  local pos, op, htype, hlen, hops, xid, secs, flags = string.unpack(buf, ">bbbbIHH")
  local ciaddr = convert_binary_string_to_ip(string.sub(buf, 13, 16))
  local yiaddr = convert_binary_string_to_ip(string.sub(buf, 17, 20))
  local siaddr = convert_binary_string_to_ip(string.sub(buf, 21, 24))
  local giaddr = convert_binary_string_to_ip(string.sub(buf, 25, 28))
  local chaddr = convert_binary_string_to_mac(string.sub(buf, 29, 44))
  local sname = string.sub(buf, 45, 108) .. '\0'
  local file = string.sub(buf, 109, 236) .. '\0'
  local magic_data = convert_binary_string_to_ip(string.sub(buf, 237, 240))
  local options_data = string.sub(buf, 241)
  -- First, verify that we got this from the hardware address that we were supposed to - otherwise, we may use
  -- packets intended for other checks
  if chaddr ~= hardware_addr then
    return 0
  end
  -- Now, parse the options
  local done = false
  while done == false do
    local tomove, option_name, option_result, option_type = parse_option(options_data, dhcp_option_names, dhcp_message_types, check)
    if tomove == 0 then
      done = true
    else
      if option_type == 'int32' then
        option_ints[option_name] = option_result
      else
        option_strings[option_name] = option_result
      end
      options_data = string.sub(options_data, tomove)
    end
  end
  -- Now, verify that we got a result from a trusted source - otherwise, bail and try again
  local server_addr = ''
  if readaddr == nil then
    server_addr = siaddr
  else
    server_addr=convert_integer_to_ip(readaddr)
  end
  if server_addr ~= target_ip then
    -- We didn't get it from the target - So, check to see if we got it from the broadcast ip
    -- If the server didn't return the broadcast IP, we can't verify that we got it from the
    -- right source, so we'll have to bail.
    if option_strings['broadcast_address'] == nil or option_strings['broadcast_address'] ~= target_ip then
      return 0
    end
  end
  -- We're good - set all the metrics and return that we're done
  for k,v in pairs(option_ints) do
    check.metric_int32(k, v)
  end
  for k,v in pairs(option_strings) do
    check.metric_string(k, v)
  end
  check.metric_string("client_ip_address", ciaddr)
  check.metric_string("your_ip_address", yiaddr)
  check.metric_string("server_ip_address", siaddr)
  check.metric_string("gateway_ip_address", giaddr)
  check.metric_string("client_hardware_address", chaddr)

  local first
  pos, first = string.unpack(sname, ">b")
  if first ~= 0 then
    check.metric_string("server_name", sname)
  end
  pos, first = string.unpack(file, ">b")
  if first ~= 0 then
    check.metric_string("boot_filename", file)
  end
  return 1
end

function initiate(module, check)
  local send_port = check.config.send_port or 67
  local recv_port = check.config.recv_port or 68
  local host_ip = check.config.host_ip or "0.0.0.0"
  local hardware_addr = check.config.hardware_addr or "00:00:00:00:00:00"
  local request_type = check.config.request_type or 1
  local good = false
  local status = ""
  local dhcp_option_names = get_dhcp_option_names()
  local dhcp_message_types = get_dhcp_message_types()
  local rv = 0
  local buf = ''
  local done = 0

  check.bad()
  check.unavailable()
  check.status("unknown error")

  if check.target_ip == nil then
    check.status("dns resolution failure")
    return
  end

  local starttime = noit.timeval.now()
  local s = noit.socket(check.target_ip, 'udp')
  s:connect(check.target_ip, recv_port, 'broadcast')
  local req = make_dhcp_request(host_ip, hardware_addr, request_type)
  local sent = s:sendto(req, check.target_ip, send_port)

  while done == 0 do
    rv, buf, readaddr = s:recv(1000)

    if buf:len() < 240 then
      status = "invalid buffer"
    else
      done = parse_buffer(buf, dhcp_option_names, dhcp_message_types, check, check.target_ip, hardware_addr, readaddr)
      status = "successful read"
      check.available()
      good = true
    end
  end

  -- turnaround time
  local endtime = noit.timeval.now()
  local seconds = elapsed(check, "duration", starttime, endtime)
  status = status .. ',rt=' .. seconds .. 's'
  if good then check.good() else check.bad() end
  check.status(status)
end

