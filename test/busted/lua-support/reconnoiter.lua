module(..., package.seeall)

local cwd = mtev.getcwd()

function test_workspace()
  return cwd .. "/" .. find_test_dir() .. "/workspace"
end

function clean_workspace()
  utils.rm_rf(test_workspace())
end

local O_NEW = bit.bor(O_CREAT,bit.bor(O_TRUNC,O_WRONLY))
local noitd = cwd .. '/../../src/noitd'
local stratcond = cwd .. '/../../src/stratcond'
local system = run_command_synchronously_return_output
local pgmoon = require 'pgmoon'

local inheritsFrom = require('lua-support/inherits.lua')

function ssl_file(file)
  return cwd .. '/../' .. file
end

local MBAPI = API
API = {}
API.__index = API;

function API:new(port, cert)
  local obj = { port = port }
  obj.sslconfig = {
    certificate_file = cwd .. '/../' .. cert .. '.crt',
    key_file = cwd .. '/../' .. cert .. '.key',
    ca_chain = cwd .. '/../test-ca.crt',
  }
  obj.api = MBAPI:new("127.0.0.1", obj.port):ssl(obj.sslconfig)
  obj.xmlapi = MBAPI:new("127.0.0.1", obj.port):headers({accept = "text/xml"}):ssl(obj.sslconfig)
  obj.curl_count = 0
  setmetatable(obj, API)
  return obj
end

local identity = function(...) return ... end

function API:set_curl_log(curl_log_folder, curl_node_name, curl_log)
  self.curl_log_folder = curl_log_folder
  self.curl_node_name = curl_node_name
  self.curl_log = curl_log
end

function API:curl_logger(headers, method, uri, payload)
  local payload_is_binary = false
  if self.curl_log ~= nil then
    assert(self.curl_log:write("curl -k --compressed -X " .. method .. " "))
    for k,v in pairs(headers) do
      assert(self.curl_log:write("-H '" .. k .. ": " .. v .. "' "))
      if string.lower(k) == "content-type" and string.find(string.lower(v), "flatbuffer") then
        payload_is_binary=true
      end
    end
    if payload then
      if payload_is_binary then
        local curl_binary_filename = self.curl_log_folder .. '/curl_binary_' ..
                                     self.curl_node_name .. self.curl_count .. '.bin'
        self.curl_count = self.curl_count + 1
        local curl_binary_file = assert(io.open(curl_binary_filename, 'wb'))
        assert(curl_binary_file:write(payload))
        assert(curl_binary_file:close())
        assert(self.curl_log:write("--data-binary '@" .. curl_binary_filename .. "' "))
      else
        if type(payload) == "table" then
          payload = mtev.tojson(payload):tostring()
        else
          payload = tostring(payload)
        end
        payload = string.gsub(payload, '\n', '\\n')
        assert(self.curl_log:write("-d $'" .. payload .. "' "))
      end
    end
    assert(self.curl_log:write("-w '\\n' "))
    assert(self.curl_log:write("'https://127.0.0.1:" .. self.port .. uri .. "'\n"))
  end
end

function API:json(method, uri, payload, _pp, config)
  self:curl_logger({}, method, uri, payload)
  return self.api:HTTPS(method, uri, payload, _pp, config)
end

function API:xml(method, uri, payload, _pp, config)
  if _pp == nil then
    _pp = function(result)
      local doc = mtev.parsexml(result)
      return doc
    end
  end
  self:curl_logger({accept = "text/xml"}, method, uri, payload)
  return self.xmlapi:HTTPS(method, uri, payload, _pp, config)
end

function API:raw(method, uri, payload, _pp, config)
  if _pp == nil then _pp = identity end
  self:curl_logger({}, method, uri, payload)
  return self.api:HTTPS(method, uri, payload, _pp, config)
end

local function mtevenv()
  local preserve = { LD_LIBRARY_PATH=1, UMEM_DEBUG=1 }
  local copy = {}
  for k,v in pairs(ENV) do
    local valid_key = false
    if preserve[k] then
      valid_key = true
    elseif string.sub(k, 1, 5) == "ASAN_" then
      valid_key = true
    end
    if valid_key then
      copy[k] = v
    end
  end
  copy['UMEM_DEBUG'] = 'default'
  return copy
end


local os = require('os')
local default_filterset = {
  allowall = { { type = "allow" } }
}
local all_noit_loaders = function()
  local r1, MTEV_MODULES_DIR = system({
    argv = { 'mtev-config', '--modules-dir' },
    timeout = 2,
  })
  local r2, MTEV_LIB_DIR = system({
    argv = { 'mtev-config', '--libdir' },
    timeout = 2,
  })
  if r1 ~= 0 or r2 ~= 0 then
    mtev.log("error", "Cannot run `mtev-config`\n")
  end
  return {
  lua = { image = 'lua',
          name = 'lua',
          config = {
             directory = MTEV_MODULES_DIR .. "/lua/?.lua;{CWD}/../../src/modules-lua/?.lua",
             cpath = MTEV_LIB_DIR .. "/mtev_lua/?.so;{CWD}/../../src/modules/noit_lua/?.so"
           }
         }
  }
