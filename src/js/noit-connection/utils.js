var fs = require('fs'),
    sys = require('util'),
    crypto = require('crypto');

exports.hashToCreds = function(h, compiled) {
  if(compiled == null) compiled = true;
  var interest = ['key', 'cert', 'ca'];
  var creds = {};
  for (var i=0; i<interest.length; i++) {
    var k = interest[i];
    if (k in h) creds[k] = fs.readFileSync(h[k]).toString();
  }
  for(var k in h) if(!(k in creds)) creds[k] = h[k];
  return compiled ? crypto.createCredentials(creds) : creds;
}

exports.bail = function(a) {
  switch(typeof(a)) {
    case 'string':
    case 'number':
    case 'undefined':
      sys.puts('noit.bail: ' + a); break;
    default:
      sys.puts('noit.bail: ');
      sys.puts(sys.inspect(a));
  }
  process.exit(1);
}
