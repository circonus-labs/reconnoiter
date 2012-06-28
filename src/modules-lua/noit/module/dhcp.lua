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
    <parameter name="send_port" required="required"
               allowed="\d+">Specifies the port to send DHCP request packets to</parameter>
    <parameter name="recv_port" required="required"
               allowed="\d+">Specifies the port to receive DHCP response packets to</parameter>
    <parameter name="hardware_addr" required="required"
               allowed=".+">The hardware address of the host computer</parameter>
    <parameter name="host_ip" required="required"
               allowed=".+">The IP address of the host computer</parameter>
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
                <send_port>67</send_port>
                <recv_port>68</recv_port>
                <hardware_addr>00:00:00:00:00:00</hardware_addr>
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

function make_dhcp_request(host_ip, hardware_addr)
    local packet = ''
    local host_ip_number = noit.extras.iptonumber(host_ip)

    packet = packet .. string.pack(">bbbb", 1, 1, 6, 0)
    packet = packet .. string.pack(">I", math.random(0, 99999999))
    packet = packet .. string.pack(">HH", 0, 0x0000)
    packet = packet .. string.pack(">I", host_ip_number) -- Client IP Address - not required for this to work
    packet = packet .. string.pack(">III", 0, 0, 0)
    packet = packet .. pack_hardware_address(hardware_addr) -- Client MAC address
    packet = packet .. string.rep(string.char(0), 192) -- Not used - just fill in zeroes
    packet = packet .. string.pack(">bbbb", 99, 130, 83, 99) -- Magic Cookie - required for this to work
    packet = packet .. string.pack(">bbb", 53, 1, 1)
    if host_ip_number ~= 0 then
      packet = packet .. string.pack(">bbI", 50, 4, noit.extras.iptonumber(host_ip))
    end
    packet = packet .. string.pack(">b", 0xFF)
    return packet
end

function parse_option(options, dhcp_option_names, dhcp_message_types, check)
  if options:len() <= 1 then
    return 0
  end
  local pos, type, length = string.unpack(options, "<bb")
  local data = ''
  if length > 0 then
    data = string.sub(options, 3, 3+length)
  end
  -- Lua doesn't do switch, so time for a lengthy if statement
  if type == 1 or type == 3 or type == 6 or type == 28 or type == 42 or type == 54  then
    -- IP Address data types
    local result = ''
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
    check.metric_string(dhcp_option_names[type], result)
  elseif type == 2 or type == 51 or type == 58 or type == 59 then
    -- Integer data types
    local pos, result = string.unpack(data, ">I")
    check.metric_int32(dhcp_option_names[type], result)
  elseif type == 15 or type == 119 then
    -- String data types
    local result = string.sub(data, 1, length) .. '\0'
    check.metric_string(dhcp_option_names[type], result)
  elseif type == 53 then
    -- Message type
    local pos, dhcp_type = string.unpack(data, "<b")
    local result = ''
    if dhcp_type >= 1 and dhcp_type <= 8 then
      result = dhcp_type .. " (" .. dhcp_message_types[dhcp_type] .. ")"
    else
      result = dhcp_type .. "(UNKNOWN)"
    end
    check.metric_string(dhcp_option_names[type], result)
  end
  return 3+length
end

function parse_buffer(buf, dhcp_option_names, dhcp_message_types, check)
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
  -- Now, parse the options
  local done = false
  while done == false do
    local tomove = parse_option(options_data, dhcp_option_names, dhcp_message_types, check)
    if tomove == 0 then
      done = true
    else
      options_data = string.sub(options_data, tomove)
    end
  end
  check.metric_string("client_ip_address", ciaddr)
  check.metric_string("your_ip_address", yiaddr)
  check.metric_string("server_ip_address", siaddr)
  check.metric_string("gateway_ip_address", giaddr)
  check.metric_string("client_hardware_address", chaddr)
  check.metric_string("server_name", sname)
  check.metric_string("boot_filename", file)
end

function initiate(module, check)
  local send_port = check.config.send_port or 67
  local recv_port = check.config.recv_port or 68
  local host_ip = check.config.host_ip or "0.0.0.0"
  local hardware_addr = check.config.hardware_addr or "00:00:00:00:00:00"
  local good = false
  local status = ""
  local dhcp_option_names = get_dhcp_option_names()
  local dhcp_message_types = get_dhcp_message_types()

  check.bad()
  check.unavailable()
  check.status("unknown error")

  if check.target_ip == nil then
    check.status("dns resolution failure")
    return
  end

  local starttime = noit.timeval.now()
  local s = noit.socket(check.target_ip, 'udp')
  s:connect(check.target_ip, recv_port, "bind")
  local req = make_dhcp_request(host_ip, hardware_addr)
  local sent = s:sendto(req, check.target_ip, send_port)
  local rv, buf = s:recv(1000)

  if buf:len() < 240 then
    status = "invalid buffer"
  else
    parse_buffer(buf, dhcp_option_names, dhcp_message_types, check)
    status = "successful read"
    check.available()
    good = true
  end

  -- turnaround time
  local endtime = noit.timeval.now()
  local seconds = elapsed(check, "duration", starttime, endtime)
  status = status .. ',rt=' .. seconds .. 's'
  if good then check.good() else check.bad() end
  check.status(status)
end

