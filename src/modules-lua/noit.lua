local pairs = _G.pairs
local mtev = require("mtev")
local noitClib = require("noit_binding");
local _P = package.loaded

module("noit")

-- Map all of mtev into here

for k,v in pairs(mtev) do
  _P.noit[k] = v
end

for k,v in pairs(noitClib) do
  _P.noit[k] = v
end

local _default_ca_chain
_P.noit.default_ca_chain = function()
  if _default_ca_chain == nil then
    _default_ca_chain = mtev.shared_get('noit-default-ca-chain')
    if _default_ca_chain == nil then
      _default_ca_chain = mtev.conf_get_string("/noit/eventer/config/default_ca_chain")
      mtev.shared_set("noit-default-ca-chain", _default_ca_chain)
    end
  end
  return _default_ca_chain
end
