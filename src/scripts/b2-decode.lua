#!/usr/local/bin/luajit
local ffi = require 'ffi'
local noitll = ffi.load 'noit'

ffi.cdef [[
  int noit_check_log_b_to_sm(const char *, int, char ***, int);
  void free(void *);
]]

local usage = [[
Decode and print B2 records that are passed to stdin.

Options:
  --drop_B2    drop original B2 records from stream
  --drop_other drop non B2 messages
  --extend     convert decoded parts into full M records by inserting the IP as second field
]]

function shift() return table.remove(arg, 1) end

local drop_B2 = false
local drop_other = false
local extend = false

while #arg > 0 do
   local key = shift()
   if key == "--drop_B2" then
      drop_B2 = true
   elseif key == "--drop_other" then
      drop_other = true
   elseif key == "--extend" then
      extend = true
   else
      print(usage)
      os.exit(1)
   end
end

local function b2_decode(line)
   local charptrptr = ffi.new("char**[?]", 1)
   local cnt = noitll.noit_check_log_b_to_sm(line, line:len(), charptrptr, 1)
   local records = {}
   if cnt > 0 then
      for i = 0, cnt-1 do
         records[i+1] = ffi.string(charptrptr[0][i])
         ffi.C.free(charptrptr[0][i])
      end
      ffi.C.free(charptrptr[0])
   end
   return records
end

while (true) do
   local line = io.read()
   if not line then break end
   if line:match("^B2\t") then
      if not drop_B2 then print(line) end
      for _, record in ipairs(b2_decode(line)) do
         if extend then
            local IP = line:gsub("^[^\t]+\t([^\t]+)\t.*", "%1")
            print(record:gsub("^([^\t]+)\t", "%1\t".. IP .. "\t"))
         else
            print(record)
         end
      end
   else -- no B2 record
      if not drop_other then print(line) end
   end
end
