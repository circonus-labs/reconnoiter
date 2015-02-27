local pairs = _G.pairs
local mtev = require("mtev")
local noitClib = require("noit_lua");
local _P = package.loaded

module("noit")

-- Map all of mtev into here

for k,v in pairs(mtev) do
  _P.noit[k] = v
end

for k,v in pairs(noitClib) do
  _P.noit[k] = v
end
