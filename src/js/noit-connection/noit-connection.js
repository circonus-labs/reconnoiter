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

var crypto = require('crypto');
var EventEmitter = require('events').EventEmitter;
var fs = require('fs');
var https = require('https');
var net = require('net');
var tls = require('tls');
var util = require('util');

var debug = global.debug || 0;
var stats = {};

// maximum amount of time (ms) between communications
// e.g. if interval between the last rx or tx and "now"
// is >MAX_COMM_INTERVAL, reset the reverse connection.
// this is a brute force timer, when communications get
// wedged and the socket timeout does not fire (for whatever
// reason, exact circumstances are not easy to replicate.).
var MAX_COMM_INTERVAL = 300 * 1000; // maximum amount of time between communications
var SOCKET_TIMEOUT = 90 * 1000;     // if no communications in this time, reset reverse
var WATCHDOG_ENABLED = true;
var WATCHDOG_INTERVAL = 60 * 1000;  // frequency of enforcing MAX_COMM_INTERVAL

var CMD_BUFF_LEN = 4096;
var MAX_FRAME_LEN = 65530;

function logTS() {
    return debug > 0 ? (new Date()).toISOString() : '';
}

function getRetryInterval() {
    var minRetryInterval = 5 * 1000; // 5 seconds
    var maxRetryInterval = 30 * 1000; // 30 seconds
    return Math.floor(Math.random() * (maxRetryInterval - minRetryInterval + 1)) + minRetryInterval;
}

var nc = function(port, host, creds, cn) {
    this.shutdown = false;
    this.port = port;
    this.host = host;
    this.remote = host + ":" + port;
    this.stats = stats;

    if (!(this.remote in this.stats)) {
        this.stats[this.remote] = {
            connects: 0, connections: 0, closes: 0
        };
    }

    this.options = creds;
    this.options.host = host;
    this.options.port = port;

    if (!this.options.hasOwnProperty('rejectUnauthorized')) {
        this.options.rejectUnauthorized = false;
    }

    if (cn) {
        this.cn = cn;
        this.options.servername = cn;
    }

    this.channels = {};

    this.commTracker = {
        lastRx: 0,
        lastTx: 0
    };
    this.watchdog = null;
    this.buflist = [];
    this.watchdogEnabled = WATCHDOG_ENABLED;
};

util.inherits(nc, EventEmitter);

nc.prototype.setDebug = function setDebug(debugLevel) {
    debug = debugLevel;
};

nc.prototype.commWatchdog = function commWatchdog(parent) {
    if (debug >= 4) {
        var dte = new Date();
        console.log("\n====================", "watchdog", "start");
        console.log(dte.toISOString());
    }

    if (parent.watchdog !== null) {
        if (debug >= 5) {
            console.log("clearing watchdog (inside watchdog)");
        }
        clearTimeout(parent.watchdog);
        parent.watchdog = null;
    }

    var stateOk = true;

    if (parent.commTracker.lastRx > 0 && parent.commTracker.lastTx > 0) {
        var ts = Date.now();
        var rxInterval = ts - parent.commTracker.lastRx;
        var txInterval = ts - parent.commTracker.lastTx;
        stateOk = rxInterval < MAX_COMM_INTERVAL && txInterval < MAX_COMM_INTERVAL
        if (debug >= 4) {
            console.log("last rx", rxInterval > 1000 ? Math.round(rxInterval / 1000) + "s" : rxInterval + "ms", "ago.");
            console.log("last tx", txInterval > 1000 ? Math.round(txInterval / 1000) + "s" : txInterval + "ms", "ago.");
            if (debug >= 5) {
                console.log("rx/tx interval ok?", stateOk);
            }
        }
    }

    if (stateOk) {
        if (debug >= 5) {
            console.log("setting watchdog (inside watchdog)");
        }
        parent.watchdog = setTimeout(parent.commWatchdog, WATCHDOG_INTERVAL, parent);
    }
    else {
        console.error("!!!WATCHDOG!!! resetting NAD reverse connection due to excessive interval since last communication with", parent.host);
        if (debug >= 4) {
            console.error("rx", rxInterval, MAX_COMM_INTERVAL, rxInterval < MAX_COMM_INTERVAL);
            console.error("tx", txInterval, MAX_COMM_INTERVAL, txInterval < MAX_COMM_INTERVAL);
        }
        if (parent.socket) {
            parent.socket.end();
        }
    }
    if (debug >= 4) {
        console.log("====================", "watchdog", "end\n");
    }
}

