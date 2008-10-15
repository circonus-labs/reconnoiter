<script type="text/javascript" src="js/eye/datepicker.js"></script>
<script type="text/javascript" src="js/eye/colorpicker.js"></script>
<script type="text/javascript" src="js/eye/eye.js"></script>
<script type="text/javascript" src="js/eye/utils.js"></script>
<script type="text/javascript">
<!--
var maingraph;
var current_graph_id = '';
var debug_graph_edit = false;
var graphinfo= {};
var displayinfo = { start : 14*86400, cnt: '', end: '' };
var color_pool = Array('#33aa33','#4a00dc','#caac00','#3377cc','#00acfa','#cc3333')
var color_sel = 0;

function set_current_graph_id(id) {
  current_graph_id = id;
  fetch_graph_info(current_graph_id);
}
var recurse = 0;
function update_current_graph(redraw, f) {
  if(recurse > 0) return;
  var str = JSON.stringify(graphinfo);
  if(debug_graph_edit) $("#payload").html(str);
  $.post("json/graph/store",
         {'json':str},
         function(d) {
           recurse++;
           graphinfo.id = d.id;
           if(d.error) $("#gtool-error").html(d.error).fadeIn('fast');
           else $("#gtool-error").fadeOut('fast');
           if(graphinfo.id && graphinfo.saved != true) {
             $(".rememberGraph").html('"Remember" this graph.').fadeIn('slow');
             $(".rememberGraph").click(function() {
               graphinfo.saved = true;
               update_current_graph(false, function(r) {
                 if(r.error) graphinfo.saved = false;
                 else $(".rememberGraph").html('Remebered').fadeOut('slow');
               });
             });
           }
           if(redraw && maingraph) maingraph.ReconGraphRefresh({graphid: graphinfo.id});
           if(f) f(d);
           recurse--;
         }, 'json');
}
function graph_add_datapoint(d) {
  if(d.axis == undefined) { d.axis = 'l'; }
  if(d.name == undefined) { d.name = d.target + '`' + d.metric_name; }
  graphinfo.datapoints.push(d);
  gtool_add_datapoint(d);
  update_current_graph(true);
}
function gtool_add_guide() {
  var d = {};
  d.metric_type = 'guide';
  graphinfo.datapoints.push(d);
  gtool_add_datapoint(d);
  update_current_graph(true);
}

