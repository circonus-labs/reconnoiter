/*
Copyright (c) 2010-2016 Circonus, Inc. All rights reserved.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name Circonus, Inc. nor the names of its contributors
      may be used to endorse or promote products derived from this
      software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

var sys = require('util'),
    noit_connection = require('noit-connection').connection,
    loginfo = require('./log'),
    debug = global.debug;


var nc = function() {

    noit_connection.apply(this, arguments);

    // check existing stats for presence of this derived classes stats
    // using livestream_requests as a proxy for all stats
    if ( "undefined" === typeof(this.stats[this.remote].livestream_requests) ) {
        this.stats[this.remote].livestream_requests = 0;
        this.stats[this.remote].livestream_in = 0;
        this.stats[this.remote].livestream_out = 0;
        this.stats[this.remote].data_temp_feed_requests = 0;
        this.stats[this.remote].data_temp_feed_in = 0;
        this.stats[this.remote].data_temp_feed_out = 0;
    }

    if ( "undefined" === typeof(this.stats.global.livestream_requests) ) {
        this.stats.global.livestream_requests = 0;
        this.stats.global.livestream_in = 0;
        this.stats.global.livestream_out = 0;
        this.stats.global.data_temp_feed_requests = 0;
        this.stats.global.data_temp_feed_in = 0;
        this.stats.global.data_temp_feed_out = 0;
    }
}

sys.inherits(nc, noit_connection);

nc.prototype.request = function(ext, payload, cb) {
  if(typeof(payload) == 'function') {
    cb = payload;
    payload = null;
  }
  var options = { };
  for(var k in this.options) options[k] = this.options[k];
  options.hostname = this.options.host;
  options.path = ext.path;
  options.method = ext.method || 'GET';
  options.headers = ext.headers || {};
  if(ext.rejectUnauthorized !== undefined)
    options.rejectUnauthorized = ext.rejectUnauthorized;
  if(payload && payload.length)
    options.headers['Content-Length'] = payload.length;
  var req = https.request(options, function(res) {
    var data = '';
    var cert = req.connection.getPeerCertificate();
    res.on('data', function(d) { data = data + d; });
    res.on('end', function() { cb(res.statusCode, data); });
  });
  req.on('error', function(err) { cb(500, err); });
  if(payload && payload.length) req.write(payload);
  req.end();
}

function htonl(buf,b) {
  buf[0] = (b >> 24) & 0xff;
  buf[1] = (b >> 16) & 0xff;
  buf[2] = (b >> 8) & 0xff;
  buf[3] = b & 0xff;
}

function ntohl(buf) {
  return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}


nc.prototype.data_temp_feed = function() {
  this.stats.global.data_temp_feed_requests++;
  this.stats[parent.remote].data_temp_feed_requests++;
  this.start(function(parent) {
    var b = new Buffer(4);
    htonl(b,0x7e66feed);
    parent.socket.write(b);

    parent._buf = [];
    parent._inprocess = { };
    parent._want = 0;

    parent.socket.addListener('data', function(buffer) {
      if(parent.shutdown) return parent.stop();
      var listeners = parent.listeners('jlog');
      if(!listeners || listeners.length == 0) return parent.stop();
      for(var i = 0; i<buffer.length; i++) parent._buf.push(buffer[i]);
      var mlen = -1;
      while(1) {
        if(parent._last_seen && parent._want == 0) {
          var log = new Buffer(4), marker = new Buffer(4);
          htonl(log, parent._last_seen.chkpt_log);
          htonl(marker, parent._last_seen.chkpt_marker);
          parent.socket.write(log);
          parent.socket.write(marker);
          delete parent._last_seen;
        }
        if(parent._want == 0) {
          if(parent._buf.length < 4) break;
          parent._want = ntohl(parent._buf.splice(0,4));
        }
        if(!("len" in parent._inprocess)) {
          if(parent._buf.length < (4*5)) break;
          // We have enough, parse the header
          parent._inprocess.chkpt_log = ntohl(parent._buf.splice(0,4));
          parent._inprocess.chkpt_marker = ntohl(parent._buf.splice(0,4));
          parent._inprocess.sec = ntohl(parent._buf.splice(0,4));
          parent._inprocess.usec = ntohl(parent._buf.splice(0,4));
          parent._inprocess.len = ntohl(parent._buf.splice(0,4));
        }
        if("len" in parent._inprocess) {
          if(parent._buf.length < parent._inprocess.len) break;
          var line_buf = parent._buf.splice(0, parent._inprocess.len);
          parent._want--;
          var m = parent._last_seen = parent._inprocess;
          var buf = new Buffer(line_buf);
          m.line = buf.toString('ascii').replace(/\n/g, " ");
          var o = m.line.indexOf("\t");
          if(o) m.line = m.line.replace(/\t/, "\t" + parent.host + "\t");
          parent._inprocess = { };
          this.stats.global.data_temp_feed_in++;
          this.stats[parent.remote].data_temp_feed_in++;
          loginfo.parse(m.line, function(infos) {
            if(infos.constructor !== Array) infos = [infos];
            var i=0; cnt=infos.length;
            this.stats.global.data_temp_feed_out += cnt;
            this.stats[parent.remote].data_temp_feed_out += cnt;
            for(;i<cnt;i++) {
              var info = infos[i];
              if(!("data" in info)) info.data = m.line;
              parent.emit('jlog', info);
            }
          });
        }
        else break;
      }
    });
  });
}

nc.prototype.livestream = function(uuid,period,f) {
  this.stats.global.livestream_requests++;
  this.stats[this.remote].livestream_requests++;
  this.start(function(parent) {
    var b = new Buffer(4);
    htonl(b,0xfa57feed);
    parent.socket.write(b);      // request livestream
    htonl(b,period);
    parent.socket.write(b);      // at period milliseconds
    b = new Buffer(36);
    for(var i=0; i<36; i++) b[i] = uuid.charCodeAt(i);
    parent.socket.write(b);      // for this check uuid

    if(debug >= 3) console.log("livestream -> writing request");
    parent._buf = [];
    parent.body_need = 0;

    parent.socket.addListener('data', function(buffer) {
      if(debug >= 3) console.log("livestream <- [data]");
      if(parent.shutdown) return parent.stop();
      for(var i = 0; i<buffer.length; i++) parent._buf.push(buffer[i]);
      while(1) {
        if(parent.body_need == 0) {
          if(parent._buf.length < 4) break;
          parent.body_need = ntohl(parent._buf.splice(0,4));
        }
        if(parent.body_need > 0) {
          if(parent._buf.length < parent.body_need) break;
          var line_buf = parent._buf.splice(0, parent.body_need)
          parent.body_need = 0;

          var line = line_buf.map(function(a) { return String.fromCharCode(a) }).join('');
          var tidx = line.indexOf('\t');
          if(tidx > 0)
            line = line.substring(0, tidx+1) + parent.host + line.substring(tidx);
          if(debug >= 3) console.log("livestream <- " + (tidx > 0 ? line.substring(0, tidx) : "??"));
          parent.stats.global.livestream_in++;
          parent.stats[parent.remote].livestream_in++;
          loginfo.parse(line, function(infos) {
            if(infos.constructor !== Array) infos = [infos];
            var i=0; cnt=infos.length;
            parent.stats.global.livestream_out += cnt;
            parent.stats[parent.remote].livestream_out += cnt;
            for(;i<cnt;i++) {
              var info = infos[i];
              if('id' in info) {
                if(debug >= 3) console.log("livestream -> "+info.type+":"+info.id);
                // emulate a rouing key: check.uuid_nasty
                var account_id = ('circonus_account_id' in info) ? info.circonus_account_id : 0;
                var check_id = ('circonus_check_id' in info) ? info.circonus_check_id : 0;
                var route = 'check.' + account_id + '.' + check_id + '.' + info.id.replace(/-/g, '').split('').join('.');
                parent.emit('live', uuid, period, { routingKey: route }, info);
                if(f && !f({ routingKey: route }, info)) {
                  parent.stop();
                }
              }
            }
          });
        }
      }
    });
  });
};

exports.connection = nc;

