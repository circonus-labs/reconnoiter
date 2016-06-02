/*
Copyright (c) 2016 Circonus, Inc. All rights reserved.
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
    fs = require('fs'),
    net = require('net'),
    util = require('util'),
    https = require('https'),
    crypto = require('crypto'),
    tls = require('tls'),
    EventEmitter = require('events').EventEmitter,
    stats = {},
    debug = global.debug;

var MAX_FRAME_LEN = 65530,
    CMD_BUFF_LEN = 4096;

var nc = function(port,host,creds,cn) {
  this.shutdown = false;
  this.port = port;
  this.host = host;
  this.remote = host + ":" + port;
  this.stats = stats;
  if(! (this.remote in this.stats)) this.stats[this.remote] = {
    connects: 0, connections: 0, closes: 0
  };
  this.options = creds;
  this.options.host = host;
  this.options.port = port;
  if(!this.options.hasOwnProperty('rejectUnauthorized')) {
    this.options.rejectUnauthorized = false;
  }
  if(cn) {
    this.cn = cn;
    this.options.servername = cn;
  }
  this.channels = {};
};

sys.inherits(nc, EventEmitter);

nc.prototype.stop = function() {
  this.shutdown = true;
  if(this.socket) {
    this.socket.end();
    this.socket.destroy();
  }
  this.socket = null;
}

nc.prototype.start = function(conncb) {
  var parent = this;
  this.conncb = conncb;
  parent.stats.global.connects++;
  parent.stats[parent.remote].connects++;
  parent.stats.global.connections++;
  parent.stats[parent.remote].connections++;
  parent.socket = tls.connect(this.options,
    function() {
      if(parent.socket.authorized == false &&
         parent.socket.authorizationError != 'SELF_SIGNED_CERT_IN_CHAIN') {
        console.log('invalid cert: ' + parent.socket.authorizationError);
        throw('invalid cert: ' + parent.socket.authorizationError);
      }
      parent.remote_cert = parent.socket.getPeerCertificate();
      if(parent.cn && parent.remote_cert.subject.CN != parent.cn) {
        throw('invalid cert: CN mismatch');
      }
      parent.socket.authorized = true;
      parent.conncb(parent);
    }
  );
  this.socket.addListener('error', function(e) {
    console.log('noit ('+parent.host+'): '+e);
    parent.reverse_cleanup();
    setTimeout(function() { parent.start(parent.conncb); }, 1000);
  });
  this.socket.addListener('close',
    function() {
      parent.stats.global.closes++;
      parent.stats[parent.remote].closes++;
      parent.stats.global.connections--;
      parent.stats[parent.remote].connections--;
      if(!parent.socket) console.log('socket closed');
      else if (parent.socket.authorized == false) console.log('invalid cert ('+parent.host+')');
      parent.reverse_cleanup();
      if(!parent.shutdown) {
        if(parent.socket) parent.socket.destroy();
        parent.socket = null;
        setTimeout(function() { parent.start(parent.conncb); }, 1000);
      }
    }
  );
};


function decode_frame(blist) {
  var frame = {}
  var hdr = new Buffer(6); /* hdr uint16 + uint32 */
  var i, avail = 0, needed = 0, read_header = false;
  for(i=0;i<blist.length;i++) {
    needed = Math.min(blist[i].length, hdr.length - avail);
    blist[i].copy(hdr, avail, 0, needed);
    avail += needed;
    if(avail >= hdr.length) { /* header size */
      frame.channel_id = hdr.readUInt16BE(0);
      frame.command = (frame.channel_id & 0x8000) ? true: false;
      frame.channel_id &= 0x7fff;
      frame.bufflen = hdr.readUInt32BE(2);
      if(frame.bufflen > MAX_FRAME_LEN) {
        throw('oversized_frame: ' + frame.bufflen);
      }
      frame.buff = new Buffer(frame.bufflen);
      read_header = true;
      break;
    }
  }
  if(!read_header) return null;
  if(needed == blist[i].length) { /* we used the whole buffer */
    i++;
    needed = 0;
  }
  var start = needed;
  avail = 0;
  for(;i<blist.length;i++) {
    needed = Math.min(blist[i].length-start, frame.buff.length - avail);
    blist[i].copy(frame.buff, avail, start, (start+needed));
    avail += needed;
    if(avail == frame.buff.length) {
      /* we are complete... adjust out blist in place and return frame */
      if((start+needed) == blist[i].length) { i++; start = needed = 0; }
      if(i > 0) {
        blist.splice(0, i);
      }
      if((start+needed) != 0) {
        blist[0] = blist[0].slice((start+needed));
      }
      return frame;
    }
    start = 0;
  }
  return null;
}

