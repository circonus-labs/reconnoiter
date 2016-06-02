/* to run this test:
 *
 * npm install noit-connection
 * npm install connection
 * node firehose-test
 */

var sys = require('util'),
    fq = require('./fq'),
    loginfo = require('./log'),
    os = require('os'),
    fh = function(p, s) {
      fq.call(this, p, s);
    };

sys.inherits(fh, fq);

fh.prototype.parse = function(s, cb) { loginfo.parse(s, cb); };
fh.prototype.defaults = function() {
  return {
    exchange: 'noit.firehose',
    program: 'prefix:"check." sample(1)',
    queuename: process.argv[1].replace(/.+\//, '') + '-firehose-' + os.hostname() + '-' + process.pid
  };
};

module.exports = fh;
