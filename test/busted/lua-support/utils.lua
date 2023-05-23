module(..., package.seeall)
lfs = require('lfs')

function dir_descend(file, func)
  local st = mtev.stat(file)
  if st == nil then return end
  if bit.band(st.mode, S_IFDIR) ~= 0 then
    mtev.readdir(file, function(entry)
      if entry == "." or entry == ".." then return false end
      dir_descend(file .. "/" .. entry, func)
      return false
    end)
  end
  func(file, st)
end

function rm_rf(file)
  dir_descend(file, function(file, st)
    if bit.band(st.mode, S_IFDIR) ~= 0 then
      mtev.rmdir(file)
    else
      mtev.unlink(file)
    end
  end)
end

function path_contains(bin)
  local path = os.getenv('PATH') or ''
  for i,dir in ipairs(path:split(":")) do
    local st = mtev.stat(dir .. "/" .. bin)
    if st ~= nil and bit.band(st.mode, tonumber('111',8)) ~= 0 then
      return true
    end
  end
  return false
end

postgres_bins = { 'pg_ctl', 'initdb', 'psql', 'postgres' }
function postgres_reqs(bins)
  if bins == nil then bins = postgres_bins end
  local out = {}
  for i,bin in ipairs(bins) do
    if path_contains(bin) then table.insert(out, bin) end
  end
  for i,bin in ipairs(bins) do
    if out[i] == nil then return false end
  end
  return true
end

function getDebuggerPath()
  local home_dir = os.getenv("HOME") or os.getenv("USERPROFILE")
  local ide_dir
  if lfs.attributes(home_dir .. "/.vscode") then
    ide_dir = ".vscode"
  elseif lfs.attributes(home_dir .. "/.vscode-server") then
    ide_dir = ".vscode-server"
  else
    return nil
  end

  local extension_dir = home_dir .. "/" .. ide_dir .. "/extensions/"
  for file in lfs.dir(extension_dir) do
    local matcher = string.match(file, "tomblind%.local%-lua%-debugger%-vscode%-(.*)")
    if matcher then
      return extension_dir .. file .. "/debugger/lldebugger.lua"
    end
  end
  return nil
end

function enableLuaDebugging()
  -- requires the vscode extension "tomblind.local-lua-debugger-vscode"
  -- https://github.com/circonus/circonus-wiki/blob/master/Engineering/AdvancedDebugAndTest.md#lua-debugging
  if os.getenv("LOCAL_LUA_DEBUGGER_VSCODE") == "1" then
    io.write("Enabling lua debugging...\n")
    require(utils.getDebuggerPath()).start()
  end
end