nc.prototype.toggleWatchdog = function toggleWatchdog() {
    if (this.watchdogEnabled) {
        this.watchdogEnabled = false;
        if (this.watchdog !== null) {
            clearTimeout(this.watchdog);
        }
    } else {
        this.watchdogEnabled = true;
        this.commWatchdog(this);
    }
}

nc.prototype.stop = function() {
    if (debug >= 2) {
        console.log(logTS(), "Stopping reverse connection to", this.host);
    }

    if (this.channels) {
        for (var channel_id in this.channels) {
            var channel = this.channels[channel_id];
            if (channel.socket) {
                channel.socket.end();
                channel.socket.destroy();
            }
        }
    }

    if (this.socket) {
        this.socket.end();
        this.socket.destroy();
    }

    this.buflist = [];
    this.channels = {};
    this.socket = null;
    this.commTracker = {
        lastRx: 0,
        lastTx: 0
    };
    this.shutdown = true;
    this.watchdog = null;
}

nc.prototype.start = function(conncb) {
    var parent = this;

    if (debug >= 2) {
        console.log(logTS(), "Starting reverse connection to", this.host);
    }

    if (this.watchdog !== null) {
        if (debug >= 5) {
            console.log(logTS(), "Clearing watchdog (inside start)");
        }
        clearTimeout(this.watchdog);
        this.watchdog = null;
    }

    this.conncb = conncb;
    this.stats.global.connects++;
    this.stats[this.remote].connects++;
    this.stats.global.connections++;
    this.stats[this.remote].connections++;

    this.socket = tls.connect(this.options, function() {
        if (parent.socket.authorized == false &&
            parent.socket.authorizationError != 'SELF_SIGNED_CERT_IN_CHAIN') {
                var err = new Error(util.format("Invalid cert: %s", parent.socket.authorizationError));
                return parent.conncb(err, parent);
        }
        parent.remote_cert = parent.socket.getPeerCertificate();
        if (parent.cn && parent.remote_cert.subject.CN != parent.cn) {
            var err = new Error(util.format("Invalid cert: CN mismatch %s != %s", parent.cn, parent.remote_cert.subject.CN));
            return parent.conncb(err, parent);
        }
        parent.socket.authorized = true;
        parent.conncb(null, parent);
        console.error(logTS(), "Established reverse connection to remote", parent.host);
        if (parent.watchdogEnabled && parent.watchdog === null) {
            if (debug >= 5) {
                console.log(logTS(), "Setting watchdog (inside start connect cb)");
            }
            parent.watchdog = setTimeout(parent.commWatchdog, WATCHDOG_INTERVAL, parent);
        }
    });

    this.socket.addListener('error', function(e) {
        console.error(logTS(), "Remote", parent.host, e);
        if (parent.socket) {
            if (debug >= 5) {
                console.log(logTS(), "Socket end to trigger close");
            }
            parent.socket.end(); // trigger close event
        }
        else {
            if (debug >= 5) {
                console.log(logTS(), "Parent stop and re-call start (no parent.socket)");
            }
            parent.stop();      // force cleanup
            setTimeout(function() { parent.start(parent.conncb); }, getRetryInterval());
        }
    });

    // enforce a timeout, normally there is no tiemout on sockets.
    this.socket.setTimeout(SOCKET_TIMEOUT);
    this.socket.addListener('timeout', function() {
        console.error(logTS(), "Remote", parent.host, "timeout(event)",
            "last RX:", parent.commTracker.lastRx ? (new Date(parent.commTracker.lastRx)).toISOString() : "unknown",
            "last TX:", parent.commTracker.lastTx ? (new Date(parent.commTracker.lastTx)).toISOString() : "unknown");
        if (parent.socket) {
            if (debug >= 5) {
                console.log(logTS(), "Socket end to trigger close");
            }
            parent.socket.end(); // trigger close event
        }
        else {
            if (debug >= 5) {
                console.log(logTS(), "Parent stop and re-call start (no parent.socket)");
            }
            parent.stop();      // force cleanup
            setTimeout(function() { parent.start(parent.conncb); }, getRetryInterval());
        }
    });

    this.socket.addListener('close', function() {
        console.error(logTS(), "Remote", parent.host, "close(event)");
        if (parent.watchdog !== null) {
            if (debug >= 5) {
                console.log(logTS(), "Clearing watchdog (inside start:close(event))");
            }
            clearTimeout(parent.watchdog);
            parent.watchdog = null;
        }
        parent.stats.global.closes++;
        parent.stats[parent.remote].closes++;
        parent.stats.global.connections--;
        parent.stats[parent.remote].connections--;
        if (!parent.socket) {
            console.log(lotTS(), 'socket is already closed');
        }
        else if (parent.socket.authorized == false) {
            console.log(logTS(), "Invalid cert for", parent.host);
        }
        parent.stop();
        var retryInterval = getRetryInterval();
        console.error(logTS(), "Attempting to re-establish reverse connection in", Math.round(retryInterval / 1000)+"s.");
        setTimeout(function() { parent.start(parent.conncb); }, retryInterval);
    });
};


