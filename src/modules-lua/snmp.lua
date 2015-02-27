local pairs = _G.pairs
local mtev = require("mtev")
local snmpClib = require("noit_lua_snmp")
local _P = package.loaded

module("snmp")

if snmpClib ~= nil then
  for k,v in pairs(snmpClib) do
mtev.log("error", " --> %s : %s\n", k, v)
    _P.snmp[k] = v
  end
end
