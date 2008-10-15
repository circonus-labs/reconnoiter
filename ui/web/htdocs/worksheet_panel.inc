<script type="text/javascript" src="js/eye/datepicker.js"></script>
<script type="text/javascript" src="js/eye/eye.js"></script>
<script type="text/javascript" src="js/eye/utils.js"></script>
<script type="text/javascript">
<!--
var ws_displayinfo = { start : 14*86400, cnt: '100', end: '' };
function process_worksheet_json(r) {
  var ul = $("ul#worksheet-graphs");
  ul.empty();
  for(var i = 0; i < r.graphs.length; i++) {
    r.graphs[i].start = ws_displayinfo.start;
    r.graphs[i].end = ws_displayinfo.end;
    r.graphs[i].cnt = ws_displayinfo.cnt;
    var o = $('<div></div>').ReconGraph( r.graphs[i] );
    ul.append($('<li/>').append(o));
    o.ReconGraphRefresh();
  }
  ul.sortable({ handle: '.graphTitle',
                scroll: true,
                stop:
                  function (e,ui) {
                    var ol = new Array();
                    ui.item.parent().find("> li > div").each(
                      function(i) { ol.push($(this).attr("id")); }
                    );
                    // Store new worksheet order here
                    console.log(ol);
                  }
              });
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
        $("#ws_datetool .btn-slide").click(function(){
                $("#ws_widgetCalendar").stop().animate({
                     height: state ? 0 :
                       $('#ws_widgetCalendar div.datepicker').get(0).offsetHeight
                  }, 500);
                state = !state;
                $(this).toggleClass("active");
                return false;
        });
        $("#ws_datetool .datechoice").click(function(){
                $("#ws_datetool .range a.btn-slide").html("YYYY/MM/DD - YYYY/MM/DD");
                $("#ws_widgetCalendar").slideUp("slow");
                $(".datechoice").removeClass("selected");
                ws_displayinfo.start = time_windows[$(this).html()];
                ws_displayinfo.end = '';
                $(this).addClass("selected");
                load_worksheet();
                return false;
        });
        $('#ws_widgetCalendar').DatePicker({
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
                        $('#ws_datetool .range a.btn-slide').get(0).innerHTML = formated.join(' - ');
                }
        });
        $("#ws-tool-error").click(function(){
          $("#ws-tool-error").fadeOut("slow");
        });
	load_worksheet('<?php print $_GET['worksheetid'] ?>');
});
</script>

<div id="ws_datetool">
	<div class="zoom">
		<dl>
			<dt>Zoom:</dt>
			<dd><a href="#" class="first datechoice">2d</a></dd>
			<dd><a href="#" class="datechoice">1w</a></dd>
			<dd><a href="#" class="selected datechoice">2w</a></dd>
			<dd><a href="#" class="datechoice">4w</a></dd>
			<dd><a href="#" class="datechoice">1y</a></dd>
		</dl>
	</div>
	<div class="range">
		<dl>
			<dt>Date Range:</dt>
			<dd><a href="" class="btn-slide">YYYY/MM/DD - YYYY/MM/DD</a></dd>
		</dl>
	</div>
<br style="clear:both; margin-bottom:0.5em;"/>
	<div id="ws_widgetCalendar" class="widgetCalendar"></div>
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

<div class="error"><p class="error" id="ws-tool-error"></p></div>

<div id="ws_payload">
</div>
