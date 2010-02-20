-- Copyright (c) 2010, OmniTI Computer Consulting, Inc.
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
  <name>ntp</name>
  <description><para>Determine clock skew from an NTP source.</para></description>
  <loader>lua</loader>
  <object>noit.module.ntp</object>
  <moduleconfig />
  <checkconfig />
  <examples>
    <example>
      <title>Monitor an NTP service</title>
      <para>The following example monitors an NTP services on 10.1.2.3.</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua">
            <config><directory>/opt/reconnoiter/libexec/modules-lua/?.lua</directory></config>
          </loader>
          <module loader="lua" name="ntp" object="noit.module.ntp"/>
        </modules>
        <checks>
          <check uuid="4ee1a1e2-1e60-11df-8e99-bf796ca462ef" module="ntp" target="10.1.2.3" period="60000" timeout="5000"/>
        </checks>
      </noit>
      ]]></programlisting>
    </example>
  </examples>
</module>]=])
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

function timeval2ntp64(sec, usec)
   -- packs a timeval into an NTP 64bit double
   if(sec == 0 and usec == 0) then return string.pack('L', 0) end
   local l32 = sec + 2208988800
   local r32 = 4294.967296 * usec + 0.5
   return string.pack('>II', l32, r32)
end

function ntp642timeval(s)
  local cnt, l32, r32 = string.unpack(s, '>II')
  local sec = l32 - 2208988800
  local usec = (r32 - 0.5) / 4294.967296
  return noit.timeval.new(sec, usec)
end

function double2ntp32(v)
   local l16 = math.floor(v)
   local r16 = 65536 * (v - l16)
   return string.pack('>hH', l16, r16)
end

function ntp322double(s)
   local cnt, l16, r16 = string.unpack(s, '>hH')
   return l16 + (r16 / 65536)
end

function make_ntp_request(fin)
    local f = fin or { }
                             --    ALARM         V4      CLIENT
    f.flags = f.flags or 227 -- (0x03 << 6) | (4 << 3) | 3
    f.stratum = f.stratum or 0
    f.poll = f.poll or 4
    f.precision = f.precision or 250
    f.rtdisp = f.rtdisp or 1
    f.rtdelay = f.rtdelay or 1
    f.refid = f.refid or 0
    return string.pack('>bbcc', f.flags, f.stratum, f.poll, f.precision)
        .. double2ntp32(f.rtdisp)
        .. double2ntp32(f.rtdelay)
        .. string.pack('>I', f.refid)
        .. timeval2ntp64(0,0)
        .. timeval2ntp64(0,0)
        .. timeval2ntp64(0,0)
        .. timeval2ntp64(noit.gettimeofday())
end

function decode_ntp_message(b)
    local cnt
    -- not as easy as a simple unpack
    local ntp_hdr = string.sub(b,1,4)
    local ntp_rtdelay = string.sub(b,5,8)
    local ntp_rtdisp = string.sub(b,9,12)
    local ntp_refid = string.sub(b,13,16)
    local ntp_refts = string.sub(b,17,24)
    local ntp_origts = string.sub(b,25,32)
    local ntp_rxts = string.sub(b,33,40)
    local ntp_txts = string.sub(b,41,48)
    local r = { }
    cnt, r.flags, r.stratum, r.poll, r.precision =
        string.unpack(ntp_hdr, '>bbcc')
    r.rtdelay = ntp322double(ntp_rtdelay)
    r.rtdisp = ntp322double(ntp_rtdisp)
    cnt, r.refid = string.unpack(ntp_refid, '>I')
    r.refts = ntp642timeval(ntp_refts)
    r.origts = ntp642timeval(ntp_origts)
    r.rxts = ntp642timeval(ntp_rxts)
    r.txts = ntp642timeval(ntp_txts)
    return r
end

function calculate_offset(response, now)
    local there_and = noit.timeval.seconds(response.rxts - response.origts)
    local back_again = noit.timeval.seconds(response.txts - now)
    return ( there_and + back_again ) / 2.0
end

function initiate(module, check)
    local s = noit.socket('inet', 'udp')
    local status = { }
    local cnt = check.config.count or 4

    check.unavailable()
    check.bad()

    s:connect(check.target, 123)
    status.responses = 0
    status.avg_offset = 0
    status.offset = { }

    for i = 1,cnt do
        local req = make_ntp_request()
        s:send(req)
        local rv, s = s:recv(48)
        local now = noit.timeval.now()
        local response = decode_ntp_message(s)
        local offset = calculate_offset(response, now)
        if offset ~= nil then
            table.insert(status.offset, offset)
            status.avg_offset = status.avg_offset + offset
            status.stratum = response.stratum
            status.poll = math.pow(2, response.poll)
            status.precision = math.pow(2, response.precision)
            status.rtdisp = response.rtdisp
            status.rtdelay = response.rtdelay
            status.responses = status.responses + 1
        end
        noit.sleep(0.1)
    end

    status.avg_offset = status.avg_offset / # status.offset
    check.status( cnt .. '/' .. status.responses )

    if # status.offset > 0 then
        check.metric_double('offset', status.avg_offset)
        check.metric_uint32('requests', cnt)
        check.metric_uint32('responses', status.responses)
        check.metric_uint32('stratum', status.stratum)
        check.metric_int32('poll', status.poll)
        check.metric_double('precision', status.precision)
        check.metric_double('rtdisp', status.rtdisp)
        check.metric_double('rtdelay', status.rtdelay)
        check.available()
        check.good()
    end
end
