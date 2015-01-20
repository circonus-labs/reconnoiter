/*
Copyright (c) 2010-2013 Circonus, Inc. All rights reserved.
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

var sys = require('sys'),
    fs = require('fs'),
    Histogram = require('./histogram'),
    zlib = require('zlib'),
    Schema = require('protobuf').Schema,
    schema = new Schema(fs.readFileSync(__dirname + "/bundle.desc")),
    bundle_protobuf = schema['Bundle'];

function make_b_parser(step1, step2, step3) {
  return function (original, cb) {
    step1(original, original,
      function(input, original) {
        step2(input, original,
          function(input, original) {
            step3(input, original, cb);
          }
        )
      }
    );
  }
}

function b64_decode(input, orig, next_step) {
  next_step(new Buffer(input.data, 'base64'), orig);
}
function decompress(input, orig, next_step) {
  zlib.inflate(input, function(err, inflated) {
    next_step(inflated, orig);
  });
}
function identity(input, orig, next_step) {
  next_step(input, orig);
}
function read_protobuf(input, orig, next_step) {
  var infos = [];
  try {
    var bobj = bundle_protobuf.parse(input);
    if(bobj.status) {
      var info = {
        type: 'S',
        agent: orig.agent,
        time: orig.time,
        id: orig.id,
        check_target: orig.check_target,
        check_module: orig.check_module,
        check_name: orig.check_name,
        circonus_account_id: orig.circonus_account_id,
        circonus_check_id: orig.circonus_check_id,
        state: bobj.status.state,
        availability: bobj.status.available,
        duration: bobj.status.duration,
        status: bobj.status.status
      };
      if(bobj.metadata) {
        info.metadata = {};
        bobj.metadata.map(function(o) { info.metadata[o.key] = o.value; });
      }
      infos.push(info);
    }
    if(bobj.metrics) {
      var len = bobj.metrics.length;
      for(var i=0;i<len;i++) {
        var metric = bobj.metrics[i];
        var metric_type = Buffer([metric.metricType]).toString();
        var m = {
          type: 'M',
          agent: orig.agent,
          time: orig.time,
          id: orig.id,
          check_target: orig.check_target,
          check_module: orig.check_module,
          check_name: orig.check_name,
          circonus_account_id: orig.circonus_account_id,
          circonus_check_id: orig.circonus_check_id,
          metric_name: metric.name,
          metric_type: metric_type,
          value: null
        };
        switch(metric_type) {
          case 's': m.value = metric.valueStr; break;
          case 'i': if('valueI32' in metric) m.value = metric.valueI32; break;
          case 'I': if('valueUI32' in metric) m.value = metric.valueUI32; break;
          case 'l': if('valueI64' in metric) m.value = metric.valueI64; break;
          case 'L': if('valueUI64' in metric) m.value = metric.valueUI64; break;
          case 'n': if('valueDbl' in metric) m.value = metric.valueDbl; break;
          default: throw("unknown metric_type: " + metric_type);
        }
        if(bobj.metadata) {
          info.metadata = {};
          bobj.metadata.map(function(o) { info.metadata[o.key] = o.value; });
        }
        infos.push(m);
      }
    }
  }
  catch(e) { infos.push({error: ""+e}); }
  next_step(infos);
}

function fixup_id(info) {
  if(info['id'].length > 36) {
    var full = info['id'];
    info['id'] = full.substring(full.length-36, full.length);
    var p1 = full.indexOf('`');
    if(p1 >= 0) {
      info['check_target'] = full.substring(0,p1++);
      var p2 = full.indexOf('`', p1);
      if(p2 >= 0) {
        info['check_module'] = full.substring(p1,p2++);
        if(p2 < full.length - 36) {
          info['check_name'] = full.substring(p2, full.length - 37);
          var m = /^c_(\d+)_(\d+)::/.exec(info['check_name']);
          if(m) {
            info['circonus_account_id'] = m[1];
            info['circonus_check_id'] = m[2];
          }
        }
      }
    }
  }
}

(function () {
  var fmts = {
    'C': [ 'agent', 'time', 'id', 'target', 'module', 'name' ],
    'S': [ 'agent', 'time', 'id', 'state', 'availability', 'duration', 'status' ],
    'M': [ 'agent', 'time', 'id', 'metric_name', 'metric_type', 'value' ],
    'B1': [ 'agent', 'time', 'id', 'target', 'module', 'name', 'raw_len', 'data' ],
    'B2': [ 'agent', 'time', 'id', 'target', 'module', 'name', 'raw_len', 'data' ],
    'H1': [ 'agent', 'time', 'id', 'metric_name', 'serialized_histogram' ]
  };
  var postprocs = {
    'B1': make_b_parser(b64_decode, decompress, read_protobuf),
    'B2': make_b_parser(b64_decode, identity, read_protobuf),
    'H1': function(original, cb) {
      original.histogram = Histogram.decode(original.serialized_histogram);
      cb(original);
    }
  };
  exports.parse = function(str, cb) {
      var tidx = str.indexOf('\t');
      var t = tidx > 0 ? str.substring(0, tidx) : str;
      var arr = tidx > 0 ? str.substring(tidx+1) : null;

      var fmt = fmts[t],
          postproc = postprocs[t],
          parts = [],
          info = {};

      if(arr == null) return;
      if(arr[arr.length-1] == '\n') arr = arr.substring(0, arr.length-1);
      if(fmt != null && arr != null) parts = arr.split("\t");
      if(/^\d+\.\d+$/.test(parts[0])) parts.unshift("unknown");
      if(parts.length > 0 && parts.length > fmt.length)
        parts.push(parts.splice(fmt.length-1).join('\t'));
      if(fmt == null || parts.length != fmt.length) {
        info = {type: 'unknown', data: arr==null ? t : [t,arr].join("\t") };
        if(postproc) postproc(info, cb);
        else cb(info);
        return;
      }
      info.type = t;
      for(var i=0; i<fmt.length; i++)
        info[fmt[i]] = parts[i];
      fixup_id(info);
      if(postproc) postproc(info, cb);
      else cb([info]);
    }
})();
