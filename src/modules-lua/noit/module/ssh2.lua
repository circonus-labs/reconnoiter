-- Copyright (c) 2015, Circonus, Inc.
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

local table = table
local mtev = mtev

module(..., package.seeall)

function onload(image)
  image.xml_description([=[
<module>
  <name>ssh2</name>
  <description><para>The ssh2 module allows reconnoiter to connect to servers over ssh protocol 2 and test the fingerprint.</para></description>
  <loader>lua</loader>
  <object>noit.module.ssh2</object>
  <moduleconfig />
  <checkconfig>
    <parameter name="port"
               required="optional"
               default="22"
               allowed="\d+">The TCP port on which the remote server's ssh service is running.</parameter>
    <parameter name="method_kex"
               required="optional"
               default="diffie-hellman-group14-sha1"
               allowed="^diffie-hellman-(?:group1-sha1|group14-sha1|group16-sha512|group18-sha512)$">The key exchange method to use.</parameter>
    <parameter name="method_hostkey"
               required="optional"
               default="ssh-rsa"
               allowed="^(?:ssh-rsa|ecdsa-sha2-nistp256|ssh-ed25519|ssh-dss)$">The host key algorithm supported.</parameter>
    <parameter name="method_crypt_cs"
               required="optional"
               allowed="^(?:aes256-ctr|aes192-ctr|aes128-ctr|aes256-cbc|aes192-cbc|aes128-cbc|blowfish-cbc|arcfour128|arcfour|cast128-cbc|3des-cbc|none)$">The encryption algorithm used from client to server.</parameter>
    <parameter name="method_crypt_sc"
               required="optional"
               allowed="^(?:aes256-ctr|aes192-ctr|aes128-ctr|aes256-cbc|aes192-cbc|aes128-cbc|blowfish-cbc|arcfour128|arcfour|cast128-cbc|3des-cbc|none)$">The encryption algorithm used from server to client.</parameter>
    <parameter name="method_mac_cs"
               required="optional"
               allowed="^(?:hmac-sha1|hmac-sha1-96|hmac-md5|hmac-md5-96|hmac-ripemd160|none)$">The message authentication code algorithm used from client to server.</parameter>
    <parameter name="method_mac_sc"
               required="optional"
               allowed="^(?:hmac-sha1|hmac-sha1-96|hmac-md5|hmac-md5-96|hmac-ripemd160|none)$">The message authentication code algorithm used from server to client.</parameter>
    <parameter name="method_comp_cs"
               required="optional"
               default="none"
               allowed="^(?:zlib|none)$">The compress algorithm used from client to server.</parameter>
    <parameter name="method_comp_sc"
               required="optional"
               default="none"
               allowed="^(?:zlib|none)$">The compress algorithm used from server to client.</parameter>
    <parameter name="method_lang_cs"
               required="optional"
               default=""
               allowed="^(?:|\w+)$">The language used from client to server.</parameter>
    <parameter name="method_lang_sc"
               required="optional"
               default=""
               allowed="^(?:|\w+)$">The language used from server to client.</parameter>
  </checkconfig>
  <examples>
    <example>
      <title>Simple ssh polling of 4 machines</title>
      <para>The following checks ssh on 10.1.2.{3,4,5,6}</para>
      <programlisting><![CDATA[
      <noit>
        <modules>
          <loader image="lua" name="lua"/>
          <module loader="lua" name="ssh2" object="noit.module.ssh2"/>
        </modules>
        <checks>
          <ssh module="ssh2">
            <check uuid="1cddb2a8-76ff-11dd-83c8-f75cb8b93bd9" target="10.1.2.3"/>
            <check uuid="1dd79110-76ff-11dd-9b54-739adc274a93" target="10.1.2.4"/>
            <check uuid="4627560a-76ff-11dd-941f-4b75679cb908" target="10.1.2.5"/>
            <check uuid="4fdcb8de-76ff-11dd-ae16-2740afc178ae" target="10.1.2.6"/>
          </ssh>
        </checks>
      </noit>
    ]]></programlisting>
    </example>
  </examples>
</module>
]=])
  return 0