function decode_frame(blist) {
    var frame = {};
    var hdr = new Buffer(6); /* hdr uint16 + uint32 */
    var i, avail = 0, needed = 0, read_header = false;

    for (i = 0; i < blist.length; i++) {
        needed = Math.min(blist[i].length, hdr.length - avail);
        blist[i].copy(hdr, avail, 0, needed);
        avail += needed;
        if (avail >= hdr.length) { /* header size */
            frame.channel_id = hdr.readUInt16BE(0);
            frame.command = (frame.channel_id & 0x8000) ? true: false;
            frame.channel_id &= 0x7fff;
            frame.bufflen = hdr.readUInt32BE(2);
            if (frame.bufflen > MAX_FRAME_LEN) {
                throw('oversized_frame: ' + frame.bufflen); // try/catch wrap in caller, not a callback
            }
            frame.buff = new Buffer(frame.bufflen);
            read_header = true;
            break;
        }
    }

    if (!read_header) {
         return null;
    }

    if (needed == blist[i].length) { /* we used the whole buffer */
        i++;
        needed = 0;
    }

    var start = needed;

    avail = 0;
    for( ; i < blist.length; i++) {
        needed = Math.min(blist[i].length-start, frame.buff.length - avail);
        blist[i].copy(frame.buff, avail, start, (start+needed));
        avail += needed;
        if (avail == frame.buff.length) {
            /* we are complete... adjust out blist in place and return frame */
            if ((start+needed) == blist[i].length) {
                i++;
                start = needed = 0;
            }

            if (i > 0) {
                blist.splice(0, i);
            }

            if ((start+needed) != 0) {
                blist[0] = blist[0].slice((start+needed));
            }

            return frame;
        }
        start = 0;
    }
    return null;
}

nc.prototype.reverse_cleanup = function() {
    this.buflist = [];
    if (this.channels) {
        for (var channel_id in this.channels) {
            var channel = this.channels[channel_id];
            if (channel.socket) {
                channel.socket.end();
                channel.socket.destroy();
            }
        }
        this.channels = {};
    }
    this.commTracker = {
        lastRx: 0,
        lastTx: 0
    };
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
    if (debug >= 4) {
        console.log(logTS(), 'Prepared', command ? "'comm'" : "'data'", "for channel", channel_id,
            "- buffer length", buff.length, frame.slice(0,6).toJSON());
    }
    return frame;
}

function isCmd(buf1, buf2) {
    if (buf1.length != buf2.length) {
        return false;
    }
    for (var i = 0; i < buf1.length; i++) {
        if (buf1[i] != buf2[i]) {
            return false;
        }
    }
    return true;
}