end
local all_noit_generics = {
  histogram = { image = 'histogram' },
  lua_web = { image = 'lua_mtev' }
}
local all_noit_modules = {
  selfcheck = { image = 'selfcheck' },
  ping_icmp = { image = 'ping_icmp' },
  snmp = { image = 'snmp' },
  test_abort = { image = 'test_abort' },
  varnish = { loader = 'lua', object = 'noit.module.varnish' },
  http = { loader = 'lua', object = 'noit.module.http' },
  resmon = { loader = 'lua', object = 'noit.module.resmon' },
  smtp = { loader = 'lua', object = 'noit.module.smtp' },
  tcp = { loader = 'lua', object = 'noit.module.tcp' },
}

local NOIT_TEST_DB = function()
  return cwd .. "/" .. find_test_dir() .. "/workspace/test-db"
end
local NOIT_TEST_DB_PORT = 23816 +
    (os.getenv('BUILD_NUMBER') or 0) % 10000

TestConfig = {}
TestConfig.__index = TestConfig

function TestConfig:new(name, opts)
  local obj = {}
  obj.name = name
  obj.opts = opts or {}
  local jitter = math.floor(math.random() * 10000 / 10) * 10
  obj.NOIT_API_PORT = 42364 + jitter
  obj.NOIT_CLI_PORT = 42365 + jitter
  obj.STRATCON_API_PORT = 42366 + jitter
  obj.STRATCON_CLI_PORT = 42367 + jitter
  obj.STRATCON_WEB_PORT = 42368 + jitter
  obj.STOMP_PORT = 42369 + jitter
  setmetatable(obj, TestConfig)
  return obj
end

function TestConfig:set_verbose(level)
  self.verbose = level
end
function TestConfig:L(err)
  if self.verbose ~= nil and self.verbose > 1 then mtevL("stderr", "%s\n", err) end
end

local pgmoon = require('pgmoon')
function pgclient(db,user)
  local conn = pgmoon.new({
    host = 'localhost',
    port = NOIT_TEST_DB_PORT,
    database = db or 'postgres',
    user = user or os.getenv('USER')
  })
  conn:connect()
  return conn
end

function TestConfig:make_eventer_config(fd, opts)
  if not opts['eventer_config'] then opts['eventer_config'] = {} end
  if not opts['eventer_config']['default_queue_threads'] then
    opts['eventer_config']['default_queue_threads'] = 10
  end
  if not opts['eventer_config']['default_ca_chain'] then
    opts['eventer_config']['default_ca_chain'] = cwd .. "/../test-ca.crt"
  end
  local out = "\n" ..
  "<eventer>\n" ..
  "  <config>\n" ..
  "    <default_queue_threads>" .. opts['eventer_config']['default_queue_threads'] .. "</default_queue_threads>\n" ..
  "    <default_ca_chain>" .. opts['eventer_config']['default_ca_chain'] .. "</default_ca_chain>\n" ..
  "    <ssl_dhparam1024_file/>\n" ..
  "    <ssl_dhparam2048_file/>\n" ..
  "  </config>\n" ..
  "</eventer>\n"
  mtev.write(fd, out)
end

function TestConfig:make_rest_acls(fd, opts)
  local acls = opts['rest_acls']
  mtev.write(fd, "  <rest>\n")
  for i, acl in ipairs(acls) do
    mtev.write(fd,"    <acl")
    if acl.type ~= nil then mtev.write(fd, ' type="' .. acl.type .. '"') end
    if acl.cn ~= nil then mtev.write(fd, ' cn="' .. acl.cn .. '"') end
    if acl.url ~= nil then mtev.write(fd, ' url="' .. acl.url .. '"') end
    mtev.write(fd,">\n")
    for j, rule in ipairs(acl.rules) do
      mtev.write(fd,"      <rule")
      if rule.type ~= nil then mtev.write(fd, ' type="' .. rule.type .. '"') end
      if rule.cn ~= nil then mtev.write(fd, ' cn="' .. rule.cn .. '"') end
      if rule.url ~= nil then mtev.write(fd, ' url="' .. rule.url .. '"') end
      mtev.write(fd,"/>\n")
    end
    mtev.write(fd,"    </acl>\n")
  end
  mtev.write(fd,"  </rest>\n")
end

function TestConfig:make_log_section(fd, type, dis)
  mtev.write(fd,"      <" .. type .. ">\n" ..
                "        <outlet name=\"error\"/>\n")
  for t,d in pairs(dis) do
    if t ~= nil and string.len(t) > 0 then
      mtev.write(fd,"        <log name=\"" .. type .. "/" .. t .. "\" disabled=\"" .. d .. "\"/>\n")
    end
  end
  mtev.write(fd,"      </" .. type .. ">\n")
end

