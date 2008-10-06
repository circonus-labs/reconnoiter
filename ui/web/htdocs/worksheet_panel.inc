<script type="text/javascript" src="js/eye/datepicker.js"></script>
<script type="text/javascript" src="js/eye/eye.js"></script>
<script type="text/javascript" src="js/eye/utils.js"></script>
<script type="text/javascript">
<!--
var ws_displayinfo = { start : 14*86400, cnt: '100', end: '' };
function plot_id(r, domid) {
      var placeholder = $(domid + " > div.plot-area");
      placeholder.bind("plotselected", function (event, ranges) {
        alert(ranges.xaxis.from + " -> " + ranges.xaxis.to);
        plot = $.plot(placeholder, data,
                    $.extend(true, {}, options, {
                               xaxis: { min: ranges.xaxis.from, max: ranges.xaxis.to }
                             }));
      });
      r.options.legend.container = $(domid + " div.plot-legend");
      if(!r.options.yaxis) r.options.yaxis = {};
      if(r.options.yaxis.suffix)
        r.options.yaxis.tickFormatter = function (val, axis) {
          return val.toFixed(axis.tickDecimals) + r.options.yaxis.suffix;
        };
      var plot = $.plot(placeholder, r.data, r.options);
}
function ws_redraw_graph(info, domid) {
  if(info.datapoints.length > 0) {
    var url = "flot/graph/settings/" + info.graphid;
    $(domid + " .plot-area").html('<div class="centered"><div class="loading">&nbsp;</div></div>');
    $.getJSON(url, {'cnt':ws_displayinfo.cnt,
                    'start':ws_displayinfo.start,
                    'end':ws_displayinfo.end},
              function (r) { plot_id(r,domid); });
  }
  else {
    $(domid + " .plot-area").html('<div class="centered">No data</div>');
  }
}
function ws_refresh_graph_from_json(info, domid) {
  if(info.datapoints == undefined) info.datapoints = Array();
  $(domid + " h3.graphTitle").html(info.title);
  ws_redraw_graph(info, domid);
}
function ws_fetch_graph_info(id, domid) {
  $.getJSON("json/graph/info/" + id,
            function(r) { ws_refresh_graph_from_json(r, domid); });
}
function process_worksheet_json(r) {
  var ul = $("ul#worksheet-graphs");
  ul.empty();
  for(var i = 0; i < r.graphs.length; i++) {
    var o = $("#maingraph-template").clone();
    o.attr("id", r.graphs[i].graphid);
    ul.append($('<li/>').append(o));
    ws_refresh_graph_from_json(r.graphs[i], "#" + r.graphs[i].graphid);
  }
}
function load_worksheet(id) {
  $.getJSON("json/worksheet/info/" + id, process_worksheet_json);
}
-->
</script>
<h2>Worksheet Title</h2>
<p/>
<!-- date range box -->
<script type="text/javascript">
$(document).ready(function(){
        var time_windows = { '2d' : 86400*2,
                             '1w' : 86400*7,
                             '2w' : 86400*14,
                             '4w' : 86400*28,
                             '1y' : 86400*365,
                           };
        var state = false;
        $(".btn-slide").click(function(){
                $("#widgetCalendar").stop().animate({
                     height: state ? 0 :
                       $('#widgetCalendar div.datepicker').get(0).offsetHeight
                  }, 500);
                state = !state;
                $(this).toggleClass("active");
                return false;
        });
        $(".datechoice").click(function(){
                $("#range a.btn-slide").html("YYYY/MM/DD - YYYY/MM/DD");
                $("#widgetCalendar").slideUp("slow");
                $(".datechoice").removeClass("selected");
                ws_displayinfo.start = time_windows[$(this).html()];
                ws_displayinfo.end = '';
                $(this).addClass("selected");
                load_worksheet();
                return false;
        });
        $('#widgetCalendar').DatePicker({
                flat: true,
                format: 'Y/m/d',
                date: [new Date(), new Date()],
                calendars: 3,
                mode: 'range',
                starts: 1,
                onChange: function(formated) {
                        var dates;
                        dates = formated[0].split('/');
                        var start = new Date(dates[0], dates[1]-1, dates[2], 0, 0, 0);
                        dates = formated[1].split('/');
                        var end = new Date((new Date(dates[0], dates[1]-1, dates[2], 0, 0, 0)).getTime() + 86400000);
                        ws_displayinfo.start = start.toUTCString();
                        ws_displayinfo.end = end.toUTCString();
                        load_worksheet('');
                        $(".datechoice").removeClass("selected");
                        $('#range a.btn-slide').get(0).innerHTML = formated.join(' - ');
                }
        });
        $("#gtool-error").click(function(){
          $("#gtool-error").fadeOut("slow");
        });
	load_worksheet('<?php print $_GET['worksheetid'] ?>');
});
</script>

<div id="datetool">
	<div id="zoom">
		<dl>
			<dt>Zoom:</dt>
			<dd><a href="#" class="first datechoice">2d</a></dd>
			<dd><a href="#" class="datechoice">1w</a></dd>
			<dd><a href="#" class="selected datechoice">2w</a></dd>
			<dd><a href="#" class="datechoice">4w</a></dd>
			<dd><a href="#" class="datechoice">1y</a></dd>
		</dl>
	</div>
	<div id="range">
		<dl>
			<dt>Date Range:</dt>
			<dd><a href="" class="btn-slide">YYYY/MM/DD - YYYY/MM/DD</a></dd>
		</dl>
	</div>
<br style="clear:both; margin-bottom:0.5em;"/>
	<div id="widgetCalendar"></div>
</div>

<div>
  <ul id="worksheet-graphs" />
  <br style="clear:left" />
</div>			
<div style="display:none">
	<div id="maingraph-template">
        <h3 class="graphTitle">graph title</h3>
        <div class="plot-area" style="width:380px;height:180px"></div>
        <div class="plot-legend">legend</div>
        </div>
</div>

<div class="error"><p class="error" id="gtool-error"></p></div>

<div id="payload">
</div>
