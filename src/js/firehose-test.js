var debug = false;
var fh = require('noit').firehose;

var stats = {}
var hose = new fh({ host: 'mq3.dev.circonus.net'
                  , program: 'prefix:"check." sample(0.01)'
                  },
                  stats);
hose.on('message', function(str, info) {
  console.log(info);
});
hose.listen();

setInterval(function() { console.log(stats); }, 1000);
