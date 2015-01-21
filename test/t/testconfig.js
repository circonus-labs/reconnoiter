'use strict';
var pg = require('pg'),
    stomp = require('stomp'),
    fs = require('fs'),
    sys = require('sys'),
    async = require('async'),
    nc = require('../../src/js/index'),
    spawn = require('child_process').spawn,
    events = require('events');

nc.setCA('../test-ca.crt');

var boot_timeout = 5000;
var default_filterset = {
  "allowall": [ { "type": "allow" } ],
};
var all_noit_modules = {
  'selfcheck': { 'image': 'selfcheck' },
  'ping_icmp': { 'image': 'ping_icmp' },
  'snmp': { 'image': 'snmp' },
  'ssh2': { 'image': 'ssh2' },
  'mysql': { 'image': 'mysql' },
  'postgres': { 'image': 'postgres' },
  'test_abort': { 'image': 'test_abort' },
  'varnish': { 'loader': 'lua', 'object': 'noit.module.varnish' },
  'http': { 'loader': 'lua', 'object': 'noit.module.http' },
  'resmon': { 'loader': 'lua', 'object': 'noit.module.resmon' },
  'smtp': { 'loader': 'lua', 'object': 'noit.module.smtp' },
  'tcp': { 'loader': 'lua', 'object': 'noit.module.tcp' },
};

var NOIT_TEST_DB =
    "/tmp/noit-test-db-" + process.getuid() + 
    (process.env['BUILD_NUMBER'] || "");
var NOIT_TEST_DB_PORT = 23816 + (process.env['BUILD_NUMBER'] || 0);

var testconfig = function() {
  // Jitter the ports up (in blocks of 10 for 10k ports)
  var jitter = Math.floor(Math.random() * 10000 / 10) * 10;
  this.NOIT_API_PORT = 42364 + jitter;
  this.NOIT_CLI_PORT = 42365 + jitter;
  this.STRATCON_API_PORT = 42366 + jitter;
  this.STRATCON_CLI_PORT = 42367 + jitter;
  this.STRATCON_WEB_PORT = 42368 + jitter;
  this.STOMP_PORT = 42369 + jitter;
}

sys.inherits(testconfig, events.EventEmitter);

testconfig.prototype.set_verbose = function(level) {
  this.verbose = level;
}
testconfig.prototype.L = function(err) {
  if(this.verbose > 1) console.log(err);
}
var pgclient = function(cb,db,user) {
  db = db || 'postgres';
  user = user || process.env['USER'];
  pg.connect({ user: user, database: db,
               host: 'localhost', port: NOIT_TEST_DB_PORT },
             cb);
}

testconfig.prototype.make_eventer_config = function(fd, opts) {
  var cwd = opts['cwd'];
  if(!opts['eventer_config']) opts['eventer_config'] = {};
  if(!opts['eventer_config']['default_queue_threads'])
    opts['eventer_config']['default_queue_threads'] = 10;
  if(!opts['eventer_config']['default_ca_chain'])
    opts['eventer_config']['default_ca_chain'] = cwd + "/../test-ca.crt";
  var out = "\n" +
  "<eventer>\n" +
  "  <config>\n" +
  "    <default_queue_threads>" + opts['eventer_config']['default_queue_threads'] + "</default_queue_threads>\n" +
  "    <default_ca_chain>" + opts['eventer_config']['default_ca_chain'] + "</default_ca_chain>\n" +
  "  </config>\n" +
  "</eventer>\n";
  fs.writeSync(fd, out);
}
testconfig.prototype.make_rest_acls = function(fd, opts) {
  var acls = opts['rest_acls'];
  fs.writeSync(fd, "  <rest>\n");
  acls.forEach(function (acl) {
    fs.writeSync(fd,"    <acl");
    if(acl.hasOwnProperty('type')) fs.writeSync(fd, ' type="' + acl['type'] + '"');
    if(acl.hasOwnProperty('cn')) fs.writeSync(fd, ' cn="' + acl['cn'] + '"');
    if(acl.hasOwnProperty('url')) fs.writeSync(fd, ' url="' + acl['url'] + '"');
    fs.writeSync(fd,">\n");
    acl['rules'].forEach(function (rule) {
      fs.writeSync(fd,"      <rule");
      if(rule.hasOwnProperty('type')) fs.writeSync(fd, ' type="' + rule['type'] + '"');
      if(rule.hasOwnProperty('cn')) fs.writeSync(fd, ' cn="' + rule['cn'] + '"');
      if(rule.hasOwnProperty('url')) fs.writeSync(fd, ' url="' + rule['url'] + '"');
      fs.writeSync(fd,"/>\n");
    });
    fs.writeSync(fd,"    </acl>\n");
  });
  fs.writeSync(fd,"  </rest>\n");
}

