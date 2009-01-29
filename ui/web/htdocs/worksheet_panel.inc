<script type="text/javascript" src="js/eye/datepicker.js"></script>
<script type="text/javascript" src="js/eye/eye.js"></script>
<script type="text/javascript" src="js/eye/utils.js"></script>
<script type="text/javascript" src="js/jquery.timers.js"></script>
<script type="text/javascript">
<!--

var ws_displayinfo = { start : 14*86400, cnt: '100', end: '' };
var wsinfo = {};
var locked = true;
var streaming = false;
var stream_graph;

function stream_data() {

  polltime = 5000;
  timewindow = 6000000;
  stream_object = stream_graph;
  stream_dirty = false;
  stream_graph.ReconGraphPrepareStream(timewindow, polltime);

//setup functionality so that every second check if we are streaming and dirty, plot if true
  stream_graph.everyTime(1000, function() {
    if(!streaming) {   stream_graph.stopTime();}
    else {
      if(stream_dirty){
        stream_graph.ReconGraphPlotPoints();     
        stream_dirty=false;
      }
    }
  });
  $('#streambox').html('<iframe src="http://bob.office.omniti.com/data"></iframe>');
}

function lock_wforms() {
  $("h2#worksheetTitle").unbind();
  $("ul#worksheet-graphs").unbind();
}
	 
function unlock_wforms() {
  $("h2#worksheetTitle").editable(function(value, settings) {
         wsinfo.title = value;
         update_current_worksheet();
         return(value);
       }, { });

  var ul = $("ul#worksheet-graphs");
  ul.sortable({ handle: '.graphTitle',
                scroll: true,
                stop:
                  function (e,ui) {
                    wsinfo.reorder = true;
                    wsinfo.graphs = new Array();
                    ui.item.parent().find("> li > div").each(
                      function(i) { wsinfo.graphs.push($(this).attr("id")); }
                    );
                    update_current_worksheet();
                    wsinfo.reorder = false;
                  }
              });

  $("ul#worksheet-graphs > li >div").each(
  function(i) { 
                    var g = { start: ws_displayinfo.start,
                      end: ws_displayinfo.end,
                      cnt: ws_displayinfo.cnt,
                      graphid:  $(this).attr("id") 
                    };
		    
                    $(this).prepend("<span style='display:none'; class='zoomGraph' id='Zoom-"+$(this).attr('id')+"'>Zoom</span>");
                    $(this).prepend("<span style='display:none'; class='deleteWorksheetGraph' id='Remove-"+$(this).attr('id')+"'>Remove</span>");

                    $("#Remove-"+$(this).attr('id')).click( function() {
                      $.getJSON('json/worksheet/deletegraph/' + wsinfo.id + '/' + g.graphid,
                        function(r) { if(r.error) { alert(r.error); } });
                        load_worksheet(wsinfo.id);
                    });

		    $("#Zoom-"+$(this).attr('id')).click( function() {

                      stream_graph = $('<div></div>').ReconGraph({graphid: g.graphid});

    		      var smod = stream_graph.modal({
		        containerId: 'StreamingModalContainer',
                        close: 'true',
  			overlayCss: {
    			backgroundColor: '#000000',
    			cursor: 'wait'
  			},
  			containerCss: {
   			backgroundColor: '#FFFFFF',
   			left: '30%',
   			top: '10%',
   			border: '2px solid #000000',
   			padding: '5px'
  			},
		      });

		      stream_graph.ReconGraphRefresh({graphid: g.graphid});
		      stream_graph.prepend("<span class='zoomStream'>Stream Data</span>");
                      stream_graph.prepend("<span class='zoomClose'>x</span>");

                      $(".zoomClose").click(function() {
                        streaming = false;                        
                        $('#streambox').html('');
                        smod.close();
                      });
/*
		      $(".zoomStream").click(function() {
                        if(!streaming) {
                           streaming = true;
                           $(".zoomStream").html('Streaming!').fadeIn('slow');
                           stream_data();
			}
                        else if(streaming) {
                           streaming = false;
                           $('#streambox').html('');
                           $(".zoomStream").html('Stream Data').fadeIn('slow');
                           stream_graph.ReconGraphRefresh({graphid: g.graphid});
                        }

                      }); //end stream click function                      
*/
                    }); //end zoom click function

             	    $(this).mouseover(                  
                       function() { 
                         $(this).attr("style", "outline: 1px solid #DDDDDD;"); 
                         $("#Zoom-"+$(this).attr('id')).removeAttr("style");
                         $("#Remove-"+$(this).attr('id')).removeAttr("style");
                    }
                    ); 
  }); //end for each graph in the list

  $("ul#worksheet-graphs > li >div").each(
  function(i) { $(this).mouseout( 
                 function() { 
                    $(this).removeAttr("style"); 
                    $("#Zoom-"+$(this).attr('id')).attr("style", "display:none;");
                    $("#Remove-"+$(this).attr('id')).attr("style", "display:none;");
                 }
); });

}	    

