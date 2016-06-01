var sys = require('sys'),
    events = require('events'),
    FQ = require('fq');

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
  if(typeof debug !== 'undefined' && !params.hasOwnProperty('debug'))
    params.debug = debug;
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
  this.hidx = (this.hidx + 1) % this.hosts.length;
  this.params.host = this.hosts[this.hidx];
  this.params.heartbeat = 2000;
  this.log('connect[' + this.hidx + ']: ' + this.params.host);
  this.emit('connecting');
  this.stats.connects++;
  this.options = mixin(this.defaults(), options, this.params);

  var client = new FQ(this.params);
  client.connect();
  client.on('ready', function(){
    p.emit('connect');
    client.bind(p.options.exchange,p.options.program,0,
                function(err, res) {
                  client.consume();
                });
  });
  client.on('error', function(e){
    p.stats.errors++;
    p.log(e);
  });
  if(f) this.on('message', f);
  client.on('message', function(msg){
    if(msg == null || msg.payload == null) {
      console.log("blank", msg.getSender(), msg.getRoute());
      return;
    }
    var str = msg.payload.toString();
    p.stats.messages_in++;
    if(p.params.debug) {
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
        if(p.params.debug) console.log("mq["+p.options.routing_key+"] -> " +
                              info.type + ":" + info.id);
        p.emit('message', str, info);
      }
    });
  });
  client.on('close', function (e) {
    p.stats.closes++;
    p.clearHeartbeat();
    p.emit('close');
  });
};

module.exports = fh;