function handle_frame(parent, frame, host, port) {
    if (!parent.channels[frame.channel_id]) {
        parent.channels[frame.channel_id] = { id: frame.channel_id };
    }

    var chan = parent.channels[frame.channel_id];

    if (frame.command) {
        if (isCmd(frame.buff, CMD_CONNECT)) {
            if (chan.socket) {
                chan.socket.end();
                chan.socket.destroy();
                chan.socket = null;
                chan = parent.channels[frame.channel_id] = { id: chan.id };
                parent.socket.write(frame_output(chan.id, true, CMD_RESET));
            }

            if (debug >= 2) {
                console.log(logTS(), "Connecting to", host, "on port", port, "channel", chan.id);
            }
            chan.socket = net.connect( { host: host, port: port } );

            chan.socket.on('connect', (function(parent, chan) {
                return function() {
                    if (debug >= 3) {
                        console.log(logTS(), "Connected to", host, "on port", port, "channel", chan.id);
                    }
                    if (chan.socket) {
                        chan.socket.on('data', function(buff) {
                            if (debug >= 3) {
                                console.log(logTS(), "Sending 'data' from local to", parent.host, "for channel", chan.id);
                            }
                            var bl = buff.length;
                            if (bl <= MAX_FRAME_LEN) {
                                parent.socket.write(frame_output(chan.id, false, buff));
                                parent.commTracker.lastTx = Date.now(); // update lastTx AFTER successful socket.write
                            } else {
                                var os = 0;
                                while (os < bl) {
                                    var tb_size = Math.min(MAX_FRAME_LEN, bl - os);
                                    var tempbuff = new Buffer(tb_size);
                                    buff.copy(tempbuff, 0, os, os + tb_size);
                                    parent.socket.write(frame_output(chan.id, false, tempbuff));
                                    parent.commTracker.lastTx = Date.now(); // update lastTx AFTER successful socket.write
                                    os += tempbuff.length;
                                }
                            }
                            if (debug >= 3) {
                                console.log(logTS(), "Sent 'data' from local to", parent.host, "for channel", chan.id);
                            }
                        });

                        chan.socket.on('end', function(buff) {
                            if (debug >= 3) {
                                console.log(logTS(), "Sending close command to", parent.host, "for channel", chan.id);
                            }
                            parent.socket.write(frame_output(chan.id, true, CMD_CLOSE));
                            parent.commTracker.lastTx = Date.now(); // update lastTx AFTER successful socket.write
                            if (debug >= 3) {
                                console.log(logTS(), "Sent close command to", parent.host, "for channel", chan.id);
                            }
                        });
                    }
                    else {
                        console.error(logTS(), "warn: connect event on chan.socket but no chan.socket...");
                    }
                };
            })(parent, chan));

            chan.socket.on('error', (function(parent, chan) {
                return function(err) {
                    if (debug >= 3) {
                        console.log(logTS(), "Sending reset command to", parent.host, "for channel", chan.id, err);
                    }
                    parent.socket.write(frame_output(chan.id, true, CMD_RESET));
                    if (debug >= 3) {
                        console.log(logTS(), "Sent reset command to", parent.host, "for channel", chan.id);
                    }
                };
            })(parent, chan));

        }
        else if (isCmd(frame.buff, CMD_CLOSE) ||
            isCmd(frame.buff, CMD_RESET) ||
            isCmd(frame.buff, CMD_SHUTDOWN)) {

            if (debug >= 2) {
                console.log(logTS(), "close/reset/shutdown command received from", parent.host, "for channel", chan.id);
            }
            if (chan.socket) {
                chan.socket.end();
                chan.socket.destroy();
                chan.socket = null;
                chan = parent.channels[frame.channel_id] = { id: chan.id };
            }
            else {
                console.error(logTS(), "warn: no chan.socket to close/reset/shutdown");
            }
        }
    }
    else {
        if (chan.socket) {
            if (debug >= 3) {
                console.log(logTS(), "Sending data to local for channel", chan.id);
            }
            chan.socket.write(frame.buff);
            if (debug >= 3) {
                console.log(logTS(), "Sent data to local for channel", chan.id);
            }
        }
        else {
            if (debug >= 3) {
                console.log(logTS(), "Sending reset command to", parent.host, "for channel", chan.id);
            }
            parent.socket.write(frame_output(chan.id, true, CMD_RESET));
            if (debug >= 3) {
                console.log(logTS(), "Sent reset command to", parent.host, "for channel", chan.id);
            }
        }
    }
}

nc.prototype.reverse = function(name, host, port) {
    this.start(function(err, parent) {
        if (debug >= 2) {
            console.log(logTS(), "tls.connect to", parent.host, "for", name, "local:", host, "port", port);
        }
        if (err !== null) {
            console.error(logTS(), "resetting connection to", parent.host, err);
            if (parent.socket) {
                parent.socket.end();
            }
        }
        parent.buflist = [];
        var b = new Buffer("REVERSE /" + name + " HTTP/1.1\r\n\r\n", 'utf8');
        parent.socket.write(b);
        parent.socket.addListener('data', function(buffer) {
            if (debug >= 3) {
                console.log(logTS(), "Received data packet from", parent.host);
            }
            parent.commTracker.lastRx = Date.now();
            parent.buflist.push(buffer);
            try {
                var frame;
                while (null !== (frame = decode_frame(parent.buflist))) {
                    handle_frame(parent, frame, host, port);
                }
            }
            catch(e) {
                console.error(lotTS(), "handling incoming packet from", parent.host, e, e.stack);
                parent.socket.end();
            }
        });
    });
};

exports.setCA = nc.prototype.setCA = function(file) {
    https.globalAgent.options.ca = [ fs.readFileSync(file, {encoding:'utf8'}) ];
};

exports.noit_connection = nc;

exports.set_stats = function(external) {
    if (external === undefined || external == null) {
        return stats;
    }
    stats = external;
    if (! ("global" in stats)) {
        stats.global = {
            connects: 0, connections: 0, closes: 0
        };
    }
    return stats;
};

exports.set_stats(stats);
