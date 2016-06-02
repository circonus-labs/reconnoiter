var sys = require('sys'),
    crypto = require('crypto'),
    ls = require('noit-connection'),
    cafile = 'dummy-ca.crt',
    keyfile = 'dummy.key',
    certfile = 'dummy.crt',
    host = '127.0.0.1',
    cn = null,
    revkey = null,
    proxy_port = null,
    period = 1000,
    id = '9beeffef-fee8-4885-987a-faa10f16e724',
    debug = false;

function help() {
  console.log(process.argv[0] + "\n" +
              "    [-l <port>]            : proxy port\n" +
              "    [-d]                   : debug\n" +
              "    [(-h|-host) host]      : noit\n" +
              "    [(-c|-cn) cn]          : noit cn\n" +
              "    [(-p|-period) period]  : period in ms\n" +
              "    [(-i|-id) uuid]        : check uuid\n" +
              "    [-k revkey]            : rev proxy key\n" +
              "    [-cafile filename]     : CA\n" +
              "    [-keyfile filename]    : client key\n" +
              "    [-certfile filename]   : client cert\n");
}
for(var i=2; i<process.argv.length; i++) {
  switch(process.argv[i]) {
    case '-d': debug = true; break;
    case '-host':
    case '-h': host = process.argv[++i]; break;
    case '-cn':
    case '-c': cn = process.argv[++i]; break;
    case '-period':
    case '-p': period = process.argv[++i]; break;
    case '-l': proxy_port = process.argv[++i]; break;
    case '-id':
    case '-i': id = process.argv[++i]; break;
    case '-k': revkey = process.argv[++i]; break;
    case '-cafile': cafile = process.argv[++i]; break;
    case '-keyfile': keyfile = process.argv[++i]; break;
    case '-certfile': certfile = process.argv[++i]; break;
    default:
      help();
      process.exit(-1);
  }
}
global.debug = debug;

console.log("Connecting to " + host + "/" + cn || host);

if(proxy_port) {
  var proxy = new ls.connection(43191, host,
              ls.utils.hashToCreds({ //key: keyfile, cert: certfile,
                                     ca: cafile }), cn || host);
  if(revkey == null) revkey = '';
  else revkey = '#' + revkey;
  proxy.reverse("check/" + id + revkey, '127.0.0.1', proxy_port);
}
