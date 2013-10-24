var local_skew = 0;
var check_search = "";
var capa = {};
var startTime = +new Date();
function refresh_capa() {
  $.ajax("/capa.json").done(function (x) {
    capa = x;
    var endTime = +new Date();
    var lat_ms = (endTime - startTime)/2;
    if(capa.current_time)
      local_skew = (+new Date()) - (parseFloat(capa.current_time) + lat_ms);

    $("#noit-version").text(capa.version);
    var features = []
    for (var feature in capa.features) features.push(feature);
    $("#noit-feature-list").text(features.join(", "));
    var modules = []
    for (var module in capa.modules) modules.push(module);
    $("#noit-module-list").text(modules.join(", "));
    var todo = { "noit-build": capa.unameBuild, "noit-run": capa.unameRun };
    for (var id in todo) {
      var info = todo[id];
      info.system = info.sysname + "/" + info.release + " " +
                    info.machine + " (" + info.bitwidth + "bit)";
      info.version = info.version.replace(/(Darwin.*Version [\d\.]+):(.*?;)/, '');
      var $dl = $("<dl/>").addClass("dl-horizontal");
      ["system","nodename","version"].forEach(
        function (x){
          $dl.append($("<dt/>").text(x));
          $dl.append($("<dd/>").text(info[x]));
        }
      );
      $("#"+id).html($dl);
    }
    var $sul = $("<ul/>").addClass("noit-listener");
    for(var s in capa.services) {
      var service = {};
      console.log(s);
      for (var cmd in capa.services[s].commands) {
        var detail = capa.services[s].commands[cmd];
        if(!service.hasOwnProperty(detail.name + "/" + detail.version))
          service[detail.name + "/" + detail.version] = [];
        service[detail.name + "/" + detail.version].push(cmd);
      }
      var $li = $("<li/>");
      $li.append($("<span/>").text(s));
      var $sub = $("<ul/>").addClass("noit-proto");
      for(var proto in service) {
        var $sli = $("<li/>").append($("<span/>").text(proto));
        var $cmdul = $("<ul/>").addClass("noit-proto-cmd");
        service[proto].forEach(function(cmd) {
          $cmdul.append($("<li/>").text(cmd));
        });
        $sli.append($cmdul);
        $sub.append($sli);
      }
      $li.append($sub);
      $sul.append($li);
      console.log(service);
    }
    $("#noit-services").append($sul);
  });
}
refresh_capa();

function pretty_mb(mb) {
  var d = function(a) { return '<span class="text-muted">'+a+'</span>'; };
  if(mb < 1) return (mb / 1024).toFixed(0) + d("kb");
  if(mb > 1024) return (mb / 1024).toFixed(0) + d("Gb");
  if(mb > 1024*1024) return (mb / (1024*1024)).toFixed(0) + d("Tb");
  return parseFloat(mb).toFixed(0) + d("Mb");
}
function nice_date(s) {
  var d = new Date(s);
  var year = d.getFullYear();
  var month = d.getMonth();
  var day = d.getDate();
  var hour = d.getHours();
  var minute = d.getMinutes();
  var second = d.getSeconds();
  return year + "/" +
         ((month < 10) ? "0" : "") + month + "/" +
         ((day < 10) ? "0" : "") + day + " " +
         ((hour < 10) ? "0" : "") + hour + ":" +
         ((minute < 10) ? "0" : "") + minute + ":" +
         ((second < 10) ? "0" : "") + second;
}
function nice_time(s) {
  var negative = (s < 0);
  var days, hours, minutes, seconds;
  s = Math.abs(s);
  days = Math.floor(s/86400); s -= (86400*days);
  hours = Math.floor(s/3600); s -= (3600*hours);
  minutes = Math.floor(s/60); s -= (60*minutes);
  var time = "";
  if(days) time = time + days + "d"
  if(hours) time = time + hours + "h"
  if(minutes) time = time + minutes + "m"
  if(s || time === "") {
    if(s == Math.floor(s))
      time = time + s + "s"
    else
      time = time + parseFloat(s).toFixed(3) + "s"
  }
  if(negative) time = time + " ago";
  return time;
}