end

function elapsed(check, name, starttime, endtime)
    local elapsedtime = endtime - starttime
    local seconds = string.format('%.3f', mtev.timeval.seconds(elapsedtime))
    check.metric_uint32(name, math.floor(seconds * 1000 + 0.5))
    return seconds
end 

local KEXALG = {
  -- TODO: Support RFC 4419 DH group exchange.
  -- Main difference is rather than fixed groups, client proposes min/max/pref
  -- group size in bits. Server then picks a group that best matches client
  -- request.
  -- 'diffie-hellman-group-exchange-sha256',
  -- 'diffie-hellman-group-exchange-sha1', 
  'diffie-hellman-group14-sha1',
  'diffie-hellman-group16-sha512',
  'diffie-hellman-group18-sha512',
  'diffie-hellman-group1-sha1'
}

local KEYALG = { 'ssh-rsa', 'ecdsa-sha2-nistp256', 'ssh-ed25519', 'ssh-dss' }

local ENCALG = {
  'aes128-cbc', 'aes192-cbc', 'aes256-cbc', 'aes128-ctr', 'aes192-ctr',
  'aes256-ctr', 'rijndael128-cbc', 'rijndael192-cbc', 'rijndael256-cbc',
  '3des-cbc', '3des-ecb', '3des-cfb', '3des-ofb', '3des-ctr',
  'blowfish-cbc', 'blowfish-ecb', 'blowfish-cfb', 'blowfish-ofb',
  'blowfish-ctr', 'twofish128-ctr', 'twofish128-cbc', 'twofish192-ctr',
  'twofish192-cbc', 'twofish256-ctr', 'twofish256-cbc', 'twofish-cbc',
  'twofish-ecb', 'twofish-cfb', 'twofish-ofb', 'cast128-cbc', 'cast128-ecb',
  'cast128-cfb', 'cast128-ofb', 'idea-cbc', 'idea-ecb', 'idea-cfb',
  'idea-ofb', 'arcfour', 'arcfour128', 'arcfour256' }

local MACALG = { 'hmac-sha1', 'hmac-md5', 'hmac-ripemd160', 'hmac-sha1-96',
  'hmac-md5-96', 'hmac-ripemd160-96', 'hmac-ripemd160@openssh.com' }

local BN_2
local MSG, DH_prime2409, DH_group14, DH_group16, DH_group18 -- these are big, assigned at the end

local KEX_DEFAULTS = {
  method_kex = table.concat(KEXALG, ','),
  method_hostkey = table.concat(KEYALG, ','),
  method_crypt_cs = table.concat(ENCALG, ','),
  method_crypt_sc = table.concat(ENCALG, ','),
  method_mac_cs = table.concat(MACALG, ','),
  method_mac_sc = table.concat(MACALG, ','),
  method_comp_cs = 'none',
  method_comp_sc = 'none',
  method_lang_cs = '',
  method_lang_sc = ''
}

local KEX_PACKET_PARTS = {
  'method_kex', 'method_hostkey',
  'method_crypt_cs', 'method_crypt_sc',
  'method_mac_cs', 'method_mac_sc',
  'method_comp_cs', 'method_comp_sc',
  'method_lang_cs', 'method_lang_sc'
}

function padding(len) return mtev.pseudo_rand_bytes(len) end

function packetize(data)
  local pad = 8 - ((data:len() + 4 + 1) % 8)
  if pad < 4 then pad = pad + 8 end
  return string.pack(">IcAA", 1 + data:len() + pad, pad, data, padding(pad))
end

function kexdhinit(exp)
  local bytes, bin = exp:num_bytes(), exp:tobin()
  local hasnohigh = (bit.band(bin:byte(1), 0x80) == 0)
  local _, neg = exp:is_negative()
  if neg then
    mtev.log("error", "ssh2 kexdhinit: exp is negative\n")
  end
  if hasnohigh then
    return string.pack(">cIA", MSG.KEXDH_INIT, bytes, bin)
  end
  return string.pack(">cIcA", MSG.KEXDH_INIT, bytes+1, 0, bin)