function TestConfig:make_logs_config(fd, opts)
  local logtypes =
    { collectd = "false", dns = "false", eventer = "true",
      external = "false", lua = "false", mysql = "false",
      ping_icmp = "false", postgres = "false", selfcheck = "false",
      snmp = "false", ssh2 = "false", listener = "true",
      datastore = "false", memory = "true", http = "true" }
  -- These are disabled attrs, so they look backwards
  if opts.logs_error == nil then
    opts.logs_error = { }
    opts.logs_error[''] = 'false'
  end
  if opts.logs_debug == nil then
    opts.logs_debug = { }
    opts.logs_debug[''] = 'false'
  end

  -- Listener is special, we need that for boot availability detection
  if opts.logs_debug.listener == nil then
    opts.logs_debug.listener = 'false'
  end
  -- As is datastore
  if opts.logs_debug.datastore == nil then
    opts.logs_debug.datastore = 'false'
  end

  for type, default in pairs(logtypes) do
    if opts.logs_error[type] == nil then
      opts.logs_error[type] = 'false'
    end
    if opts.logs_debug[type] == nil then
      opts.logs_debug[type] = default
    end
  end

  local buff = "\n" ..
  "<logs>\n" ..
  "  <console_output>\n" ..
  "    <outlet name=\"stderr\"/>\n" ..
  "    <log name=\"error\" debug=\"true\" disabled=\"" .. opts['logs_error'][''] .. "\" timestamps=\"true\"/>\n" ..
  "    <log name=\"debug\" debug=\"true\" disabled=\"" .. opts['logs_debug'][''] .. "\" timestamps=\"true\"/>\n" ..
  "    <log name=\"http/access\" debug=\"true\" disabled=\"false\" timestamps=\"true\"/>\n" ..
  "  </console_output>\n" ..
  "  <feeds>\n" ..
  "    <log name=\"feed\" type=\"jlog\" path=\"" .. opts.workspace .. "/logs/" .. opts.name .. ".feed(*)\"/>\n" ..
  "  </feeds>\n" ..
  "  <components>\n"
  mtev.write(fd, buff)
  self:make_log_section(fd, 'error', opts['logs_error'])
	self:make_log_section(fd, 'debug', opts['logs_debug'])
  mtev.write(fd,"\n" ..
  "  </components>\n" ..
  "  <feeds>\n" ..
  "    <config><extended_id>on</extended_id></config>\n" ..
  "    <outlet name=\"feed\"/>\n" ..
  "    <log name=\"config\"/>\n" ..
  "    <also>\n" ..
  "    <outlet name=\"error\"/>\n" ..
  "    <log name=\"bundle\"/>\n" ..
  "    <log name=\"check\"/>\n" ..
  "    <log name=\"status\"/>\n" ..
  "    <log name=\"metrics\"/>\n" ..
  "    </also>\n" ..
  "  </feeds>\n" ..
  "</logs>\n")
end

function TestConfig:make_modules_config(fd, opts)
  mtev.write(fd,"\n" ..
  "<modules directory=\"" .. cwd .. "/../../src/modules\">\n")
  for k, loader in pairs(opts.loaders or {}) do
    mtev.write(fd, "    <loader")
    if loader.image ~= nil then
      mtev.write(fd, " image=\"" .. loader.image .. "\"")
    end
    mtev.write(fd, " name=\"" .. k .. "\">\n")
    if loader.config ~= nil then
      mtev.write(fd, "      <config>\n")
      for name,value in pairs(loader.config) do
        value = string.gsub(value, "{CWD}", cwd)
        mtev.write(fd, "        <" .. name .. ">" .. value .. "</" .. name .. ">\n")
      end
      mtev.write(fd, "      </config>\n")
    end
    mtev.write(fd, " </loader>\n")
  end
  for k, generic in pairs(opts.generics or {}) do
    mtev.write(fd, "    <generic")
    if generic.image ~= nil then
      mtev.write(fd, " image=\"" .. generic.image .. "\"")
    end
    mtev.write(fd, " name=\"" .. k .. "\">\n")
    if generic.config ~= nil then
      mtev.write(fd, "      <config>\n")
      for name, value in pairs(generic.config) do
        mtev.write(fd, "        <" .. name .. ">" .. value ..
                         "</" .. name .. ">\n")
      end
      mtev.write(fd, "      </config>\n")
    end
    mtev.write(fd, "</generic>\n")
  end
  for k, module in pairs(opts.modules or {}) do
    mtev.write(fd, "    <module")
    if module.image ~= nil then
      mtev.write(fd, " image=\"" .. module.image .. "\"")
    end
    if module.loader ~= nil then
      mtev.write(fd, " loader=\"" .. module.loader .. "\"")
    end
    if module.object ~= nil then
      mtev.write(fd, " object=\"" .. module.object .. "\"")
    end
    mtev.write(fd, " name=\"" .. k .. "\">\n")
    if module.config ~= nil then
      mtev.write(fd, "      <config>\n")
      for name, value in pairs(module.config) do
        mtev.write(fd, "        <" .. name .. ">" .. value ..
                         "</" .. name .. ">\n")
      end
      mtev.write(fd, "      </config>\n")
    end
    mtev.write(fd, "</module>\n")
  end
  mtev.write(fd, "</modules>\n")
end