function $badge(n) {
  return $("<span class=\"badge\"/>").text(n);
}
function $label(n, type) {
  if(!type) type = "default";
  return $("<span class=\"label label-"+type+"\"/>").text(n);
}
function $nice_type(t) {
  if(t == "s") t = "string";
  else if(t == "i") t = "int32";
  else if(t == "I") t = "uint32";
  else if(t == "l") t = "int64";
  else if(t == "L") t = "uint64";
  else if(t == "n") t = "double";
  return $label(t, "info");
}
function $flags(which, flag) {
  var $d = $("<div/>");
  if(which & 1) {
    if(flag & 0x1) $d.append($label("running","primary"));
    if(flag & 0x2) $d.append($label("killed","danger"));
    if(flag & 0x4) $d.append($label("disabled","default"));
    if(flag & 0x8) $d.append($label("unconfig","warning"));
  }
  if(which & 2) {
    if(flag & 0x10) $d.append($label("T","success"));
    if(flag & 0x40) $d.append($label("R","info"));
    else if(flag & 0x20) $d.append($label("R?","info"));
    if(flag & 0x1000) $d.append($label("-S","warning"));
    if(flag & 0x2000) $d.append($label("-M","warning"));
    if(flag & 0x4000) {
      if(flag & 0x8000) $d.append($label("v6","default"));
      else $d.append($label("v6,v4","default"));
    } else {
      if(flag & 0x8000) $d.append($label("v4","default"));
      //else $d.append($label("v4,v6","default"));
    }
    if(flag & 0x00010000) $d.append($label("P", "info"));
  }
  return $d;
}
function mk_jobq_row(jobq,detail) {
  var $tr = $("<tr/>");
  $tr.append($("<td/>").html($badge(detail.backlog+detail.inflight)));
  $tr.append($("<td/>").text(jobq));
  var $conc = $("<td class=\"text-center\"/>");
  if(detail.desired_concurrency == 0)
    $conc.text("N/A");
  else if(detail.concurrency == detail.desired_concurrency)
    $conc.text(detail.concurrency);
  else
    $conc.html(detail.concurrency + " &rarr; " + detail.desired_concurrency);
  $tr.append($conc);
  $tr.append($("<td class=\"text-right\"/>").append($badge(detail.total_jobs)));
  $tr.append($("<td class=\"text-right\"/>").append($("<small/>").text(parseFloat(detail.avg_wait_ms).toFixed(3) + "ms")));
  $tr.append($("<td class=\"text-left\"/>").append($badge(detail.backlog)));
  $tr.append($("<td class=\"text-right\"/>").append($("<small/>").text(parseFloat(detail.avg_run_ms).toFixed(3) + "ms")));
  $tr.append($("<td class=\"text-left\"/>").append($badge(detail.inflight)));
  return $tr;
}
function mk_timer_row(event) {
  var $tr = $("<tr/>");
  $tr.append($("<td/>").text(event.callback));
  $tr.append($("<td/>").text(new Date(event.whence)));
  return $tr;
}
function mk_socket_row(event) {
  var $tr = $("<tr/>");
  var mask = [];
  if(event.mask & 1) mask.push("R");
  if(event.mask & 2) mask.push("W");
  if(event.mask & 4) mask.push("E");
  $tr.append($("<td/>").html(event.fd + "&nbsp").append($badge(mask.join("|")).addClass('pull-right')));
  $tr.append($("<td/>").html('<small>'+event.impl+'</small>'));
  $tr.append($("<td/>").text(event.callback));
  $tr.append($("<td/>").text(event.local ? event.local.address+":"+event.local.port : "-"));
  $tr.append($("<td/>").text(event.remote ? event.remote.address+":"+event.remote.port : "-"));
  return $tr;
}

function update_eventer(uri, id, mkrow) {
  return function(force) {
    var $table = $("div#"+id+" table" + (force ? "" : ":visible"));
    if($table.length == 0) return;
    var st = jQuery.ajax(uri);
    st.done(function( events ) {
      var $tbody = $("<tbody/>");
      if(events.hasOwnProperty('length')) {
        events.forEach(function(event) {
          $tbody.append(mkrow(event));
        });
      } else {
        var keys = [];
        for(var name in events) keys.push(name);
        keys.sort().forEach(function(name) {
          $tbody.append(mkrow(name, events[name]));
        });
      }
      $table.find("tbody").replaceWith($tbody);
    });
  };
}

setInterval(update_eventer("/eventer/sockets.json",
                           "eventer-sockets", mk_socket_row),
            5000);
$('#eventer-sockets').on('shown.bs.collapse', function () {
  update_eventer("/eventer/sockets.json",
                 "eventer-sockets", mk_socket_row)();
});
update_eventer("/eventer/sockets.json",
               "eventer-sockets", mk_socket_row)(true);

setInterval(update_eventer("/eventer/timers.json",
                           "eventer-timers", mk_timer_row),
            5000);
$('#eventer-timers').on('shown.bs.collapse', function () {
  update_eventer("/eventer/timers.json",
                 "eventer-timers", mk_timer_row)();
});

setInterval(update_eventer("/eventer/jobq.json",
                           "eventer-jobq", mk_jobq_row),
            5000);
$('#eventer-jobq').on('shown.bs.collapse', function () {
  update_eventer("/eventer/jobq.json",
                 "eventer-jobq", mk_jobq_row)();
});

