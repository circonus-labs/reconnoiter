var amqp = require('amqp'),
    sys = require('sys'),
    events = require('events');

function mixin () {
  // copy reference to target object
  var target = arguments[0] || {}, i = 1, length = arguments.length, deep = false, source;

  // Handle a deep copy situation
  if ( typeof target === "boolean" ) {
    deep = target;
    target = arguments[1] || {};
    // skip the boolean and the target
    i = 2;
  }

  // Handle case when target is a string or something (possible in deep copy)
  if ( typeof target !== "object" && !(typeof target === 'function') )
    target = {};

  // mixin process itself if only one argument is passed
  if ( length == i ) {
    target = GLOBAL;
    --i;
  }

  for ( ; i < length; i++ ) {
    // Only deal with non-null/undefined values
    if ( (source = arguments[i]) != null ) {
      // Extend the base object
      Object.getOwnPropertyNames(source).forEach(function(k){
        var d = Object.getOwnPropertyDescriptor(source, k) || {value: source[k]};
        if (d.get) {
          target.__defineGetter__(k, d.get);
          if (d.set) {
            target.__defineSetter__(k, d.set);
          }
        }
        else {
          // Prevent never-ending loop
          if (target === d.value) {
            return;
          }

          if (deep && d.value && typeof d.value === "object") {
            target[k] = mixin(deep,
              // Never move original objects, clone them
              source[k] || (d.value.length != null ? [] : {})
            , d.value);
          }
          else {
            target[k] = d.value;
          }
        }
      });
    }
  }
  // Return the modified object
  return target;
}

var fh = function(params, stats) {
  this.stats = stats || {};
  this.stats.connects = 0;
  this.stats.closes = 0;
  this.stats.errors = 0;
  this.stats.heartattacks = 0;
  this.stats.messages_in = 0;
  this.stats.messages_out = 0;
  this.params = {};
  if(debug) params.debug = debug;
  for (var key in params)
    this.params[key] = params[key];
  this.mq = null;
  if(typeof params.host === "string") this.hosts = [params.host];
  else this.hosts = params.host;
  this.hidx = -1;
}
sys.inherits(fh, events.EventEmitter);

fh.prototype.parse = function(d, cb) { cb(d); };

fh.prototype.defaults = function() { return {}; };

fh.prototype.clearHeartbeat = function() {
  this.log("clearing heartbeat");
  this._last_seen_hb = this._hb = 0;
  if(this.hbout) clearInterval(this.hbout);
  if(this.hbin) clearInterval(this.hbin);
  delete this.hbout;
  delete this.hbin;
};

fh.prototype.handleHeartbeat = function() {
  this._hb = (this._hb + 1) % 4294967296;
};
fh.prototype.log = function(msg) {
  if(this.params.debug)
    sys.puts("["+this.options.exchange+"/"+this.options.routing_key+"] " + msg);
};
fh.prototype.listen = function() {
  var p = this;
  var f = arguments[1];
  var options = {};
  if(typeof f === "object") {
    options = f;
    f = arguments[2];
  }
  this.options = mixin(this.defaults(), options);
  this.hidx = (this.hidx + 1) % this.hosts.length;
  this.params.host = this.hosts[this.hidx];
  this.log('connect[' + this.hidx + ']: ' + this.params.host);
  this.emit('connecting');
  this.stats.connects++;
  this.mq = amqp.createConnection(this.params);
  if(f) this.on('message', f);
  this.mq.on('connect', function() { p.emit('connect'); });
  var retry = function(t) {
    if(p.retryTimeout) clearTimeout(p.retryTimeout);
    p.retryTimeout = setTimeout(function() {
      clearTimeout(p.retryTimeout);
      p.listen(p.options);
    }, t);
  };
  this.mq.on('error', function (e) {
    p.stats.errors++;
    p.log(e);
  });
  this.mq.on('close', function (e) {
    p.stats.closes++;
    p.clearHeartbeat();
    retry(1000);
    p.log("amqp close.");
    p.emit('close');
  });
  if(p.params.heartbeat) {
    p.clearHeartbeat();
    p.handleHeartbeat();
    p.hbout = setInterval(function() { p.mq.heartbeat(); },
                          p.params.heartbeat * 1000);
    p.log("setting up heartbeat");
    p.hbin = setInterval(function() {
      if(p._last_seen_hb == p._hb) {
        p.stats.heartattacks++;
        p.log("lost heartbeat.");
        p.clearHeartbeat();
        if(p.mq.writeable)
          p.mq.end();
        else {
          p.mq.destroy();
          p.log("amqp abort connect.");
          p.emit('close');
          retry(1000);
        }
      }
      else
        p.log("amqp checking pulse " + p._last_seen_hb + " -> " + p._hb);
      p._last_seen_hb = p._hb;
    }, p.params.heartbeat * 3 * 1000);
  }
  this.mq.on('heartbeat', function() { p.handleHeartbeat(); });
  this.mq.on('ready', function () {
    var mq = this;
    var exchange = p.options.exchange;
    if(p.options.type) exchange = mq.exchange(p.options.exchange, p.options);
    var q = mq.queue(p.options.queuename || '', function() {
      q.bind(exchange, p.options.routing_key);

      q.subscribeRaw({noAck: true}, function (m) {
        p.handleHeartbeat();
        m._payload = '';
        m.addListener('data', function (d) {
          if(d != null) this._payload += d;
        });
        m.addListener('end', function () {
          var str = this._payload;
          p.stats.messages_in++;
          if(debug) {
            var tidx = str.indexOf('\t');
            console.log("mq["+p.options.routing_key+"] <- " +
                        (tidx > 0 ? str.substring(0, tidx) : "??"));
          }
          p.parse(str, function(infos) {
            if(infos.constructor !== Array) infos = [infos];
            var i=0; cnt=infos.length;
            p.stats.messages_out += cnt;
            for(;i<cnt;i++) {
              var info = infos[i]
              if(debug) console.log("mq["+p.options.routing_key+"] -> " +
                                    info.type + ":" + info.id);
              p.emit('message', m, info);
            }
          });
        });
      });
    });
  });
};

module.exports = fh;