function TestConfig:make_noit_listeners_config(fd, opts)
  if opts.noit_api_port == nil then
    opts.noit_api_port = self.NOIT_API_PORT
  end
  if opts.noit_cli_port == nil then
    opts.noit_cli_port = self.NOIT_CLI_PORT
  end
  mtev.write(fd, "\n" ..
  "<listeners>\n" ..
  "  <sslconfig>\n" ..
  "    <optional_no_ca>false</optional_no_ca>\n" ..
  "    <certificate_file>" .. cwd .. "/../test-noit.crt</certificate_file>\n" ..
  "    <key_file>" .. cwd .. "/../test-noit.key</key_file>\n" ..
  "    <ca_chain>" .. cwd .. "/../test-ca.crt</ca_chain>\n" ..
  "    <crl>" .. cwd .. "/../test-ca.crl</crl>\n" ..
  "  </sslconfig>\n" ..
  "  <consoles type=\"noit_console\">\n" ..
  "    <listener address=\"*\" port=\"".. opts['noit_cli_port'] .. "\">\n" ..
  "      <config>\n" ..
  "        <line_protocol>telnet</line_protocol>\n" ..
  "      </config>\n" ..
  "    </listener>\n" ..
  "  </consoles>\n" ..
  "  <api>\n" ..
  "    <config>\n" ..
  "      <log_transit_feed_name>feed</log_transit_feed_name>\n" ..
  "    </config>\n" ..
  "    <listener type=\"control_dispatch\" address=\"inet6:*\" port=\"" .. opts['noit_api_port'] .. "\" ssl=\"on\"/>\n" ..
  "    <listener type=\"control_dispatch\" address=\"inet:*\" port=\"" .. opts['noit_api_port'] .. "\" ssl=\"on\"/>\n" ..
  "  </api>\n" ..
  "</listeners>\n")
end

function TestConfig:do_check_print(fd, tree)
  if tree == nil then return end
  for i, node in ipairs(tree) do
    mtev.write(fd, "<" .. node[1])
    for k,v in pairs(node[2]) do
      mtev.write(fd, " " .. k .. "=\"" .. node[1][k] .. "\"")
    end
    if node[3] ~= nil then
      mtev.write(fd,">\n")
      self:do_check_print(fd, node[3])
      mtev.write(fd,"</check>\n")
    else
      mtev.write(fd,"/>\n")
    end
  end
end

function TestConfig:make_checks_config(fd, opts)
  mtev.write(fd,"  <checks minimum_period=\"100\" max_initial_stutter=\"10\" filterset=\"default\">\n")
  self:do_check_print(fd, opts.checks)
  mtev.write(fd,"  </checks>\n")
end

function TestConfig:make_clusters_config(fd, opts)
  mtev.write(fd, "<clusters/>\n")
end

function TestConfig:make_filtersets_config(fd, opts)
  mtev.write(fd,"<filtersets>\n")
  for name, set in pairs(opts.filtersets or {}) do
    mtev.write(fd,"  <filterset name=\"" .. name .. "\">\n")
    for i, rule in ipairs(set) do
      mtev.write(fd,"    <rule")
      for k,v in pairs(rule) do
        mtev.write(fd, " " .. k .. "=\"" .. v .. "\"")
      end
      mtev.write(fd,"/>\n")
    end
    mtev.write(fd,"  </filterset>\n")
  end
  mtev.write(fd,"</filtersets>\n")
end

function TestConfig:make_noit_config(name, opts)
  if name == nil then name = self.name end
  if opts == nil then opts = self.opts end
  if opts.name == nil then opts.name = name end
  if opts.modules == nil then opts.modules = all_noit_modules end
  if opts.generics == nil then opts.generics = all_noit_generics end
  if opts.loaders == nil then opts.loaders = all_noit_loaders() end
  if opts.filtersets == nil then opts.filtersets = default_filterset end
  if opts.rest_acls == nil then
    opts.rest_acls = { { type = 'deny', rules = { { type = 'allow' } } } }
  end
  local logroot = opts.workspace .. "/logs/"
  local st = mtev.stat(logroot)
  if st == nil then mtev.mkdir_for_file(logroot .. '/logfile', tonumber('777',8)) end
  local file = opts.workspace .. "/" .. name .. ".conf"
  local fd, errno, err = mtev.open(file, O_NEW, tonumber('644', 8))
  mtev.write(fd, "<?xml version=\"1.0\" encoding=\"utf8\" standalone=\"yes\"?>\n")
  -- Speed up recycling for shorter tests.
  mtev.write(fd, "<noit check_recycle_period=\"1000\">\n")
  mtev.write(fd, "<jlog><default_mseconds_between_batches>500</default_mseconds_between_batches></jlog>\n")
  self:make_eventer_config(fd, opts)
  self:make_rest_acls(fd, opts)
  self:make_logs_config(fd, opts)
  self:make_modules_config(fd, opts)
  self:make_noit_listeners_config(fd, opts)
  self:make_checks_config(fd, opts)
  self:make_filtersets_config(fd, opts)
  self:make_clusters_config(fd, opts)
  mtev.write(fd,"</noit>\n")
  mtev.close(fd)
  return file
end