function gtool_add_datapoint(d) {
  var o;
  if(d.metric_type == 'guide') {
    o = $("#guideeditor").clone();
  }
  else {
    o = $("#datapointeditor").clone();
  }
  recurse++;
 
  if(d.color == null) {
    d.color = color_pool[color_sel];
    color_sel = (color_sel + 1) % color_pool.length;
  }
  o.find('.colorPickerHolder').ColorPicker({
    flat: true,
    color: '#ffffff',
    onSubmit: (function(dpo,dp) {
      var cs = dpo.find('.colorSelector');
      return function(hsb, hex, rgb) {
        dp.color = '#' + hex;
        cs.css('border', '1px solid ' + dp.color);
        cs.find('div').css('backgroundColor', dp.color)
                      .css('opacity', 0.2);
        cs.parent().find('.colorPickerHolder').stop().animate({height:0},500);
        update_current_graph(true);
      }
    })(o,d)
  });
  o.find('.colorPickerHolder > div').css('position', 'absolute');
  o.find('.colorSelector').css('border', '1px solid ' + d.color);
  o.find('.colorSelector div').css('backgroundColor', d.color)
                              .css('opacity',
                                   (d.opacity == null) ? 0.2 : d.opacity);
  o.find('.colorSelector').bind('click', function() {
    var activecp = $(this).parent().find('.colorPickerHolder');
    var h = activecp.height();
    $("#gtool #dataPoints .colorPickerHolder").filter(function(index) {
      return ($(this) != activecp);
    }).stop().animate({height:0}, 0);;
    $(this).parent().find('.colorPickerHolder').stop().animate({height: h != 0 ? 0 : 173}, 500);
  });

  o.find(d.axis == "l" ? ".axisl" : ".axisr").addClass("axison");
  o.find("span.axis").click(function(){
    $(this).siblings().removeClass("axison");
    $(this).addClass("axison");
    d.axis = $(this).hasClass("axisl") ? "l" : "r";
    update_current_graph(true);
  });
  if(!d.hidden){o.find('input[@name="view"]').attr("checked","checked");}
  o.find('input[@name="view"]').change(function(){
    d.hidden = !($(this).attr("checked"));
    update_current_graph(true);
  }).change();

  o.find('.deletedatapoint').click(function(){
    for(var i=0; i<graphinfo.datapoints.length; i++) {
      if(graphinfo.datapoints[i] == d) {
        graphinfo.datapoints.splice(i,1);
        break;
      }
    }
    update_current_graph(true);
    refresh_graph_from_json(graphinfo);
    return false;
  });

  if(d.metric_type == 'text') {
    o.find('tr.mathbox').remove();
    o.find('select[@name="derive"]').val('false');
  }
  else {
    if(d.derive){
      o.find('select[@name="derive"]').val(d.derive);
    }
    o.find('select[@name="derive"]').change(function(){
      d.derive = $(this).find(":selected").val();
      update_current_graph(true);
    }).change();

    o.find('input[@name="math1"]').val(d.math1);
    o.find('input[@name="math1"]').change(function(){
      d.math1 = $(this).val();
      update_current_graph(true);
    });
    o.find('input[@name="math2"]').val(d.math2);
    o.find('input[@name="math2"]').change(function(){
      d.math2 = $(this).val();
      update_current_graph(true);
    });
  }

  o.find(".datatitle").html(d.name);
  o.find(".datatitle").editable(function(value, settings) {
      d.name = value;
      update_current_graph(true);
      return(value);
    }, { });

  recurse--;
  $("#gtool #dataPoints").append(o.children());
}
function refresh_graph_from_json(j) {
  graphinfo = j;
  if(graphinfo.datapoints == undefined) graphinfo.datapoints = Array();
  $("h2#graphTitle").html(graphinfo.title ? graphinfo.title : 'Graph Title (click to edit)');
  $("h2#graphTitle").editable(function(value, settings) { 
      graphinfo.title = value;
      update_current_graph(false);
      return(value);
    }, { });
  $("input[@name='graphtype']:checked").removeAttr("checked");
  $("input[@name='graphtype']")
    .filter(function(i){return $(this).val() == graphinfo.type;})
    .attr("checked","checked");
  $("#dataPoints").empty();
  for(var i=0; i<graphinfo.datapoints.length; i++) {
    gtool_add_datapoint(graphinfo.datapoints[i]);
  }
  if(maingraph) {
    if(graphinfo.id) 
      maingraph.ReconGraphRefresh({graphid: graphinfo.id});
    else
      maingraph.ReconGraphReset();
  }
}
function fetch_graph_info(id) {
  graphinfo.id = id;
  if(id) $.getJSON("json/graph/info/" + id, refresh_graph_from_json);
  else refresh_graph_from_json({});
  $("span.blankGraph").click(function() {
    if(graphinfo.saved || confirm("I will forget the current graph.  Are you sure?"))
      set_current_graph_id('');
    return false;
  });
}
-->
</script>
<!--<p><a href="">username</a> / <a href="#">worksheet</a></p>-->
<p/>
<span class="rememberGraph"></span>
<span class="blankGraph">New Blank</span>
<h2 id="graphTitle"></h2>
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
        $("#graph_datetool .btn-slide").click(function(){
                $("#graph_widgetCalendar").stop().animate({
                     height: state ? 0 :
                       $('#graph_widgetCalendar div.datepicker').get(0).offsetHeight
                  }, 500);
                state = !state;
                $(this).toggleClass("active");
                return false;
        });
        $("#graph_datetool .datechoice").click(function(){
                $("#graph_datetool .range a.btn-slide").html("YYYY/MM/DD - YYYY/MM/DD");
                $("#graph_widgetCalendar").slideUp("slow");
                $("#graph_datetool .datechoice").removeClass("selected");
                displayinfo.start = time_windows[$(this).html()];
                displayinfo.end = '';
                $(this).addClass("selected");
                maingraph.ReconGraphRefresh({graphid: graphinfo.id, start: time_windows[$(this).html()], end: ''});
                return false;
        });
        $('#graph_widgetCalendar').DatePicker({
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
                        displayinfo.start = start.toUTCString();
                        displayinfo.end = end.toUTCString();
                        maingraph.ReconGraphRefresh({graphid: graphinfo.id});
                        $('#graph_datetool .range a.btn-slide').get(0).innerHTML = formated.join(' - ');
                }
        });
        $(".graphType").change(function(){
                graphinfo.type = $(this).val();
		update_current_graph(true);
        });
        $("#gtool-error").click(function(){
          $("#gtool-error").fadeOut("slow");
        });
        $("span.addGuide").click(function() {
          gtool_add_guide();
        });
        set_current_graph_id('');
        maingraph = $('<div></div>').attr("id", "maingraph").ReconGraph({graphid: graphinfo.id, width: 780, height: 400});
        $("#maingraphHolder").append(maingraph);
});
</script>

