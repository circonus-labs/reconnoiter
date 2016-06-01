/* to run this test:
 *
 * npm install noit-connection
 * npm install connection
 * node test.js -keyfile ../../test/client.key -cafile ../../test/test-ca.crt -certfile ../../test/client.crt -cn noit-tes-id 1b4e28ba-2fa1-11d2-883f-b9b761bde3fb -d
 */
var sys = require('sys'),
    crypto = require('crypto'),
    ls = require('noit'),
    cafile = 'dummy-ca.crt',
    keyfile = 'dummy.key',
    certfile = 'dummy.crt',
    host = '127.0.0.1',
    cn = null,
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

console.log("Streaming: " + id + " @ " + period);
if(proxy_port) {
  var proxy = new ls.connection(43191, host,
              ls.utils.hashToCreds({ //key: keyfile, cert: certfile,
                                     ca: cafile }), cn || host);
  proxy.reverse("check/" + id, '127.0.0.1', proxy_port);
}
var stats = {};
ls.connection_set_stats(stats);

noit = new ls.connection(43191, host,
            ls.utils.hashToCreds({ key: keyfile, cert: certfile,
                                   ca: cafile }), cn || host);
noit.livestream(id, period);
noit.on('live', function(uuid,period,r,i) {
  console.log(this.r,i);
  if (debug) {
      console.log(stats);
  }
});