function TestConfig:make_stratcon_noits_config(fd, opts)
  if opts.noit_api_port == nil then
    opts.noit_api_port = self.NOIT_API_PORT
  end
  mtev.write(fd,"\n" ..
  "<noits>\n" ..
  "  <sslconfig>\n" ..
  "    <certificate_file>" .. cwd .. "/../test-stratcon.crt</certificate_file>\n" ..
  "    <key_file>" .. cwd .. "/../test-stratcon.key</key_file>\n" ..
  "    <ca_chain>" .. cwd .. "/../test-ca.crt</ca_chain>\n" ..
  "  </sslconfig>\n" ..
  "  <config>\n" ..
  "    <reconnect_initial_interval>1000</reconnect_initial_interval>\n" ..
  "    <reconnect_maximum_interval>15000</reconnect_maximum_interval>\n" ..
  "  </config>\n")
  if opts.noits == nil then opts.noits = {} end
  for i, n in ipairs(opts.noits) do
    mtev.write(fd,"    <noit");
    for k,v in pairs(n) do
      mtev.write(fd," " .. k .. "=\"" .. v .. "\"")
    end
    mtev.write(fd,"/>\n")
  end
  mtev.write(fd,"</noits>\n")
end

function TestConfig:make_stratcon_listeners_config(fd,opts)
  if opts.stratcon_api_port == nil then
    opts.stratcon_api_port = self.STRATCON_API_PORT
  end
  if opts.stratcon_web_port == nil then
    opts.stratcon_web_port = self.STRATCON_WEB_PORT
  end
  mtev.write(fd,"\n" ..
  "<listeners>\n" ..
  "  <sslconfig>\n" ..
  "    <certificate_file>" .. cwd .. "/../test-stratcon.crt</certificate_file>\n" ..
  "    <key_file>" .. cwd .. "/../test-stratcon.key</key_file>\n" ..
  "    <ca_chain>" .. cwd .. "/../test-ca.crt</ca_chain>\n" ..
  "  </sslconfig>\n" ..
  "  <realtime type=\"http_rest_api\">\n" ..
  "    <listener address=\"*\" port=\"" .. opts.stratcon_web_port .. "\">\n" ..
  "      <config>\n" ..
  "        <hostname>stratcon.noit.example.com</hostname>\n" ..
  "        <document_domain>noit.example.com</document_domain>\n" ..
  "      </config>\n" ..
  "    </listener>\n" ..
  "  </realtime>\n" ..
  "  <listener type=\"control_dispatch\" address=\"*\" port=\"" .. opts.stratcon_api_port .. "\" ssl=\"on\" />\n" ..
  "</listeners>\n")
end

function readfile(file)
  local inp = io.open(file, "rb")
  if inp ~= nil then
    local data = inp:read("*all")
    inp:close()
    return data
  end
  return nil
end

function TestConfig:make_iep_config(fd, opts)
  if opts.iep.disabled == nil then opts.iep.disabled = "false" end
  local ieproot = opts.workspace .. "/logs/" .. opts.name .. "_iep_root"
  local st = mtev.stat(ieproot)
  if st == nil then mtev.mkdir_for_file(ieproot .. "/file", tonumber('777',8)) end
  local template = readfile(cwd .. "/../../src/java/reconnoiter-riemann/run-iep.sh")
  if template == nil then
    mtev.log("stderr", "cannot open source run-iep.sh\n")
  end

  local out = template:gsub('\nDIRS="', "\nDIRS=\"" .. cwd .. "/../../src/java/reconnoiter-riemann/target ");
  out = out:gsub('\nJPARAMS="', "\nJPARAMS=\"-Djava.security.egd=file:/dev/./urandom ");
  local ss = mtev.open(ieproot .. "/run-iep.sh", O_NEW, tonumber('755', 8))
  mtev.write(ss, out)
  mtev.close(ss)

  local rc = mtev.open(ieproot .. "/riemann.config", O_NEW, tonumber('644', 8))
  mtev.write(rc, opts.iep.riemann.config)
  mtev.close(rc)

  mtev.write(fd, "\n" ..
  "<iep disabled=\"" .. opts.iep.disabled .. "\">\n" ..
  "  <start directory=\"" .. ieproot .. "\"\n" ..
  "         command=\"" .. ieproot .. "/run-iep.sh\" />\n")

  for mqt, obj in pairs(opts.iep.mq) do
    if opts.generics[mqt .. "_driver"] ~= nil then
      mtev.write(fd,"    <mq type=\"" .. mqt .. "\">\n");
      for k,v in pairs(obj) do
        mtev.write(fd,"      <" .. k .. ">" .. v .. "</" .. k .. ">\n")
      end
      mtev.write(fd,"    </mq>\n")
    end
  end
  for bt, obj in pairs(opts.iep.broker) do
    if opts.generics[bt .. "_driver"] ~= nil then
      mtev.write(fd,"    <broker adapter=\"" .. bt .. "\">\n")
      for k,v in pairs(obj) do
        mtev.write(fd,"      <" .. k .. ">" .. v .. "</" .. k .. ">\n")
      end
      mtev.write(fd,"    </broker>\n")
    end
  end
  mtev.write(fd,"</iep>\n")
end

