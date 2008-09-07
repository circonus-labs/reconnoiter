rc = 0

function extract(file, docsdir)
  local module = file:gsub('.lua$', ''):gsub('/', '.')
  local M = require(module)
  if not M or not M.onload then
    return
  end
  M.onload( {
    xml_description =
      function(xml)
        local f = io.open(docsdir .. "/" .. module .. ".xml", "w+")
        if not f then
          rc = 2
        end
        f:write(xml)
        f:close()
      end
  })
end


if (#arg) ~= 2 then
  print(string.format("%s <module> <docs directory>\n", (arg[0])))
  os.exit(1)
end

extract(arg[1], arg[2])
os.exit(rc)
