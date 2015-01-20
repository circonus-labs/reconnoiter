var sys = require('sys'),
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
    routing_key: 'check.',
    queuename: process.argv[1].replace(/.+\//, '') + '-firehose-' + os.hostname() + '-' + process.pid
  };
};

module.exports = fh;
