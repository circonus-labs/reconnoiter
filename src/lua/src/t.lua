local a = "%34"
a = a:gsub("%%(%x%x)", function(s) return string.char(tonumber(s,16)) end)
print(a)