testconfig.prototype.make_log_section = function(fd, type, dis) {
  fs.writeSync(fd,"      <" + type + ">\n" +
                  "        <outlet name=\"error\"/>\n");
  for (var t in dis) {
    var d = dis[t];
    if(t == null || t.length == 0) continue;
    fs.writeSync(fd,"        <log name=\"" + type + "/" + t + "\" disabled=\"" + d + "\"/>\n");
  }
  fs.writeSync(fd,"      </" + type + ">\n");
}
testconfig.prototype.make_logs_config = function(fd, opts) {
  var cwd = opts['cwd'];
  var logtypes = ["collectd", "dns", "eventer", "external", "lua", "mysql", "ping_icmp", "postgres", 
                  "selfcheck", "snmp", "ssh2", "listener", "datastore" ];
  // These are disabled attrs, so they look backwards
  if(!opts.hasOwnProperty('logs_error'))
    opts['logs_error'] = { '': false };
  if(!opts.hasOwnProperty('logs_debug'))
    opts['logs_debug'] = { '': false };

  // Listener is special, we need that for boot availability detection
  if(!opts['logs_debug'].hasOwnProperty('listener'))
    opts['logs_debug']['listener'] = false;
  // As is datastore
  if(!opts['logs_debug'].hasOwnProperty('datastore'))
    opts['logs_debug']['datastore'] = false;

  logtypes.forEach(function (type) {
    if(!opts['logs_error'].hasOwnProperty(type))
      opts['logs_error'][type] = false;
    if(!opts['logs_debug'].hasOwnProperty(type))
      opts['logs_debug'][type] = true;
  });
 
  var buff = "\n" + 
  "<logs>\n" +
  "  <console_output>\n" +
  "    <outlet name=\"stderr\"/>\n" +
  "    <log name=\"error\" disabled=\"" + opts['logs_error'][''] + "\" timestamps=\"true\"/>\n" +
  "    <log name=\"debug\" disabled=\"" + opts['logs_debug'][''] + "\" timestamps=\"true\"/>\n" +
  "    <log name=\"http/access\" disabled=\"false\" timestamps=\"true\"/>\n" +
  "  </console_output>\n" +
  "  <feeds>\n" +
  "    <log name=\"feed\" type=\"jlog\" path=\"" + cwd + "/logs/" + opts['name'] + ".feed(*)\"/>\n" +
  "  </feeds>\n" +
  "  <components>\n";
  fs.writeSync(fd, buff);
  this.make_log_section(fd, 'error', opts['logs_error']);
	this.make_log_section(fd, 'debug', opts['logs_debug']);
  fs.writeSync(fd,"\n" +
  "  </components>\n" +
  "  <feeds>\n" +
  "    <config><extended_id>on</extended_id></config>\n" +
  "    <outlet name=\"feed\"/>\n" +
  "    <log name=\"bundle\"/>\n" +
  "    <log name=\"check\">\n" +
  "      <outlet name=\"error\"/>\n" +
  "    </log>\n" +
  "    <log name=\"status\"/>\n" +
  "    <log name=\"metrics\"/>\n" +
  "    <log name=\"config\"/>\n" +
  "  </feeds>\n" +
  "</logs>\n");
}
testconfig.prototype.make_modules_config = function(fd,opts) {
  var cwd = opts['cwd'];
  fs.writeSync(fd,"\n" +
  "<modules directory=\"" + cwd + "/../../src/modules\">\n" +
  " <loader image=\"lua\" name=\"lua\">\n" +
  "   <config><directory>" + cwd + "/../../src/modules-lua/?.lua</directory></config>\n" +
  " </loader>\n");
  for (var k in opts['generics']) {
    fs.writeSync(fd, "    <generic");
    if(opts['generics'][k].hasOwnProperty('image'))
      fs.writeSync(fd, " image=\"" + opts['generics'][k]['image'] + "\"");
    fs.writeSync(fd, " name=\"" + k + "\"/>\n");
  }
  for (var k in opts['modules']) {
    fs.writeSync(fd, "    <module");
    if(opts['modules'][k].hasOwnProperty('image'))
      fs.writeSync(fd, " image=\"" + opts['modules'][k]['image'] + "\"");
    if(opts['modules'][k].hasOwnProperty('loader'))
      fs.writeSync(fd, " loader=\"" + opts['modules'][k]['loader'] + "\"");
    if(opts['modules'][k].hasOwnProperty('object'))
      fs.writeSync(fd, " object=\"" + opts['modules'][k]['object'] + "\"");
    fs.writeSync(fd, " name=\"" + k + "\"/>\n");
  }
  fs.writeSync(fd, "</modules>\n");
}
testconfig.prototype.make_noit_listeners_config = function(fd, opts) {
  var cwd = opts['cwd'];
  if(!opts.hasOwnProperty('noit_api_port'))
    opts['noit_api_port'] = this.NOIT_API_PORT;
  if(!opts.hasOwnProperty('noit_cli_port'))
    opts['noit_cli_port'] = this.NOIT_CLI_PORT;
  fs.writeSync(fd, "\n" +
  "<listeners>\n" +
  "  <sslconfig>\n" +
  "    <optional_no_ca>false</optional_no_ca>\n" +
  "    <certificate_file>" + cwd + "/../test-noit.crt</certificate_file>\n" +
  "    <key_file>" + cwd + "/../test-noit.key</key_file>\n" +
  "    <ca_chain>" + cwd + "/../test-ca.crt</ca_chain>\n" +
  "    <crl>" + cwd + "/../test-ca.crl</crl>\n" +
  "  </sslconfig>\n" +
  "  <consoles type=\"noit_console\">\n" +
  "    <listener address=\"*\" port=\""+ opts['noit_cli_port'] + "\">\n" +
  "      <config>\n" +
  "        <line_protocol>telnet</line_protocol>\n" +
  "      </config>\n" +
  "    </listener>\n" +
  "  </consoles>\n" +
  "  <api>\n" +
  "    <config>\n" +
  "      <log_transit_feed_name>feed</log_transit_feed_name>\n" +
  "    </config>\n" +
  "    <listener type=\"control_dispatch\" address=\"inet6:*\" port=\"" + opts['noit_api_port'] + "\" ssl=\"on\"/>\n" +
  "    <listener type=\"control_dispatch\" address=\"inet:*\" port=\"" + opts['noit_api_port'] + "\" ssl=\"on\"/>\n" +
  "  </api>\n" +
  "</listeners>\n");
}
testconfig.prototype.do_check_print = function(fd, tree) {
  var self = this;
  if(tree == null || tree.length == 0) return;
  tree.forEach(function (node) {
    fs.writeSync(fd, "<" + node[0]);
    for(var k in node[1]) {
      fs.writeSync(fd, " " + k + "=\"" + node[0][k] + "\"");
    }
    if(node[2]) {
      fs.writeSync(fd,">\n");
      self.do_check_print(fd, node[2]);
      fs.writeSync(fd,"</check>\n");
    }
    else fs.writeSync(fd,"/>\n");
  });
}
testconfig.prototype.make_checks_config = function(fd, opts) {
  fs.writeSync(fd,"  <checks max_initial_stutter=\"10\" filterset=\"default\">\n");
  this.do_check_print(fd, opts['checks']);
  fs.writeSync(fd,"  </checks>\n");
}
testconfig.prototype.make_filtersets_config = function(fd, opts) {
  fs.writeSync(fd,"<filtersets>\n");
  for (var name in opts['filtersets']) {
    var set = opts['filtersets'][name];
    fs.writeSync(fd,"  <filterset name=\"" + name +"\">\n");
    set.forEach(function(rule) {
      fs.writeSync(fd,"    <rule");
      for (var k in rule) {
        fs.writeSync(fd, " " + k + "=\"" + rule[k] + "\"");
      }
      fs.writeSync(fd,"/>\n");
    });
    fs.writeSync(fd,"  </filterset>\n");
  }
  fs.writeSync(fd,"</filtersets>\n");
}