function TestConfig:make_database_config(fd, opts)
  mtev.write(fd,"\n" ..
  "<database>\n" ..
  "  <journal>\n" ..
  "    <path>" .. opts.workspace .. "/logs/" .. opts.name .. "_stratcon.persist</path>\n" ..
  "  </journal>\n" ..
  "  <dbconfig>\n" ..
  "    <host>localhost</host>\n" ..
  "    <port>" .. NOIT_TEST_DB_PORT .. "</port>\n" ..
  "    <dbname>reconnoiter</dbname>\n" ..
  "    <user>stratcon</user>\n" ..
  "    <password>stratcon</password>\n" ..
  "  </dbconfig>\n" ..
  "  <statements>\n" ..
  "    <allchecks><![CDATA[\n" ..
  "      SELECT remote_address, id, target, module, name\n" ..
  "        FROM check_currently\n" ..
  "    ]]></allchecks>\n" ..
  "    <findcheck><![CDATA[\n" ..
  "      SELECT remote_address, id, target, module, name\n" ..
  "        FROM check_currently\n" ..
  "       WHERE sid = $1\n" ..
  "    ]]></findcheck>\n" ..
  "    <allstoragenodes><![CDATA[\n" ..
  "      SELECT storage_node_id, fqdn, dsn\n" ..
  "        FROM stratcon.storage_node\n" ..
  "    ]]></allstoragenodes>\n" ..
  "    <findstoragenode><![CDATA[\n" ..
  "      SELECT fqdn, dsn\n" ..
  "        FROM stratcon.storage_node\n" ..
  "       WHERE storage_node_id = $1\n" ..
  "    ]]></findstoragenode>\n" ..
  "    <mapallchecks><![CDATA[\n" ..
  "      SELECT id, sid, noit as remote_cn, storage_node_id, fqdn, dsn\n" ..
  "        FROM stratcon.map_uuid_to_sid LEFT JOIN stratcon.storage_node USING (storage_node_id)\n" ..
  "    ]]></mapallchecks>\n" ..
  "    <mapchecktostoragenode><![CDATA[\n" ..
  "      SELECT o_storage_node_id as storage_node_id, o_sid as sid,\n" ..
  "             o_fqdn as fqdn, o_dsn as dsn\n" ..
  "        FROM stratcon.map_uuid_to_sid($1,$2)\n" ..
  "    ]]></mapchecktostoragenode>\n" ..
  "    <check><![CDATA[\n" ..
  "      INSERT INTO check_archive_%Y%m%d\n" ..
  "                  (remote_address, whence, sid, id, target, module, name)\n" ..
  "           VALUES ($1, 'epoch'::timestamptz + ($2 || ' seconds')::interval,\n" ..
  "                   $3, $4, $5, $6, $7)\n" ..
  "    ]]></check>\n" ..
  "    <status><![CDATA[\n" ..
  "      INSERT INTO check_status_archive_%Y%m%d\n" ..
  "                  (whence, sid, state, availability, duration, status)\n" ..
  "           VALUES ('epoch'::timestamptz + ($1 || ' seconds')::interval,\n" ..
  "                   $2, $3, $4, $5, $6)\n" ..
  "    ]]></status>\n" ..
  "    <metric_numeric><![CDATA[\n" ..
  "      INSERT INTO metric_numeric_archive_%Y%m%d\n" ..
  "                  (whence, sid, name, value)\n" ..
  "           VALUES ('epoch'::timestamptz + ($1 || ' seconds')::interval,\n" ..
  "                   $2, $3, $4)\n" ..
  "    ]]></metric_numeric>\n" ..
  "    <metric_text><![CDATA[\n" ..
  "      INSERT INTO metric_text_archive_%Y%m%d\n" ..
  "                  ( whence, sid, name,value)\n" ..
  "           VALUES ('epoch'::timestamptz + ($1 || ' seconds')::interval,\n" ..
  "                   $2, $3, $4)\n" ..
  "    ]]></metric_text>\n" ..
  "    <config><![CDATA[\n" ..
  "      SELECT stratcon.update_config\n" ..
  "             ($1, $2, $3,\n" ..
  "              'epoch'::timestamptz + ($4 || ' seconds')::interval,\n" ..
  "              $5)\n" ..
  "    ]]></config>\n" ..
  "    <findconfig><![CDATA[\n" ..
  "      SELECT config FROM stratcon.current_node_config WHERE remote_cn = $1\n" ..
  "    ]]></findconfig>\n" ..
  "  </statements>\n" ..
  "</database>\n")
end

