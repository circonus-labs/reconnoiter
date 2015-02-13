#!/usr/bin/env node
var tools = require('./testconfig'),
    runtests = require('trial').runtests,
    runenv = { require: require, tools: tools, suppress: {}, verbose: 0 },
    dir = '.', i,
    deadman = 300; # Five minute deadman default

for(i=2, stop=false; i<process.argv.length; i++) {
  switch(process.argv[i]) {
    case '-tap': runenv.tap = 1; break;
    case '-d': deadman = parseInt(process.argv[++i]); break;
    case '-v': runenv.verbose++; break;
    case '-s': runenv.summary = 1; break;
    case '-i': runenv.incremental_reporting = 1; break;
    case '-b': runenv.brief = 1; break;
    case '-S': runenv.suppress[process.argv[++i]] = true; break;
    default: stop = true; break;
  }
  if(stop) break;
}
if(i < process.argv.length) dir = process.argv[i];

var trial = runtests(dir, runenv);
trial.noexit();
if(deadman) {
  deadman = setTimeout(function() {
    console.log("Deadman timer fired! Aborting test suite.");
    process.exit(-1);
  }, 1000*deadman);
}
trial.on('complete', function(code) {
  if(deadman) clearTimeout(deadman);
  tools.do_cleanup(code);
})
if(runenv.verbose > 1) {
  console.log("Registering verbose progress tracker...");
  trial.on('progress', function(trial, test, msg, status) {
    console.log(" -> " + test.file + "/" + msg.name + " : " + status);
  });
}