testconfig.prototype.make_noit_config = function(name, opts) {
  if(!opts.hasOwnProperty('cwd'))
    opts['cwd'] = process.cwd();
  if(!opts.hasOwnProperty('modules'))
    opts['modules'] = all_noit_modules;
  if(!opts.hasOwnProperty('filtersets'))
    opts['filtersets'] = default_filterset;
  if(!opts.hasOwnProperty('rest_acls'))
    opts['rest_acls'] = [ { 'type': 'deny', 'rules': [ { 'type': 'allow' } ] } ];
  var cwd = opts['cwd'];
  var file = cwd + "/logs/" + name + "_noit.conf";
  var fd = fs.openSync(file, "w", { mode: '0644' });
  fs.writeSync(fd, "<?xml version=\"1.0\" encoding=\"utf8\" standalone=\"yes\"?>\n");
  fs.writeSync(fd, "<noit>\n");
  this.make_eventer_config(fd, opts);
  this.make_rest_acls(fd, opts);
  this.make_logs_config(fd, opts);
  this.make_modules_config(fd, opts);
  this.make_noit_listeners_config(fd, opts);
  this.make_checks_config(fd, opts);
  this.make_filtersets_config(fd, opts);
  fs.writeSync(fd,"</noit>\n");
  fs.closeSync(fd);
  return file;
}