function check_refresh(check) {
  return function() {
    if($("tr#check-details-" + check.id + ":visible").length == 0) {
      clearInterval(check.intervalId);
      return;
    }
    var st = $.ajax("/checks/show/" + check.id + ".json");
    st.done(function(x) {
      var $c = $("tr#check-details-"+check.id + " td div");
      $c.find(".check-detail-uuid").text(check.id);
      $c.find(".check-detail-name").text(x.name);
      $c.find(".check-detail-module").text(x.module);
      $c.find(".check-detail-target").text(x.target);
      $c.find(".check-detail-target-ip").text(x.target_ip);
      $c.find(".check-detail-filterset").text(x.filterset);
      $c.find(".check-detail-period").text(nice_time(x.period/1000.0));
      $c.find(".check-detail-timeout").text(nice_time(x.timeout/1000.0));
      $c.find(".check-detail-last-run").data("date", x.last_run);
      $c.find(".check-detail-next-run").data("date", x.next_run);
      var $dl = $c.find("dl.check-config-container");
      var config = $dl.data("config");
      if(JSON.stringify(config) !== JSON.stringify(x.config)) {
        $dl.find("dd").remove();
        $dl.find("dt").remove();
        for(var key in x.config) {
          $dl.append($("<dt/>").text(key));
          $dl.append($("<dd/>").text(x.config[key]));
        }
        $dl.data("config", x.config);
      }
      var keys = [];
      var $tbody = $c.find(".check-metrics tbody");
      for (var key in x.metrics.current) keys.push(key);
      keys.sort();
      var metrics = $tbody.data("metrics");
      if(JSON.stringify(metrics) !== JSON.stringify(keys)) {
        $tbody.find("tr").remove();
        for(var kidx in keys) {
          var key = keys[kidx];
          var $tr = $("<tr/>");
          $tr.append($("<td/>").text(key));
          $tr.append($("<td class=\"metric-type\"/>").html($nice_type(x.metrics.current[key]._type)));
          $tr.append($("<td class=\"metric-value\"/>").text(x.metrics.current[key]._value));
          $tbody.append($tr);
        }
        $tbody.data("metrics", keys);
      }
      else {
        $tbody.find("td.metric-value").text("");
        $tbody.find("td.metric-type").text("");
        for(var kidx in keys) {
          kidx = parseInt(kidx);
          var key = keys[kidx];
          var $tr = $tbody.find("tr:nth-child("+(kidx+1)+")");
          if($tr.length == 1) {
            $tr.find("td.metric-type").html($nice_type(x.metrics.current[key]._type));
            $tr.find("td.metric-value").text(x.metrics.current[key]._value);
          }
        }
      }
      var $a, $g;
      if(!x.status || !x.status.hasOwnProperty("available"))
        $a = $label("unknown", "default");
      else if(x.status.available) $a = $label("available", "success");
      else $a = $label("unavailable", "danger");
      $c.find(".check-detail-available").html($a);
      if(!x.status || !x.status.hasOwnProperty("good"))
        $g = $label("unknown", "default");
      else if(x.status.good) $g = $label("good", "success");
      else $g = $label("bad", "danger");
      $c.find(".check-detail-state").html($g);
    });
  }
}
function start_check_details(x) {
  var check = x.data;
  var f = check_refresh(check);
  f();
  check.intervalId = setInterval(f, 1000);
}

