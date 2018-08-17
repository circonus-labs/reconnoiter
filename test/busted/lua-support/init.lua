_J = function(t) return mtev.tojson(t):tostring() end

function string:split( inSplitPattern, outResults )
   if not outResults then
      outResults = { }
   end
   local theStart = 1
   local theSplitStart, theSplitEnd = string.find( self, inSplitPattern, 
theStart )
   while theSplitStart do
      table.insert( outResults, string.sub( self, theStart, theSplitStart-1 ) )
      theStart = theSplitEnd + 1
      theSplitStart, theSplitEnd = string.find( self, inSplitPattern, theStart )
   end
   table.insert( outResults, string.sub( self, theStart ) )
   return outResults
end

plpretty = require 'pl.pretty'
utils = require('lua-support/utils.lua')
Reconnoiter = require('lua-support/reconnoiter.lua')
Stomp = require('lua-support/stomp.lua')