end

function parse_kexdhinit(check, buf)
  local decoded = {}
  decoded = { reply = buf:byte(1) }
  if decoded.reply ~= MSG.KEXDH_REPLY then return decoded end

  local r, hostkey, server_f, h_sig = string.unpack(buf, ">aaa", 2)
  local _, type, sig = string.unpack(h_sig, ">aa")
  local digest_sha1   = mtev.sha1(hostkey)
  local digest_sha256 = mtev.sha256(hostkey)

  decoded.type = type
  decoded.fingerprint = mtev.md5_hex(hostkey)
  decoded.fingerprint_sha1 = mtev.sha1_hex(hostkey)
  decoded.fingerprint_sha256 = mtev.sha256_hex(hostkey)
  decoded.fingerprint_sha1_base64 = mtev.base64_encode(digest_sha1)
  decoded.fingerprint_sha256_base64 = mtev.base64_encode(digest_sha256)

  -- strip any base64 padding to match OpenSSH behavior
  decoded.fingerprint_sha1_base64 = string.gsub(decoded.fingerprint_sha1_base64, "=", "")
  decoded.fingerprint_sha256_base64 = string.gsub(decoded.fingerprint_sha256_base64, "=", "")

  if type == 'ssh-rsa' then
    local _, _, _, n = string.unpack(hostkey, ">aaa")
    decoded.type = 'RSA'
    decoded.bits = mtev.bignum_bin2bn(n):num_bits()
  elseif type == 'ecdsa-sha2-nistp256' then
    decoded.type = 'ECDSA'

    -- ECDSA host key data, as unpacked above:
    --   (uint32_t) host key type len + (string) host key type 
    --   (uint32_t) EC curve ID len + (string) EC curve ID
    --   (uint32_t) ECDSA pubkey len + (string) ECDSA pubkey (Q)
    -- https://tools.ietf.org/html/rfc5656#section-3.1

    local _, key_type, ec_curve, qlen, comp = string.unpack(hostkey, ">aaIb")
    mtev.log("debug/ssh2", "ssh2 ECDSA key data: type: %s, EC curve: %s, Qlen: %d, compression: %d\n", key_type, ec_curve, qlen, comp)

    -- The first byte of Q indicates whether point compression is in use
    -- 0x2 or 0x3 indicates compression, so the remaining bytes are the X coordinate only
    -- 0x4 indicates no compression, and the remaining bytes are the X coordinate, followed by the Y coordinate.
    -- http://www.secg.org/sec1-v2.pdf, section 2.3.3
    -- Note that at least as of release 7.6, OpenSSH does not support encoding with point compression.

    if comp == 4 then
      decoded.bits = ((qlen-1)/2)*8
    else
      decoded.bits = (qlen-1)*8
    end
  elseif type == 'ssh-ed25519' then
    -- https://tools.ietf.org/html/draft-ietf-curdle-ssh-ed25519-01
    decoded.type = 'Ed25519'

    -- always 256 bits
    -- https://tools.ietf.org/html/rfc8032#section-5.1.5
    decoded.bits = 256
  elseif type == 'ssh-dss' then
    local _, _, p = string.unpack(hostkey, ">aa")
    decoded.type = 'DSA'
    decoded.bits = mtev.bignum_bin2bn(p):num_bits()
  end

  check.metric_string("key-type", decoded.type)
  check.metric_string("fingerprint", decoded.fingerprint)
  check.metric_string("fingerprint_sha1", decoded.fingerprint_sha1)
  check.metric_string("fingerprint_sha1_base64", decoded.fingerprint_sha1_base64)
  check.metric_string("fingerprint_sha256", decoded.fingerprint_sha256)
  check.metric_string("fingerprint_sha256_base64", decoded.fingerprint_sha256_base64)
  check.metric_int32("bits", decoded.bits)
  
  return decoded