function TestConfig:make_stratcon_config(name, opts)
  if name == nil then name = self.name end
  if opts == nil then opts = self.opts end
	if opts.stratcon_stomp_port == nil then opts.stratcon_stomp_port = self.STOMP_PORT end
  self:L("make_stratcon_config in " .. opts.workspace)
  if opts.generics == nil then
    opts.generics = { stomp_driver = { image = 'stomp_driver' },
                      postgres_ingestor = { image = 'postgres_ingestor' } }
  end
  if opts.rest_acls == nil then
    opts.rest_acls = { { type = 'deny', rules = { { type = 'allow' } } } }
  end
  if opts.iep == nil then opts.iep = {} end
  if opts.iep.mq == nil then
    opts.iep.mq = { stomp = { port = opts.stratcon_stomp_port, hostname = '127.0.0.1' } }
  end
  if opts.iep.broker == nil then
    opts.iep.broker = { stomp = { port = opts.stratcon_stomp_port, hostname = '127.0.0.1' } }
  end
  if opts.iep.riemann == nil then
    opts.iep.riemann = { config =
"(logging/init :file \"riemann.log\")\n(streams \n (where (and (tagged \"type:numeric\")\n (not (nil? :metric)))\n (reconnoiter/alert-key \"numeric\"))\n)\n" }
  end
  local file = opts.workspace .. "/" .. name .. "_stratcon.conf"
  self:L("make_stratcon_config -> open " .. file);
  local fd = mtev.open(file, O_NEW, tonumber('644',8))
  mtev.write(fd, '<?xml version="1.0" encoding="utf8" standalone="yes"?>' .. "\n")
  mtev.write(fd, '<stratcon id="8325581c-1068-11e1-ac63-db8546d81c8b" metric_period="1000">' .. "\n")
  self:make_eventer_config(fd, opts)
  self:make_rest_acls(fd, opts)
  self:make_stratcon_noits_config(fd, opts)
  self:make_logs_config(fd, opts)
  self:make_modules_config(fd, opts)
  self:make_stratcon_listeners_config(fd, opts)
  self:make_database_config(fd, opts)
  self:make_iep_config(fd, opts)
  mtev.write(fd, "</stratcon>\n")
  self:L("make_stratcon_config -> close " .. file)
  mtev.close(fd)
  return file
end

-- Generic TestProc to be shared by noitd and stratcond process management
TestProc = {}
TestProc.__index = TestProc

function TestProc:create()
  local obj = {}
  setmetatable(obj, TestProc)
  return obj
end

function TestProc:configure(name, opts)
  if name == nil then name = "noit" end
  if opts == nil then opts = {} end
  if opts.name == nil then opts.name = name end
  opts.testdir = find_test_dir()
  opts.workspace = cwd .. '/' .. opts.testdir .. '/workspace'
  self.name = name
  mtev.mkdir_for_file(opts.workspace .. '/file', tonumber('777',8))
  self:setup_config(opts)

  if opts.path == nil then opts.path = self:path() end
  if opts.argv == nil then opts.argv = self:argv() end

  self.opts = opts
  return self
end

function TestProc:new(...)
  return TestProc:create():configure(...)
end
function TestProc:start(params, sloppy)
  local curl_log_filename = self.opts.workspace .. '/curls_' .. self.name .. '.log'
  self.curl_log = assert(io.open(curl_log_filename, 'a'))
  self.api = self:API()
  self.api:set_curl_log(self.workspace, self.name, self.curl_log)
  if self.proc ~= nil then
    if sloppy then return self end
    error("TestProc:start failed, already running")
  end
  local argv = {unpack(self.opts.argv)}
  if params == nil then params = {} end
  if params.dir == nil then params.dir = self.opts.workspace end
  if params.path == nil then params.path = self.opts.path end
  if params.argv == nil then params.argv = argv end
  if params.env == nil then params.env = env_flatten(mtevenv()) end
  if params.timeout == nil then params.timeout = 10 end
  if params.logname == nil then params.logname = self.name end
  if params.boot_match == nil then params.boot_match = params.argv[1] .. ' ready' end
  if TEST_OPTIONS.debug then table.insert(argv, 2, '-d') end
  self.proc = start_child(params)
  self.crash_counter = self:watchfor(mtev.pcre(" \\d+ has crashed"))
  self.crash_count = 0
  self.expect_crash = false
  return self
end
function TestProc:pause() return self.proc:pause() end
function TestProc:resume() return self.proc:resume() end
function TestProc:watchfor(match, many)
  return self.proc:watchfor(match, many)
end
function TestProc:watchfor_stop(key)
  return self.proc:watchfor_stop(key)
end
function TestProc:waitfor(key,timeout)
  return self.proc:waitfor(key,timeout)
end

function TestProc:API(cert)
  -- usually stratcon connects to noit
  if cert == nil then
    cert = "test-stratcon"
    if self.api ~= nil then
      return self.api
    end
  end
  local api = API:new(self:api_port(), cert)
  return api
end
function TestProc:is_booted()
  return self.proc ~= nil and self.proc:pid() >= 0
end
function TestProc:crashes()
  while self:waitfor(self.crash_counter,0) do
    self.crash_count = self.crash_count + 1
  end
  return self.crash_count
end
function TestProc:stop()
  if self.expect_crash == false and self:crashes() > 0 then
    error("process crashed")
  end
  if self.proc ~= nil then
    kill_child(self.proc)
  end
  self.proc = nil
  self.api:set_curl_log(nil, nil, nil)
  return self
end

TestNoit = inheritsFrom(TestProc)
function TestNoit:new(...) return TestNoit:create():configure(...) end
function TestNoit:path() return noitd end
function TestNoit:argv() return { 'noitd', '-D', '-D', '-c', self.conffile } end
function TestNoit:api_port() return self.opts.noit_api_port end
function TestNoit:setup_config(opts)
  self.conf = TestConfig:new(self.name, opts)
  self.conffile = self.conf:make_noit_config()