nc.prototype.reverse_cleanup = function(conncb) {
  var parent = this;
  parent.buflist = [];
  if(parent.channels) {
    for(var channel_id in parent.channels) {
      var channel = parent.channels[channel_id];
      if(channel.socket) {
        channel.socket.end();
        channel.socket.destroy();
      }
    }
    parent.channels = {};
  }
}

var CMD_CONNECT = new Buffer('CONNECT', 'utf8'),
    CMD_SHUTDOWN = new Buffer('SHUTDOWN', 'utf8'),
    CMD_CLOSE = new Buffer('CLOSE', 'utf8'),
    CMD_RESET = new Buffer('RESET', 'utf8');

function frame_output(channel_id, command, buff) {
  var frame = new Buffer(6 + buff.length);
  buff.copy(frame, 6, 0, buff.length);
  frame.writeUInt16BE((channel_id & 0x7fff) | (command ? 0x8000 : 0), 0);
  frame.writeUInt32BE(buff.length, 2);
  if(debug >= 3) console.log('send', channel_id, command ? "comm" : "data", buff.length, frame.slice(0,6));
  return frame;
}

function isCmd(buf1, buf2) {
  if(buf1.length != buf2.length) return false;
  for(var i=0;i<buf1.length;i++) {
    if(buf1[i] != buf2[i]) return false;
  }
  return true;
}

function handle_frame(parent, frame, host, port) {
  if(!parent.channels[frame.channel_id])
    parent.channels[frame.channel_id] = { id: frame.channel_id };
  var chan = parent.channels[frame.channel_id];
  if(frame.command) {
    if(isCmd(frame.buff, CMD_CONNECT)) {
      if(chan.socket) {
        chan.socket.end();
        chan.socket.destroy();
        chan.socket = null;
        chan = parent.channels[frame.channel_id] = { id: chan.id };
        parent.socket.write(frame_output(chan.id, true, CMD_RESET));
      }
      chan.socket = net.connect( { host: host, port: port } );
      if(debug >= 2) console.log('connecting to ', host, ':', port, 'on channel', chan.id);
      chan.socket.on('error', (function(parent, chan) {
        return function(err) {
          if(debug >= 3) console.log('close to remote on channel', chan.id, err);
          parent.socket.write(frame_output(chan.id, true, CMD_RESET));
        }
      })(parent,chan));
      chan.socket.on('connect', (function(parent, chan) {
        return function() {
          if(debug >= 3) console.log('connect on channel', chan.id);
          if(chan.socket) {
            chan.socket.on('data', function(buff) {
              if(debug >= 3) console.log('data to remote on channel', chan.id);
              parent.socket.write(frame_output(chan.id, false, buff));
            });
            chan.socket.on('end', function(buff) {
              if(debug >= 3) console.log('close to remote on channel', chan.id);
              parent.socket.write(frame_output(chan.id, true, CMD_CLOSE));
            });
          }
        }})(parent,chan));
    }
    else if(isCmd(frame.buff, CMD_CLOSE) ||
            isCmd(frame.buff, CMD_RESET) ||
            isCmd(frame.buff, CMD_SHUTDOWN)) {
      if(chan.socket) {
        chan.socket.end();
        chan.socket.destroy();
        chan.socket = null;
        chan = parent.channels[frame.channel_id] = { id: chan.id };
      }
    }
  }
  else {
    if(chan.socket) {
      if(debug >= 3) console.log('data to local on channel', chan.id);
      chan.socket.write(frame.buff);
    }
    else {
      if(debug >= 2) console.log('reset to remote on channel', chan.id);
      parent.socket.write(frame_output(chan.id, true, CMD_RESET));
    }
  }
}

nc.prototype.reverse = function(name, host, port) {
  this.start(function(parent) {
    parent.buflist = [];
    var b = new Buffer("REVERSE /" + name + " HTTP/1.1\r\n\r\n", 'utf8');
    parent.socket.write(b);
    parent.socket.addListener('data', function(buffer) {
      parent.buflist.push(buffer);
      try {
        var frame;
        while(null !== (frame = decode_frame(parent.buflist))) {
          handle_frame(parent, frame, host, port);
        }
      }
      catch(e) {
        console.log(e, e.stack);
        parent.reverse_cleanup();
        parent.socket.end();
      }
    });
  });
}

exports.setCA =
nc.prototype.setCA = function(file) {
  https.globalAgent.options.ca = [ fs.readFileSync(file, {encoding:'utf8'}) ];
}

exports.noit_connection = nc;
exports.set_stats = function(external) {
  if(external === undefined || external == null) return stats;
  stats = external;
  if(! ("global" in stats)) {
    stats.global = {
      connects: 0, connections: 0, closes: 0
    };
  }
  return stats;
};

exports.set_stats(stats);