end

function kex_fallback(conf, k) return conf[k] or KEX_DEFAULTS[k] or '' end
function kexinit(conf)
  local args = { ">cAaaaaaaaaaacI", MSG.KEXINIT, mtev.rand_bytes(16) }
  for _,v in pairs(KEX_PACKET_PARTS) do
    table.insert(args, kex_fallback(conf,v))
  end
  table.insert(args,0)
  table.insert(args,0)
  return string.pack(unpack(args))
end

function parse_kexinit(buf)
  local decoded = {}
  local pos, code = string.unpack(buf, ">c")
  if code ~= MSG.KEXINIT then return decoded end
  pos, decoded.cookie = string.unpack(buf, ">A16", pos)
  for _,k in pairs(KEX_PACKET_PARTS) do
    pos, decoded[k] = string.unpack(buf, ">a", pos)
  end
  return decoded
end

-- Reconnoiter functions

function init(module)
  BN_2 = mtev.bignum_dec2bn("2")
  DH_prime2409 = mtev.bignum_hex2bn(DH_prime2409)
  DH_group14 = mtev.bignum_hex2bn(DH_group14)
  DH_group16 = mtev.bignum_hex2bn(DH_group16)
  DH_group18 = mtev.bignum_hex2bn(DH_group18)
  return 0
end

function config(module, options)
  return 0
end

function initiate(module, check)
  local config = check.interpolate(check.config)
  config.port = config.port or 22
  local starttime = mtev.timeval.now()

  local cerr = function (msg)
    check.status(msg)
    check.metric_string("error", msg)
    return
  end

  local e = mtev.socket(check.target_ip)
  local rv, err = e:connect(check.target_ip, config.port);

  if rv ~= 0 then return cerr(err or "connect error") end

  local banner = e:read("\n")
  if banner == nil then return cerr("failed to read SSH banner") end

  local trimmed_banner = banner:gsub("%s+$", "")
  check.metric_string("banner", trimmed_banner)

  if e:write("SSH-2.0-Reconnoiter\r\n") < 0 then
    return cerr("failed to write SSH banner")
  end

  local payload = function()
    local pos, plen, pad
    local buf = e:read(4)
    if buf == nil or buf:len() ~= 4 then return nil, "bad pkt" end
    pos, plen = string.unpack(buf, ">I")
    buf = e:read(plen)
    if buf == nil or buf:len() ~= plen then return nil, "bad pkt" end
    pad = string.byte(buf)
    return string.sub(buf,2,plen-pad)
  end

  if e:write(packetize(kexinit(config))) < 0 then
    return cerr("failed starting key exchange")
  end

  buf, status = payload()
  if status ~= nil then return cerr(status) end
  local kexinfo = parse_kexinit(buf)

  mtev.log("debug/ssh2", "ssh2 KEX algorithms from server: %s\n", kexinfo.method_kex)

  local alg_prime, alg_q
  local savail = "," .. kexinfo.method_kex
  local cavail = "," .. kex_fallback(config, "method_kex")
  if nil ~= string.find(cavail, ",diffie[-]hellman[-]group14[-]") and
     nil ~= string.find(savail, ",diffie[-]hellman[-]group14[-]") then
    alg_prime = DH_group14
    alg_q = 2048
  elseif nil ~= string.find(cavail, ",diffie[-]hellman[-]group16[-]") and
         nil ~= string.find(savail, ",diffie[-]hellman[-]group16[-]") then
    alg_prime = DH_group16
    alg_q = 4096
  elseif nil ~= string.find(cavail, ",diffie[-]hellman[-]group18[-]") and
         nil ~= string.find(savail, ",diffie[-]hellman[-]group18[-]") then
    alg_prime = DH_group18
    alg_q = 8192
  elseif nil ~= string.find(cavail, ",diffie[-]hellman[-]group1[-]") and
         nil ~= string.find(savail, ",diffie[-]hellman[-]group1[-]") then
    alg_prime = DH_prime2409
    alg_q = 1024
  else
    return cerr("no shared KEX algorithms")
  end

  local x, exp = mtev.bignum_new(), mtev.bignum_new()

  x:rand(alg_q, 0, 0)

  if x:is_negative() then x:set_negative(0) end
  exp:mod_exp(BN_2, x, alg_prime)

  if e:write(packetize(kexdhinit(exp))) < 0 then
    return cerr("failed in DH key exchange")
  end

  buf, status = payload()
  if status ~= nil then return cerr(status) end

  local kexdhinfo = parse_kexdhinit(check, buf)

  local endtime = mtev.timeval.now()
  elapsed(check, "duration", starttime, endtime)
  if kexdhinfo.fingerprint ~= nil then
    check.status(kexdhinfo.fingerprint)
    check.available()
    check.good()
  else
    check.unavailable()
    check.bad()
  end