function refresh_checks() {
  var st = jQuery.ajax("/checks/show.json");
  st.done(function( checks ) {
    // First unmark all our rows
    var $tbody = $("tbody#check-table");
    $tbody.find("> tr").removeClass("current-check");
    for ( var id in checks ) {
      var check = checks[id];
      $tr = $tbody.find("tr#check-" + id);
      if($tr.length == 0) {
        $tr = $("<tr/>").attr("id", "check-" + id);
        $tr.addClass("accordion-toggle");
        $tr.append($("<td class=\"check-state\"/>").html($flags(1, check.flags)));
        $tr.append($("<td class=\"check-uuid\"/>").text(id));
        $tr.append($("<td class=\"check-name\"/>").text(check.name));
        $tr.append($("<td class=\"check-module\"/>").text(check.module));
        $tr.append($("<td class=\"check-target\"/>").text(check.target));
        $tr.append($("<td class=\"check-target-ip\"/>").text(check.target_ip));
        $tr.append($("<td class=\"check-frequency\"/>").html(nice_time(check.period/1000.0)+"/"+nice_time(check.timeout/1000.0)));
        $tr.append($("<td class=\"check-flags\"/>").html($flags(2, check.flags)));
        if(check_search !== "") {
          var text = $tr.find("> td").text();
          if(text.indexOf(check_search) >= 0) $tr.show();
          else $tr.hide();
        }
        $tbody.append($tr);
        var $details = $("<tr/>");
        $details.attr("id", "check-details-" + id);
        $details.addClass("check-details");
        $details.addClass("accordian-body");
        $details.addClass("collapse");
        var $td = $('<td colspan="8"></td>');
        var $t = $("div#checktemplate").clone();
        $t.attr("id", null);
        $td.append($t);
        $details.append($td);
        $tbody.append($details);
        checks[id].id = id;
        $details.on('shown.bs.collapse', checks[id], start_check_details);
      }
      else {
        var old = $tr.data("check");
        if(old.flags != check.flags) {
          $tr.find("td.check-state").html($flags(1, check.flags));
          $tr.find("td.check-flags").html($flags(2, check.flags));
        }
        if(old.name != check.name)
          $tr.find("td.check-name").text(check.name);
        if(old.module != check.module)
          $tr.find("td.check-module").text(check.module);
        if(old.target != check.target)
          $tr.find("td.check-target").text(check.target);
        if(old.target_ip != check.target_ip)
          $tr.find("td.check-target-ip").text(check.target_ip);
        if(old.period != check.period || old.timeout != check.timeout)
          $tr.find("td.check-target-ip").html(nice_time(check.period/1000.0)+"/"+nice_time(check.timeout/1000.0));
      }
      $tr.addClass("current-check");
      $tr.find("+ tr").addClass("current-check");
      $tr.data("check", checks[id]);
    }
    $tbody.find("> tr:not(.current-check)").remove();
  });
}
$("#checksview tbody").on('click', function(x) {
  var id = x.target.parentNode.id;
  if(!id) return;
  var uuid = /^check-(.+)/.exec(id);
  if(!uuid) return;
  var tohide = $("tr.check-details.in:not(.collapse)")
  if(tohide.length) {
    tohide.collapse('hide');
    if(tohide.attr("id") == "check-details-"+uuid[1]) return;
    tohide.one('hidden.bs.collapse', function() {
      $("tr#" + id + " + tr").collapse('toggle');
    });
  }
  else {
    $("tr#" + id + " + tr").collapse('toggle');
  }
})
$("#check-refresh").on('click', refresh_checks);
refresh_checks();
setInterval(refresh_checks, 10000);
setInterval(function(){
  $(".auto-age").each(function(idx) {
    var date = $(this).data("date");
    var odate = $(this).data("date-rendered");
    $(this).data("date-rendered", date);
    if(typeof(date) === 'undefined') {
      $(this).text("");
      return;
    }
    var age = ((+new Date()) - local_skew) - date;
    if(date == 0) $(this).text("never");
    else $(this).html(nice_time(Math.floor(0 - age/1000)));
  });
  $(".auto-date").each(function(idx) {
    var date = $(this).data("date");
    var odate = $(this).data("date-rendered");
    if(odate == date) return;
    $(this).data("date-rendered", date);
    if(typeof(date) === 'undefined') $this.text("");
    else if(date == 0) $(this).html("never");
    else $(this).html(nice_date(date));
  });
}, 1000);

$("input#check-search").on('input', function(x) {
  check_search = x.target.value;
  $("tbody#check-table > tr:not(.check-details)").each(function() {
    if(check_search === "") $(this).show();
    else {
      var text = $(this).find("> td").text();
      if( $(this).find("+ tr").hasClass("in") ) $(this).show();
      else if(text.indexOf(check_search) >= 0) $(this).show();
      else $(this).hide();
    }
  });
});

var last_log_idx;
function refresh_logs(force) {
  var qs = "";
  var $c = $("#main-log-window");
  if(typeof(last_log_idx) !== 'undefined')
    qs = "?since=" + last_log_idx;
  else
    qs = "?last=100";
  $.ajax("/eventer/logs/internal.json" + qs).done(function (logs) {
    var atend = force || Math.abs(($c[0].scrollTop + $c[0].clientHeight - $c[0].scrollHeight));
    for(var i in logs) {
      var log = logs[i];
      $row = $("<div class=\"row\"/>");
      $row.append($("<div class=\"col-md-2 text-muted\"/>").text(nice_date(log.whence)));
      $row.append($("<div class=\"col-md-10\"/>").text(log.line));
      $c.append($row);
      if(log.idx < last_log_idx) refresh_capa();
      last_log_idx = log.idx;
      if(atend < 20) {
        $c[0].scrollTop = $c[0].scrollHeight;
        $c[0].scrollLeft = 0;
      }
    }
    var rows = $c.find("> div");
    var cnt = 0;
    for(var i = rows.length ; i > 1000; i--) 
      rows[cnt++].remove();
  });
}
refresh_logs(1);
setInterval(refresh_logs, 1000);