function update_current_worksheet(f) {
  var str = JSON.stringify(wsinfo);
  $.post("json/worksheet/store",
         {'json':str},
         function(d) {
           wsinfo.id = d.id;
           if(d.error) $("#ws-tool-error").html(d.error).fadeIn('fast');
           else $("#ws-tool-error").fadeOut('fast');
           if(wsinfo.id && wsinfo.title && wsinfo.saved != true &&
              $(".rememberWorksheet:visible").length == 0) {
             wsinfo.saved = false;
             $(".rememberWorksheet").html('"Remember" this worksheet.').fadeIn('slow');
             $(".rememberWorksheet").click(function() {
               wsinfo.saved = true;
               update_current_worksheet(function(r) {
                 if(r.error) wsinfo.saved = false;
                 else $(".rememberWorksheet").html('Remebered').fadeOut('slow');
               });
             });
           }
           if(f) f(d);
         }, 'json');
}

function process_worksheet_json(r) {
  wsinfo.id = r.sheetid;
  wsinfo.title = r.title;
  wsinfo.graphs = new Array();

  var ul = $("ul#worksheet-graphs");
  $("h2#worksheetTitle").html(r.title);
  ul.empty();
  for(var i = 0; i < r.graphs.length; i++) {
    var g = {};
    g.graphid = r.graphs[i];
    g.start = ws_displayinfo.start;
    g.end = ws_displayinfo.end;
    g.cnt = ws_displayinfo.cnt;
    var o = $('<div></div>').ReconGraph(g);
    ul.append($('<li/>').append(o));
    o.ReconGraphRefresh();
    wsinfo.graphs.push(o.attr("id"));
  }
  ul.sortable("refresh");
}
function add_graph_to_worksheet(graphid) {
  if(!locked){
    for(var i = 0; i < wsinfo.graphs.length; i++) {
      if(wsinfo.graphs[i]==graphid) {
         alert("Worksheets cannot have dusplicate graphs!");
         return;
      }
    }  
    var g = { start: ws_displayinfo.start,
              end: ws_displayinfo.end,
              cnt: ws_displayinfo.cnt,
              graphid: graphid };
    var o = $('<div></div>').ReconGraph( g );
    var ul = $("ul#worksheet-graphs");
    ul.append($('<li/>').append(o));
    o.ReconGraphRefresh();
    ul.sortable("refresh");
    wsinfo.graphs = new Array();
    alert('pushing ' + graphid);
    wsinfo.graphs.push(graphid);
    update_current_worksheet();
  }

}
function refresh_worksheet() {
  var g = { start: ws_displayinfo.start,
            end: ws_displayinfo.end,
            cnt: ws_displayinfo.cnt };
  $("ul#worksheet-graphs > li > div").ReconGraphRefresh(g);
}
function load_worksheet(id) {
    wsinfo.graphs = new Array();
  if(id==null) {
    wsinfo.saved = false;
    locked = false;
    unlock_wforms();
    $(".editWorksheet").html('Editing!').fadeIn('slow');
    process_worksheet_json({graphs: [], title:'Worksheet Title (click to edit)', sheetid: ''});
  }
  else {
    wsinfo.saved = true;
    locked = true;
    lock_wforms();
    $(".editWorksheet").html('Edit Worksheet').fadeIn('slow');
    $.getJSON("json/worksheet/info/" + id, process_worksheet_json);
  }
}
-->
</script>
<span class="rememberWorksheet"></span>
<span class="blankWorksheet">New Blank</span>
<span class="editWorksheet">Edit Worksheet</span>
<h2 id="worksheetTitle">Worksheet Title</h2>
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

	$("h2#worksheetTitle").editable(function(value, settings) {
         wsinfo.title = value;
         update_current_worksheet();
         return(value);
       }, { });

	$(".editWorksheet").click(function() {		
	        if(locked){
		    locked = false;		    
		    unlock_wforms();
		    $(".editWorksheet").html('Editing!').fadeIn('slow');
		}
		else if(!locked){
		    locked = true;	
		    lock_wforms();
                    $(".editWorksheet").html('Edit Worksheet').fadeIn('slow');
		}
	});


	$(".blankWorksheet").click(function() {
		load_worksheet();		
		alert("id = "+wsinfo.id);
	});

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
                refresh_worksheet();
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
                        refresh_worksheet();
                        $(".datechoice").removeClass("selected");
                        $('#ws_datetool .range a.btn-slide').get(0).innerHTML = formated.join(' - ');
                }
        });
        $("#ws-tool-error").click(function(){
          $("#ws-tool-error").fadeOut("slow");
        });

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
<div id="streambox" style="display:none"></div>	
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