end

MSG = {
  DISCONNECT = 1,
  IGNORE = 2,
  UNIMPLEMENTED = 3,
  DEBUG = 4,
  SERVICE_REQUEST = 5,
  SERVICE_ACCEPT = 6,
  KEXINIT = 20,
  NEWKEYS = 21,
  KEXDH_INIT = 30,
  KEXDH_REPLY = 31,
  KEXDH_GEX_REQUEST_OLD = 30, -- repeat
  KEXDH_GEX_GROUP = 31,       -- repeat
  KEXDH_GEX_INIT = 32,
  KEXDH_GEX_REPLY = 33,
  KEXDH_GEX_REQUEST = 34,
  LAST_KEX_PACKET = 49,
  FIRST_SERVICE_PACKET = 50,
  USERAUTH_REQUEST = 50,
  USERAUTH_FAILURE = 51,
  USERAUTH_SUCCESS = 52,
  USERAUTH_BANNER = 53,
  _FIRST_USERAUTH_METHOD_PACKET = 60,
  USERAUTH_PASSWD_CHANGEREQ = 60,
  USERAUTH_CHALLENGE = 60,
  USERAUTH_SECURID_CHALLENGE = 60,
  USERAUTH_SECURID_NEW_PIN_REQD = 61,
  _LAST_USERAUTH_METHOD_PACKET = 79,
  GLOBAL_REQUEST = 80,
  REQUEST_SUCCESS = 81,
  REQUEST_FAILURE = 82,
  CHANNEL_OPEN = 90,
  CHANNEL_OPEN_CONFIRMATION = 91,
  CHANNEL_OPEN_FAILURE = 92,
  CHANNEL_WINDOW_ADJUST = 93,
  CHANNEL_DATA = 94,
  CHANNEL_EXTENDED_DATA = 95,
  CHANNEL_EOF = 96,
  CHANNEL_CLOSE = 97,
  CHANNEL_REQUEST = 98,
  CHANNEL_SUCCESS = 99,
  CHANNEL_FAILURE = 100,
  RESERVED = 255
}

-- RFC 2409
DH_prime2409 = "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E\z
088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F\z
14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE\z
386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381FFFFFFFFFFFFFFFF"

-- RFC 3526, 2048-bit MODP, "group 14"
DH_group14   = "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E\z
088A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F\z
14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE\z
386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA4836\z
1C55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED52907709696\z
6D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B27\z
83A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898\z
FA051015728E5A8AACAA68FFFFFFFFFFFFFFFF"