testconfig.prototype.make_stratcon_noits_config = function(fd, opts) {
  var cwd = opts['cwd'];
  if(!opts.hasOwnProperty('noit_api_port'))
    opts['noit_api_port'] = this.NOIT_API_PORT;
  fs.writeSync(fd,"\n" +
  "<noits>\n" +
  "  <sslconfig>\n" +
  "    <certificate_file>" + cwd + "/../test-stratcon.crt</certificate_file>\n" +
  "    <key_file>" + cwd + "/../test-stratcon.key</key_file>\n" +
  "    <ca_chain>" + cwd + "/../test-ca.crt</ca_chain>\n" +
  "  </sslconfig>\n" +
  "  <config>\n" +
  "    <reconnect_initial_interval>1000</reconnect_initial_interval>\n" +
  "    <reconnect_maximum_interval>15000</reconnect_maximum_interval>\n" +
  "  </config>\n");
  if(!opts.hasOwnProperty('noits')) opts['noits'] = [];
  opts['noits'].forEach(function (n) {
    fs.writeSync(fd,"    <noit");
    for (var k in n) {
      fs.writeSync(fd," " + k + "=\"" + n[k] + "\"");
    }
    fs.writeSync(fd,"/>\n");
  });
  fs.writeSync(fd,"</noits>\n");
}

testconfig.prototype.make_stratcon_listeners_config = function(fd,opts) {
  var cwd = opts['cwd'];
  if(!opts.hasOwnProperty('stratcon_api_port'))
    opts['stratcon_api_port'] = this.STRATCON_API_PORT;
  if(!opts.hasOwnProperty('stratcon_web_port'))
    opts['stratcon_web_port'] = this.STRATCON_WEB_PORT;
  fs.writeSync(fd,"\n" +
  "<listeners>\n" +
  "  <sslconfig>\n" +
  "    <certificate_file>" + cwd + "/../test-stratcon.crt</certificate_file>\n" +
  "    <key_file>" + cwd + "/../test-stratcon.key</key_file>\n" +
  "    <ca_chain>" + cwd + "/../test-ca.crt</ca_chain>\n" +
  "  </sslconfig>\n" +
  "  <realtime type=\"http_rest_api\">\n" +
  "    <listener address=\"*\" port=\"" + opts['stratcon_web_port'] + "\">\n" +
  "      <config>\n" +
  "        <hostname>stratcon.noit.example.com</hostname>\n" +
  "        <document_domain>noit.example.com</document_domain>\n" +
  "      </config>\n" +
  "    </listener>\n" +
  "  </realtime>\n" +
  "  <listener type=\"control_dispatch\" address=\"*\" port=\"" + opts['stratcon_api_port'] + "\" ssl=\"on\" />\n" +
  "</listeners>\n");
}

