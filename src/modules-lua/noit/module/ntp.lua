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

local band,     bor,     bxor,     bnot,     rshift,     lshift
    = bit.band, bit.bor, bit.bxor, bit.bnot, bit.rshift, bit.lshift

function onload(image)
  image.xml_description([=[
<module>
  <name>ntp</name>
  <description><para>Determine clock skew from an NTP source.</para></description>
  <loader>lua</loader>
  <object>noit.module.ntp</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port"
               required="optional"
               default="^123$"
               allowed="\d+">The port to which we will attempt to speak NTP.</parameter>
    <parameter name="control"
               required="optional"
               default="^false$"
               allowed="^(?:true|on|false|off)$">Use the NTP control protocol to learn about the other end.  If thise ois not true/on, then this check will determine the NTP telemetry of the target relative to the agent's local time.  If it is true/on, then the agent will request the NTP telemetry of the target regarding it's preferred peer.</parameter>
  </checkconfig>
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

function parts2timeval(l32, r32)
  local sec = l32 - 2208988800
  local usec = (r32 - 0.5) / 4294.967296
  return noit.timeval.new(sec, usec)
end

function ntp642timeval(s)
  local cnt, l32, r32 = string.unpack(s, '>II')
  return parts2timeval(l32, r32)
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

local _sequence = 0
function next_sequence()
  _sequence = _sequence + 1
  return _sequence
end

function make_ntp_control(req)
    req.version = req.version or 2 -- NTP version
    req.mode = req.mode or 6 -- control
    req.leap = req.leap or 0
    -- contruct
    req.li_vn_mode = bor(bor(band(req.mode,0x7),
                             lshift(band(req.version,0x7),3)),
                         lshift(band(req.leap,6),0x3))
    req.op = req.op or 0x01
    req.r_m_e_op = band(req.op,0x1f)
    req.sequence = req.sequence or next_sequence()
    req.status = req.status or 0
    req.associd = req.associd or 0
    req.offset = req.offset or 0
    req.count = req.count or 0
    local qcnt = req.count
    req.data = req.data or ''
    req.pad = ''
    while (qcnt % 8) ~= 0 do
        req.pad = req.pad .. '\0'
    end
    return string.pack('>bbHHHHH', req.li_vn_mode, req.r_m_e_op, req.sequence,
                       req.status, req.associd, req.offset, req.count)
        .. req.data
        .. req.pad
         , req.sequence
end

function ntp_control(s, req)
    local f = { }
    local req_packet = make_ntp_control(req)
    s:send(req_packet)

    f.num_frags = 0
    f.offsets = {}
    local done = false
    repeat
        local rv, buf = s:recv(480) -- max packet
        local offset, count, cnt
        -- need at least a header
        if buf:len() < 12 then return "short packet" end

        f.hdr = buf:sub(1,12)
        f.buf = buf:sub(13,buf:len())
        cnt, f.li_vn_mode, f.r_m_e_op, f.sequence,
            f.status, f.associd, offset, count = string.unpack(f.hdr, '>bbHHHHH')

        f.mode = band(f.li_vn_mode, 0x7)
        f.version = band(rshift(f.li_vn_mode, 3), 0x7)
        f.leap = band(rshift(f.li_vn_mode, 6), 0x3)
        f.op = band(f.r_m_e_op, 0x1f)
        f.is_more = band(f.r_m_e_op, 0x20) ~= 0
        f.is_error = band(f.r_m_e_op, 0x40) ~= 0
        f.is_response = band(f.r_m_e_op, 0x80) ~= 0

        -- validate
        if f.version > 4 or f.version < 1 then return "bad version" end
        if f.mode ~= 6 then return "not a control packet" end
        if not f.is_response then return "not a response packet" end
        if req.sequence ~= f.sequence then return "sequence mismatch" end
        if req.op ~= f.op then return "opcode mismatch " .. req.op .. " != " .. f.op  end
        if f.is_error then
            return "error: "
                .. bit.tohex(band(rshift(f.status, 8), 0xff), 2)
        end
        local expect = band(band(12 + count + 3, bnot(3)),0xffff)
        -- must be aligned on a word boundary
        if band(buf:len(), 3) ~= 0 then return "bad padding" end
        if expect > buf:len() then
            return "bad payload size " .. expect .. " vs. " .. buf:len()
        end
        if expect < buf:len() then
            -- auth
            return "auth unsupported " .. expect .. " vs. " .. buf:len()
        end
        if f.num_frags > 23 then return "too many fragments" end
        if count < f.buf:len() then
            f.buf = f.buf:sub(1,count)
        end
        f.offsets[offset] = f.buf
        done = not f.is_more
    until done

    f.data = ''
    for i, buf in pairs(f.offsets) do f.data = f.data .. buf end
    return nil, f
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

function initiate_control(module, check, s)
    local err, result = ntp_control(s, {})
    local associations = {}
    if err ~= nil then
        check.status(err)
        return
    end
    local i = 0
    local len, numassoc = result.data:len(), result.data:len() / 4;
    local use_id = 0
    while len > 0 do
      local cnt, associd, status = string.unpack(result.data:sub(1+4*i, 4+4*i), '>HH')
      i = i + 1
      len = len - 4;
      associations[i] = { }
      associations[i].associd = associd
      associations[i].status = status
      if result.version > 1 then
          associations[i].flash = band(rshift(status,8),0x7)
          associations[i].prefer = band(associations[i].flash,0x2) ~= 0
          associations[i].burst = band(associations[i].flash,0x4) ~= 0
          associations[i].volley = band(associations[i].flash,0x1) ~= 0
      else
          associations[i].flash = band(rshift(status,8),0x3)
          associations[i].prefer = band(associations[i].flash,0x1) ~= 0
          associations[i].burst = band(associations[i].flash,0x2) ~= 0
          associations[i].volley = false
      end
      if(associations[i].prefer) then use_id = i end
    end
    if(use_id < 1) then use_id = 1 end

    err, result = ntp_control(s, { associd = associations[use_id].associd })
    if err ~= nil then
        check.status(err)
        return
    end
    local vars = {}
    for k, v in string.gmatch(result.data, "%s*([^,]+)=([^,]+)%s*,%s*") do
       vars[k] = v;
       noit.log("debug", "ntp: %s = %s\n", k, v)
    end
    check.metric_string('clock_name', vars.srcadr)
    check.metric_int32('stratum', tonumber(vars.stratum))

    -- parse the rec and the reftime
    local rec_l, rec_h = vars.rec:match('^0x([%da-fA-F]+)%.([%da-fA-F]+)$')
    rec_l, rec_h = tonumber("0x"..rec_l), tonumber("0x"..rec_h)
    local rec = parts2timeval(rec_l, rec_h)

    local reftime_l, reftime_h = vars.reftime:match('^0x([%da-fA-F]+)%.([%da-fA-F]+)$')
    reftime_l, reftime_h = tonumber("0x"..reftime_l), tonumber("0x"..reftime_h)
    local reftime = parts2timeval(reftime_l, reftime_h)

    local when = nil
    if rec.sec ~= 0 then when = noit.timeval.seconds(noit.timeval.now() - rec)
    elseif reftime.sec ~= 0 then when = noit.timeval.seconds(noit.timeval.now() - reftime)
    end
    check.metric_double('when', when)
    local poll = math.pow(2, math.max(math.min(vars.ppoll or 17, vars.hpoll or 17), 3))
    check.metric_uint32('poll', poll)
    check.metric_double('delay', tonumber(vars.delay))
    check.metric_double('offset', tonumber(vars.offset))
    check.metric_double('jitter', tonumber(vars.jitter))
    check.metric_double('dispersion', tonumber(vars.dispersion))
    check.metric_double('xleave', tonumber(vars.xleave))
    check.metric_int32('peers', numassoc)
    check.status("ntp successful")
    check.available()
    check.good()
end

function initiate(module, check)
    local s = noit.socket(check.target_ip, 'udp')
    local status = { }
    local cnt = check.config.count or 4

    check.unavailable()
    check.bad()

    s:connect(check.target_ip, check.config.port or 123)
    status.responses = 0
    status.avg_offset = 0
    status.offset = { }

    if check.config.control == "true" or check.config.control == "on" then
        return initiate_control(module, check, s)
    end

    for i = 1,cnt do
        local req = make_ntp_request()
        s:send(req)
        local rv, buf = s:recv(48)
        local now = noit.timeval.now()
        local response = decode_ntp_message(buf)
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