-- RFC 3526, 4096-bit MODP, "group 16"
DH_group16 = "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E08\z
8A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14\z
374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE38\z
6BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C\z
55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D\z
670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783\z
A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA\z
051015728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64ECFB850458DBEF0A8A\z
EA71575D060C7DB3970F85A6E1E4C7ABF5AE8CDB0933D71E8C94E04A25619DCEE3D226\z
1AD2EE6BF12FFA06D98A0864D87602733EC86A64521F2B18177B200CBBE117577A615D\z
6C770988C0BAD946E208E24FA074E5AB3143DB5BFCE0FD108E4B82D120A92108011A72\z
3C12A787E6D788719A10BDBA5B2699C327186AF4E23C1A946834B6150BDA2583E9CA2A\z
D44CE8DBBBC2DB04DE8EF92E8EFC141FBECAA6287C59474E6BC05D99B2964FA090C3A2\z
233BA186515BE7ED1F612970CEE2D7AFB81BDD762170481CD0069127D5B05AA993B4EA\z
988D8FDDC186FFB7DC90A6C08F4DF435C934063199FFFFFFFFFFFFFFFF"

-- RFC 3526, 8192-bit MODP, "group 18"
DH_group18 = "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E08\z
8A67CC74020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14\z
374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE38\z
6BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C\z
55D39A69163FA8FD24CF5F83655D23DCA3AD961C62F356208552BB9ED529077096966D\z
670C354E4ABC9804F1746C08CA18217C32905E462E36CE3BE39E772C180E86039B2783\z
A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA\z
051015728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64ECFB850458DBEF0A8A\z
EA71575D060C7DB3970F85A6E1E4C7ABF5AE8CDB0933D71E8C94E04A25619DCEE3D226\z
1AD2EE6BF12FFA06D98A0864D87602733EC86A64521F2B18177B200CBBE117577A615D\z
6C770988C0BAD946E208E24FA074E5AB3143DB5BFCE0FD108E4B82D120A92108011A72\z
3C12A787E6D788719A10BDBA5B2699C327186AF4E23C1A946834B6150BDA2583E9CA2A\z
D44CE8DBBBC2DB04DE8EF92E8EFC141FBECAA6287C59474E6BC05D99B2964FA090C3A2\z
233BA186515BE7ED1F612970CEE2D7AFB81BDD762170481CD0069127D5B05AA993B4EA\z
988D8FDDC186FFB7DC90A6C08F4DF435C93402849236C3FAB4D27C7026C1D4DCB26026\z
46DEC9751E763DBA37BDF8FF9406AD9E530EE5DB382F413001AEB06A53ED9027D83117\z
9727B0865A8918DA3EDBEBCF9B14ED44CE6CBACED4BB1BDB7F1447E6CC254B33205151\z
2BD7AF426FB8F401378CD2BF5983CA01C64B92ECF032EA15D1721D03F482D7CE6E74FE\z
F6D55E702F46980C82B5A84031900B1C9E59E7C97FBEC7E8F323A97A7E36CC88BE0F1D\z
45B7FF585AC54BD407B22B4154AACC8F6D7EBF48E1D814CC5ED20F8037E0A79715EEF2\z
9BE32806A1D58BB7C5DA76F550AA3D8A1FBFF0EB19CCB1A313D55CDA56C9EC2EF29632\z
387FE8D76E3C0468043E8F663F4860EE12BF2D5B0B7474D6E694F91E6DBE115974A392\z
6F12FEE5E438777CB6A932DF8CD8BEC4D073B931BA3BC832B68D9DD300741FA7BF8AFC\z
47ED2576F6936BA424663AAB639C5AE4F5683423B4742BF1C978238F16CBE39D652DE3\z
FDB8BEFC848AD922222E04A4037C0713EB57A81A23F0C73473FC646CEA306B4BCBC886\z
2F8385DDFA9D4B7FA2C087E879683303ED5BDD3A062B3CF5B3A278A66D2A13F83F44F8\z
2DDF310EE074AB6A364597E899A0255DC164F31CC50846851DF9AB48195DED7EA1B1D5\z
10BD7EE74D73FAF36BC31ECFA268359046F4EB879F924009438B481C6CD7889A002ED5\z
EE382BC9190DA6FC026E479558E4475677E9AA9E3050E2765694DFC81F56E880B96E71\z
60C980DD98EDD3DFFFFFFFFFFFFFFFFF"