testconfig.prototype.make_iep_config = function(fd, opts) {
  var cwd = opts['cwd'];
  if(!opts['iep'].hasOwnProperty('disabled'))
    opts['iep']['disabled'] = false;
  var ieproot = cwd + "/logs/" + opts['name'] + "_iep_root";
  if(!fs.existsSync(ieproot)) fs.mkdirSync(ieproot);
  var template = fs.readFileSync(cwd + "/../../src/java/reconnoiter-riemann/run-iep.sh", { encoding: 'utf8' });
  if(!template) BAIL_OUT("cannot open source run-iep.sh");

  var out = template.replace(/^DIRS="/m, "DIRS=\"" + cwd + "/../../src/java/reconnoiter-riemann/target ");
  out = out.replace(/^JPARAMS="/m, "JPARAMS=\"-Djava.security.egd=file:/dev/./urandom ");
  if(fs.writeFileSync(ieproot + "/run-iep.sh", out, { mode: '0755' }))
    BAIL_OUT("failed to write iep start script");

  if(fs.writeFileSync(ieproot + "/riemann.config", opts['iep']['riemann']['config'], { mode: '0644' }))
    BAIL_OUT("cannot write target riemann.config");

  fs.writeSync(fd, "\n" +
  "<iep disabled=\"" + opts['iep']['disabled'] + "\">\n" +
  "  <start directory=\"" + ieproot + "\"\n" +
  "         command=\"" + ieproot + "/run-iep.sh\" />\n");

  for (var mqt in opts['iep']['mq']) {
    fs.writeSync(fd,"    <mq type=\"" + mqt + "\">\n");
    for (var k in opts['iep']['mq'][mqt]) {
      var v = opts['iep']['mq'][mqt][k];
      fs.writeSync(fd,"      <" + k + ">" + v + "</" + k + ">\n");
    }
    fs.writeSync(fd,"    </mq>\n");
  }
  for (var bt in opts['iep']['broker']) {
    fs.writeSync(fd,"    <broker adapter=\"" + bt + "\">\n");
    for (var k in opts['iep']['broker'][bt]) {
      var v = opts['iep']['broker'][bt][k];
      fs.writeSync(fd,"      <" + k + ">" + v + "</" + k + ">\n");
    }
    fs.writeSync(fd,"    </broker>\n");
  }
  fs.writeSync(fd,"</iep>\n");
}
testconfig.prototype.make_database_config = function(fd, opts) {
  var cwd = opts['cwd'];
  fs.writeSync(fd,"\n" +
  "<database>\n" +
  "  <journal>\n" +
  "    <path>" + cwd + "/logs/" + opts['name'] + "_stratcon.persist</path>\n" +
  "  </journal>\n" +
  "  <dbconfig>\n" +
  "    <host>localhost</host>\n" +
  "    <port>" + NOIT_TEST_DB_PORT + "</port>\n" +
  "    <dbname>reconnoiter</dbname>\n" +
  "    <user>stratcon</user>\n" +
  "    <password>stratcon</password>\n" +
  "  </dbconfig>\n" +
  "  <statements>\n" +
  "    <allchecks><![CDATA[\n" +
  "      SELECT remote_address, id, target, module, name\n" +
  "        FROM check_currently\n" +
  "    ]]></allchecks>\n" +
  "    <findcheck><![CDATA[\n" +
  "      SELECT remote_address, id, target, module, name\n" +
  "        FROM check_currently\n" +
  "       WHERE sid = \$1\n" +
  "    ]]></findcheck>\n" +
  "    <allstoragenodes><![CDATA[\n" +
  "      SELECT storage_node_id, fqdn, dsn\n" +
  "        FROM stratcon.storage_node\n" +
  "    ]]></allstoragenodes>\n" +
  "    <findstoragenode><![CDATA[\n" +
  "      SELECT fqdn, dsn\n" +
  "        FROM stratcon.storage_node\n" +
  "       WHERE storage_node_id = \$1\n" +
  "    ]]></findstoragenode>\n" +
  "    <mapallchecks><![CDATA[\n" +
  "      SELECT id, sid, noit as remote_cn, storage_node_id, fqdn, dsn\n" +
  "        FROM stratcon.map_uuid_to_sid LEFT JOIN stratcon.storage_node USING (storage_node_id)\n" +
  "    ]]></mapallchecks>\n" +
  "    <mapchecktostoragenode><![CDATA[\n" +
  "      SELECT o_storage_node_id as storage_node_id, o_sid as sid,\n" +
  "             o_fqdn as fqdn, o_dsn as dsn\n" +
  "        FROM stratcon.map_uuid_to_sid(\$1,\$2)\n" +
  "    ]]></mapchecktostoragenode>\n" +
  "    <check><![CDATA[\n" +
  "      INSERT INTO check_archive_%Y%m%d\n" +
  "                  (remote_address, whence, sid, id, target, module, name)\n" +
  "           VALUES (\$1, 'epoch'::timestamptz + (\$2 || ' seconds')::interval,\n" +
  "                   \$3, \$4, \$5, \$6, \$7)\n" +
  "    ]]></check>\n" +
  "    <status><![CDATA[\n" +
  "      INSERT INTO check_status_archive_%Y%m%d\n" +
  "                  (whence, sid, state, availability, duration, status)\n" +
  "           VALUES ('epoch'::timestamptz + (\$1 || ' seconds')::interval,\n" +
  "                   \$2, \$3, \$4, \$5, \$6)\n" +
  "    ]]></status>\n" +
  "    <metric_numeric><![CDATA[\n" +
  "      INSERT INTO metric_numeric_archive_%Y%m%d\n" +
  "                  (whence, sid, name, value)\n" +
  "           VALUES ('epoch'::timestamptz + (\$1 || ' seconds')::interval,\n" +
  "                   \$2, \$3, \$4)\n" +
  "    ]]></metric_numeric>\n" +
  "    <metric_text><![CDATA[\n" +
  "      INSERT INTO metric_text_archive_%Y%m%d\n" +
  "                  ( whence, sid, name,value)\n" +
  "           VALUES ('epoch'::timestamptz + (\$1 || ' seconds')::interval,\n" +
  "                   \$2, \$3, \$4)\n" +
  "    ]]></metric_text>\n" +
  "    <config><![CDATA[\n" +
  "      SELECT stratcon.update_config\n" +
  "             (\$1, \$2, \$3,\n" +
  "              'epoch'::timestamptz + (\$4 || ' seconds')::interval,\n" +
  "              \$5)\n" +
  "    ]]></config>\n" +
  "    <findconfig><![CDATA[\n" +
  "      SELECT config FROM stratcon.current_node_config WHERE remote_cn = \$1\n" +
  "    ]]></findconfig>\n" +
  "  </statements>\n" +
  "</database>\n");
}

