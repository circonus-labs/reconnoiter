#!/usr/bin/env node
var tools = require('./testconfig'),
    runtests = require('trial').runtests,
    runenv = { require: require, tools: tools, suppress: {}, verbose: 0 },
    dir = '.', i;

for(i=2, stop=false; i<process.argv.length; i++) {
  switch(process.argv[i]) {
    case '-tap': runenv.tap = 1; break;
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
trial.on('complete', function(code) {
  tools.do_cleanup(code);
})
if(runenv.verbose > 1) {
  console.log("Registering verbose progress tracker...");
  trial.on('progress', function(trial, test, msg, status) {
    console.log(" -> " + test.name + "/" + msg.name + " : " + status);
  });
}
