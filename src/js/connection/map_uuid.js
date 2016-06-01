var sys = require('sys'),
    Client = require('pg').Client;

var mapper = function(config) {
  this.config = config;
};

mapper.prototype.connect = function() {
  var m = this;
  this.client = new Client(this.config);
  this.client.on('error', function(error) {
    sys.puts("postgres: " + error.severity + " " +
             error.name + " " + error.message);
    setTimeout(function() { m.client.end(); m.connect(); }, 1000);
  });           
  this.client.connect();
}

mapper.prototype.map = function(uuid, f) {
  var preparedStatement = this.client.query({
    name: 'noit by uuid',
    text: this.config.sql,
    values: [uuid]
  });
  preparedStatement.on('row', function(row){ f(row.noit); });
};

mapper.prototype.noit_config = function(remote_cn, f) {
  var preparedStatement = this.client.query({
    name: 'config by noit',
    text: this.config.noitconf,
    values: [remote_cn]
  });
  preparedStatement.on('row', function(row){ f(row.config); })
                   .on('end', function() { f(); });
}

exports.mapper = mapper;