testconfig.prototype.make_stratcon_config = function(name, opts) {
  this.L("make_stratcon_config");
  if(!opts.hasOwnProperty('cwd'))
    opts['cwd'] = process.cwd();
  this.L("make_stratcon_config in " + opts['cwd']);
  if(!opts.hasOwnProperty('generics'))
    opts['generics'] = { 'stomp_driver': { 'image': 'stomp_driver' },
                         'postgres_ingestor': { 'image': 'postgres_ingestor' } };
  if(!opts.hasOwnProperty('rest_acls'))
    opts['rest_acls'] = [ { 'type': 'deny', 'rules': [ { 'type': 'allow' } ] } ];
  if(!opts.hasOwnProperty('iep')) opts['iep'] = {};
  if(!opts['iep'].hasOwnProperty('mq'))
    opts['iep']['mq'] = { 'stomp': { 'port': this.STOMP_PORT, 'hostname': '127.0.0.1' },
                        };
  if(!opts['iep'].hasOwnProperty('broker'))
    opts['iep']['broker'] = { 'stomp': { 'port': this.STOMP_PORT, 'hostname': '127.0.0.1' },
                            };
  if(!opts['iep'].hasOwnProperty('riemann'))
    opts['iep']['riemann'] = { 'config':
"(logging/init :file \"riemann.log\")\n\
(streams \n\
  (where (and (not (tagged \"riemann\"))\n\
              (not (nil? :metric)))\n\
    (reconnoiter/alert-key \"numeric\"))\n\
  prn)\n"
    };
  var cwd = opts['cwd'];
  var file = cwd + "/logs/" + name + "_stratcon.conf";
  this.L("make_stratcon_config -> open " + file);
  var fd = fs.openSync(file, "w", { mode: '0644' });
  fs.writeSync(fd, '<?xml version="1.0" encoding="utf8" standalone="yes"?>' + "\n");
  fs.writeSync(fd, '<stratcon id="8325581c-1068-11e1-ac63-db8546d81c8b" metric_period="1000">' + "\n");
  this.make_eventer_config(fd, opts);
  this.make_rest_acls(fd, opts);
  this.make_stratcon_noits_config(fd, opts);
  this.make_logs_config(fd, opts);
  this.make_modules_config(fd, opts);
  this.make_stratcon_listeners_config(fd, opts);
  this.make_database_config(fd, opts);
  this.make_iep_config(fd, opts);
  fs.writeSync(fd, "</stratcon>\n");
  this.L("make_stratcon_config -> close " + file);
  fs.close(fd);
  return file;
}