end

TestStratcon = inheritsFrom(TestProc)
function TestStratcon:new(...) return TestStratcon:create():configure(...) end
function TestStratcon:path() return stratcond end
function TestStratcon:argv() return { 'stratcond', '-D', '-D', '-c', self.conffile } end
function TestStratcon:api_port() return self.opts.stratcon_api_port end
function TestStratcon:web_port() return self.opts.stratcon_web_port end
function TestStratcon:stomp_port() return self.opts.stratcon_stomp_port end
function TestStratcon:setup_config(opts)
  self.conf = TestConfig:new(self.name, opts)
  self.conffile = self.conf:make_stratcon_config()
end

TestPostgres = {}
TestPostgres.__index = TestPostgres

function TestPostgres:new()
  local obj = { db_dir = NOIT_TEST_DB(), db_port = NOIT_TEST_DB_PORT }
  setmetatable(obj, TestPostgres)
  return obj
end

function TestPostgres:db_version()
  local ret, out, err = system({ argv = { 'postgres', '--version' }, timeout = 5 })
  local rv, v = mtev.pcre("\\s(\\d+\\.\\d+)")(out)
  if rv then return tonumber(v) end
  return 9.0
end

function TestPostgres:client()
  if self._client then return self._client end
  self._client = pgmoon.new({ host = 'localhost', port = self.db_port,
                              database = 'reconnoiter', user = 'reconnoiter'})
  local success, err = self._client:connect()
  assert(success)
  return self._client
end

function TestPostgres:setup()
  if not utils.postgres_reqs() then
    return "postgres reqs missing ($PATH?)"
  end
  local dir = self.db_dir
  self:shutdown() -- just in case it is still running?!
  utils.rm_rf(dir)
  local ret, out, err = system({
    argv = { 'initdb', '-A', 'trust', dir },
    timeout = 10
  })
  if ret ~= 0 then
    return 'initdb: ' .. tostring(ret) .. ": " .. tostring(out) .. "/" .. tostring(err)
  end
  local fd = mtev.open(dir .. "/postgresql.conf", O_NEW, tonumber('644',8))
  if fd < 0 then return "cannot open postgresql.conf" end
  mtev.write(fd, "listen_addresses = 'localhost'\n")
  mtev.write(fd, "port = " .. self.db_port .. "\n")
  self.sockdir = '/tmp/' .. mtev.sha256_hex(dir .. self.db_port)
  utils.rm_rf(self.sockdir)
  mtev.mkdir_for_file(self.sockdir .. '/socket', tonumber('755', 8))
  if self:db_version() >= 9.3 then
    mtev.write(fd, "unix_socket_directories = '" .. self.sockdir .. "'\n")
  else
    mtev.write(fd, "unix_socket_directory = '" .. self.sockdir .. "'\n")
  end
  mtev.close(fd)

  -- pg_ctl never closes stderr/stdout, so we can't use system
  local proc, in_e, out_e, err_e =
    mtev.spawn('pg_ctl', { 'pg_ctl', '-D', dir, '-l', dir .. '/serverlog', '-s', '-w', 'start' },
               env_flatten(ENV))
  ret = nil
  local start_o, start_e = mtev.uuid(), mtev.uuid()
  local out_key, err_key = mtev.uuid(), mtev.uuid()
  if proc ~= nil then
    in_e:close()
    mk_coroutine_reader(start_o, out_key, out_e)
    mk_coroutine_reader(start_e, err_key, err_e)
    mtev.notify(start_o, true)
    mtev.notify(start_e, true)
    ret = proc:wait(10)
  end
  if ret == nil then
    proc:kill()
    return "pg_ctl did not return"
  end
  if mtev.WEXITSTATUS(ret) ~= 0 then
    local okey, out_data = mtev.waitfor(out_key, 1)
    local ekey, err_data = mtev.waitfor(err_key, 1)
    return tostring(ret) .. ': ' .. out_data .. '/' .. err_data
  end

  -- load DB
  ret, out, err = system({
    argv = { 'psql', '-h', 'localhost', '-p', self.db_port, 'postgres',
             '-f', cwd .. "/../../sql/reconnoiter_ddl_dump.sql" },
    timeout = 10
  })
  err = err and err or ""
  local lines = err:split("\n")
  for i, line in ipairs(lines) do
    if line ~= '' then
      local rv = mtev.pcre("\\b(NOTICE|INFO)\\b")(line)
      if not rv then return line end
    end
  end
  out = out or ""
  if ret ~= 0 then return "psql load failed (" .. out .. ") (" .. err .. ")" end
  return nil
end

function TestPostgres:shutdown()
  local dir = self.db_dir
  local st = mtev.stat(dir)
  if st == nil then return end
  local ret, out, err = system({
    argv = { 'pg_ctl', '-D', dir, '-w', '-m', 'immediate', 'stop' },
    timeout = 10
  })
  if self.sockdir ~= nil then
    utils.rm_rf(self.sockdir)
  end
end