<div id="graph_datetool">
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
<br/>
	<div id="graph_widgetCalendar" class="widgetCalendar"></div>
</div>
			
<!-- confirm box -->
<div id="confirm" style="display:none">
	<a href="#" title="Close" class="modalCloseX modalClose">x</a>
	<div class="header"><span>Confirm</span></div>
	<p class="message"></p>
	<div class="buttons">
		<div class="no modalClose">No</div><div class="yes">Yes</div>
	</div>
</div>

<div>
	<div id="maingraphHolder">
        </div>
	<form action="#" name="form4" id="form4" style="margin:1em 0;text-align:center;">
	<fieldset>
	<legend style="display:none;">View</legend>
	<label for="std_view"><input class="graphType" type="radio" name="graphtype" id="std_view" value="standard"/> Standard View</label> &nbsp;&nbsp;&nbsp;
	<label for="stacked_view"><input class="graphType" type="radio" name="graphtype" id="stacked_view" value="stacked" /> Stacked View</label>
	</fieldset>
	</form>
	
</div>
<div style="float:right"><span class="addGuide">Add Guide</span></div>
<br style="clear:right" />
<div class="error"><p class="error" id="gtool-error"></p></div>
<table id="gtool">
	<tr>
		<th></th>
		
		<th class="data">Data Points</th>
		<th>Color</th>
		<th>Derivative</th>
		<th>Axis</th>
		<th></th>
		<th></th>
	</tr>
	<tbody id="dataPoints">
	</tbody>
</table>

<div style="display:none">
<form id="hiddeneditor">
	<table>
	<tbody id="guideeditor">
	<tr>
		<td><input name="view" type="checkbox" value="1"/></td>
		<td class="data datatitle"></td>
		<td><div class="colorPicker"><div class="colorSelector"><div style="background-color: #fff"></div></div><div class="colorPickerHolder"></div></div></td>
		<td></td>
		<td></td>
		<td><a href="#" class="deletedatapoint"><span>delete</span></a></td>
		<td class="math">math</td>
	</tr>
	<tr class="mathbox">
		<td colspan="7">
		<div>
			<label for="math">Display Math</label> <input name="math1" type="text" value="" style="width:380px;" />
		</div>
		<div>
			<label for="math">Percentile</label> <input name="math2" type="text" value="" style="width:380px;" />
		</div>
		</td>
	</tr>
	</tbody>
	<tbody id="datapointeditor">
	<tr>
		<td><input name="view" type="checkbox" value="1"/></td>
		<td class="data datatitle"></td>
		<td><div class="colorPicker"><div class="colorSelector"><div style="background-color: #fff"></div></div><div class="colorPickerHolder"></div></div></td>
		<td><select name="derive"><option value="false">no</option><option value="derive">derive</option><option value="counter">counter</option></select></td>
		<td><span class="axis axisl"> L</span> <span class="axis axisr"> R</span></td>
		<td><a href="#" class="deletedatapoint"><span>delete</span></a></td>
		<td class="math">math</td>
	</tr>
	<tr class="mathbox">
		<td colspan="7">
		<div>
			<label for="math">Display Math</label> <input name="math1" type="text" value="" style="width:380px;" />
		</div>
		<div>	
			<label for="math">Source Math</label> <input name="math2" type="text" value="" style="width:380px;" />
		</div>
		</td>
	</tr>
	</tbody>
        </table>
</form>
</div>

<div id="payload">
</div>