testconfig.prototype.find_in_log = function(logfile, re, opts) {
  var fd = -1, stat, want;
  if(!opts) opts = { offset: 0 };
  if(typeof(logfile) === 'number') {
    fd = logfile;
    stat = fs.fstatSync(fd);
  } else {
    fd = fs.openSync(logfile,'r');
    stat = fs.statSync(logfile);
  }
  want = stat.size - opts.offset;
  if(want <= 0) return 0;
  var buff = new Buffer(want);
  var bytes_read = fs.readSync(fd, buff, 0, want, opts.offset )
  if(logfile != fd) fs.close(fd);
  var lines = buff.toString('utf8').split(/\n/);
  for(var i=0; i<lines.length; i++) {
    var m = re.exec(lines[i]);
    if(m) {
      opts.offset = opts.offset + lines[i].length;
      if(m.length > 1) return m.slice(1);
      return 1;
    }
    if(i>0) opts.offset = opts.offset + lines[i-1].length + 1;
  }
  return 0;
}

testconfig.prototype.wait_for_log = function(re, timeout, interval, cb, tracker) {
  var self = this;
  if(typeof(timeout) == 'function') { tracker = interval; cb = timeout; timeout = 1000; interval = 100; }
  if(typeof(interval) == 'function') { tracker = cb; cb = interval; interval = 100; }
  if(!timeout) timeout = 1000;
  if(!interval) interval = 100;
  if(!tracker) tracker = { offset: 0 };
  var check = function() {
    var m = self.find_in_log(self.logfd, re, tracker);
    if(m) cb(true);
    else if(timeout < 0) {
      console.log("wait_for_log timeout on", re);
      cb(false);
    }
    else {
      timeout = timeout - interval;
      setTimeout(check,interval);
    }
  }
  check();
}

testconfig.prototype.get_log = function() {
  return this.log;
}
testconfig.prototype.stop = function() {
  if(this.child) {
    this.expect_sigkill = true;
    this.child.kill('SIGKILL');
  }
}

