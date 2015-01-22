# Reconnoiter API library for node.js

    var noit = require('noit');

## Livestreaming check data

    var creds = noit.utils.hashToCreds({
      key: keyfile,
      cert: certfile,
      ca: cafile
    });
    var conn = new noit.connection(43191, host, creds, cn || host);

    conn.livestream(uuid, period_in_ms);
    conn.on('live', function(uuid,period,r,i) {
      console.log(this.r,i);
    });

## Reverse TCP tunnel

    var creds = noit.utils.hashToCreds({ ca: cafile });
    var proxy = new noit.connection(43191, host, creds, cn || host);
    var tgt_host = '127.0.0.1', tgt_port = 2609;
    proxy.reverse('check/' + uuid, tgt_host, tgt_port);

## Tap the firehose

    var params = { host: fqhost, exchange:'noit.firehose' },
        stats = {},
        hose = new noit.firehose(params, stats);

    hose.on('message', function(str, info) {
      console.log(info);
    });
    hose.listen();
    setInterval(function() { console.log(stats); }, 1000);