testconfig.prototype.logsize = function() {
  var stat = fs.statSync(this.log);
  return stat.size;
}
testconfig.prototype.start = function() {
  var self = this;
  var _program = self._program;
  self.opts['name'] = self.name;
  if(self.child) {
    self.emit('error', 'already started');
    return;
  }
  var conf = self.make_config(self.name, self.opts);
  self.log = "logs/" + self.name + "_" + _program.replace(/d$/,'') + ".log";
  if(fs.exists(self.log)) fs.unlinkSync(self.log);
  self.L(_program + " configured");
  var fd = fs.openSync(self.log, 'w', { mode: "0644" });
  self.logfd = fs.openSync(self.log, 'r');
  var prog = '../../src/' + _program;
  var args = [ '-D', '-c', conf ];
  if(process.env['TRACE']) {
    args.splice(0,1, '-o', "logs/" + self.name + "_" + _program + ".trace", prog);
    prog = process.env['TRACE'];
  }

  self.child = spawn(prog, args, { stdio: [ 'ignore', 'ignore', fd ]});
  self.L(_program + " spawned");
  self.child.on('error', function(err) {
    self.L(_program + " BAIL -> " + err);
    BAIL_OUT(err);
    self.child = null;
  });
  process.on('exit', function() {
    if(self.child) self.child.kill('SIGKILL');
  });
  self.child.on('exit', function(code, signal) {
    self.L(_program + " exit -> " + code);
    if(self.expect_sigkill && signal == 'SIGKILL') code = 0;
    self.emit('exit', code, signal);
    self.child = null;
  });

  self.start = new Date();
  var boot_check = function () {
    if(!self.child) {
      self.emit('error', 'child is gone');
      return;
    }
    var m = self.find_in_log(self.logfd, /process starting: (\d+)/);
    var l = self.find_in_log(self.logfd, /noit_listener\([^,]+, (\d+), \d+, \d+, control_dispatch,.* success/);
    if(m && l) {
      self.emit('boot', m[0], l[0]);
      return;
    }
    if((new Date() - self.start) < boot_timeout) setTimeout(boot_check, 100);
    else self.emit('error', 'boot failed');
  };
  setTimeout(boot_check, 100);
  self.L(_program + " start finished");
}

testconfig.prototype.get_connection = function(port, host, opts, cn) {
  if(!port) port = this.get_api_port();
  if(!host) host = '127.0.0.1';
  if(!opts) opts = { 'key': fs.readFileSync('../' + this.get_keyname() + '.key', {encoding:'utf8'}),
                     'cert': fs.readFileSync('../' + this.get_keyname() + '.crt', {encoding:'utf8'})
                   };
  if(!cn) cn = 'noit-test';
  return new nc.connection(port,host,opts,cn);
}

/* Our noit subclass */

var noit = function(name, opts) {
  noit.super_.call(this);
  this._program = 'noitd';
  this.name = name;
  this.opts = opts || {};
}

sys.inherits(noit, testconfig);
noit.prototype.make_config = function(name,opts) { return this.make_noit_config(name,opts); }
noit.prototype.get_api_port = function() { return this.NOIT_API_PORT; }
noit.prototype.get_cli_port = function() { return this.NOIT_CLI_PORT; }
noit.prototype.get_keyname = function() { return 'client'; }

/* Our stratcon subclass */

var stratcon = function(name, opts) {
  stratcon.super_.call(this);
  this._program = 'stratcond';
  this.name = name;
  this.opts = opts || {};
}

sys.inherits(stratcon, testconfig);
stratcon.prototype.make_config = function(name,opts) { return this.make_stratcon_config(name,opts); }
stratcon.prototype.get_api_port = function() { return this.STRATCON_API_PORT; }
stratcon.prototype.get_cli_port = function() { return this.STRATCON_CLI_PORT; }
stratcon.prototype.get_web_port = function() { return this.STRATCON_WEB_PORT; }
stratcon.prototype.get_stomp_port = function() { return this.STOMP_PORT; }
stratcon.prototype.get_keyname = function() { return 'client'; }

stratcon.prototype.get_stomp_connection = function(topic) {
  var client = new stomp.Stomp({
    port: this.get_stomp_port(),
    host: '127.0.0.1',
    debug: false,
    login: 'guest',
    passcode: 'guest',
  });
  if(topic) {
    client.on('connected', function() {
      client.subscribe({ destination:topic, ack:'auto' });
    });
  }
  return client;
}

/* General cleanup helpers */

var cleanup_list = [ function(cb) { cb(); } ];
var cleanup = function(fn) { cleanup_list.push(fn); }
var do_cleanup = function(exitcode) {
  async.parallel(cleanup_list, function() { process.exit(exitcode); });
}
module.exports = {
  do_cleanup: do_cleanup,
  cleanup: cleanup,
  noit: noit,
  stratcon: stratcon,
  pgclient: pgclient,
  NOIT_TEST_DB_PORT: NOIT_TEST_DB_PORT,
  NOIT_TEST_DB: NOIT_TEST_DB
};
