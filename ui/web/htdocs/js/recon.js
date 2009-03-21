//global objects to use for calling plot_ifram_data from stream
var stream_object;
var stream_dirty;

//set the global streaming object to the local ReconGraph object to use,
// and init,update the global streaming boolean to then call this from a server
function plot_iframe_data(xdata, uuid, metric_name, ydata) {
    stream_object.ReconGraphAddPoint(xdata, uuid, metric_name, ydata);
    stream_dirty = true;
}

function log_iframe_message(message) {
    $(".stream-log").html(message).fadeIn('slow');
}

function dump(arr,level) {
	var dumped_text = "";
	if(!level) level = 0;
	
	//The padding given at the beginning of the line.
	var level_padding = "";
	for(var j=0;j<level+1;j++) level_padding += "    ";
	
	if(typeof(arr) == 'object') { //Array/Hashes/Objects 
		for(var item in arr) {
			var value = arr[item];
			
			if(typeof(value) == 'object') { //If it is an array,
				dumped_text += level_padding + "'" + item + "' ...\n";
				dumped_text += dump(value,level+1);
			} else {
				dumped_text += level_padding + "'" + item + "' => \"" + value + "\"\n";
			}
		}
	} else { //Stings/Chars/Numbers etc.
		dumped_text = "===>"+arr+"<===("+typeof(arr)+")";
	}
	return dumped_text;
}

function rpn_magic(expr) {
  return function(value, o) {
    return rpn_eval(value, expr, (o != null) ? o : {});
  };
}
function rpn_eval(value, expr, meta) {
  var s = [];
  var ops = expr.split(",");
  s.unshift(value)
  for (var i = 0; i < ops.length; i++) {
    var opname = ops[i];
    if(meta && meta[opname])
      for(var j = 0; j < meta[opname].length; j++)
        ops.splice(i, (j==0) ? 1 : 0, meta[opname][j]);
  }

  for (var i = 0; i < ops.length; i++) {
    op = ops[i];

    switch(op) {
      case 'ln':
        s.unshift(Math.log(s.shift())); break;
      case 'round':
        r = Math.pow(10,s.shift());
        l = s.shift();
        s.unshift(Math.round(r * l)/r);
        break;
      case 'floor':
        s.unshift(Math.floor(s.shift())); break;
      case 'ceil':
        s.unshift(Math.ceil(s.shift())); break;
      case 'log':
        r = s.shift();
        l = s.shift();
        s.unshift(Math.log(l, r));
        break;
      case 'e':
        s.unshift(Math.exp(1)); break;
      case 'pi':
        s.unshift(Math.pi()); break;
      case '^':
        r = s.shift();
        l = s.shift();
        s.unshift(Math.pow(l, r));
        break;
      case '-':
        r = s.shift();
        l = s.shift();
        s.unshift(l - r);
        break;
      case '/':
        r = s.shift();
        l = s.shift();
        s.unshift(l / r);
        break;
      case '~':
        s.shift(); break;
      case '.':
        s.unshift(s[s.shift()]); break;
      case '+':
        s.unshift(s.shift() + s.shift()); break;
      case '*':
        s.unshift(s.shift() * s.shift()); break;
      case 'auto':
        var units = 1;
        if(meta && meta._max) {
          units = Math.pow(1000,Math.floor(Math.log(meta._max)/Math.log(1000)))
          if(units == 0) units = 1;
        }
        switch(units) {
          case 0.000000001: meta.suffix = 'n'; break;
          case 0.000001: meta.suffix = 'u'; break;
          case 0.001: meta.suffix = 'm'; break;
          case 1000: meta.suffix = 'k'; break;
          case 1000000: meta.suffix = 'M'; break;
          case 1000000000: meta.suffix = 'G'; break;
          case 1000000000000: meta.suffix = 'T'; break;
          default: meta.suffix = null; break;
        }
        s.unshift( s.shift() / units );
        break;
      case 'min':
        s.unshift(min(s.shift(),s.shift())); break;
      case 'max':
        s.unshift(max(s.shift(),s.shift())); break;
      default:
        if(op.match(/^-?\d+$/)) {
          s.unshift(op);
        }
    }
  }
  var newvalue = s.shift();
  return newvalue;
}

(function ($) {
  var ReconGraph = function() {
    var displayinfo = { start : 14*86400, end: '', width: 380, height: 180 };
    var doptions, dplaceholder, ddata;

    function ytickformatter (ddata, axisidx) {
      return function(val,axis) {
        for(var i=0; i<ddata.length; i++) {
          if(ddata[i].yaxis == axisidx &&
             ddata[i].reconnoiter_display_expression) {
            var meta = { _max: Math.max(Math.abs(axis.datamax),
                                        Math.abs(axis.datamin)),
                         // for delta calc, we don't want to
                         // lose precision
                         floor: ['.'], ciel: ['.'], round: ['~','.']
                       },
                pval = rpn_eval(val, ddata[i].reconnoiter_display_expression, meta);
            if((val > 0 && pval < 0) ||
               (val < 0 && pval > 0)) {
              // Sign inversion means we're being clever and using
              // the negative axis as a positive one.
              pval = Math.abs(pval);
            }
            return pval.toFixed(axis.tickDecimals) +
                   ((meta.suffix != null) ? meta.suffix : '');
          }
        } 
        return val.toFixed(axis.tickDecimals);
      }
    }

    return {
      init:
        function(options) {
          this.graphinfo = $.extend({}, displayinfo, options||{});
          if(!this.graphinfo.cnt) this.graphinfo.cnt = this.graphinfo.width / 2;
          if(!this.attr("id")) this.attr("id", this.graphinfo.graphid);
          this.append($('<h3/>').addClass("graphTitle")
                               .html(this.graphinfo.title || ''))
              .append($('<div></div>').addClass("plot-area")
                                      .css('width', this.width + 'px')
                                      .css('height', this.height + 'px'))
              .append($('<div></div>').addClass("plot-legend"));
          this.data('__recon', this);
          return this;
        },
      reset:
        function() {
          if(this.length > 1) {
            this.each(function(i) { $(this).ReconGraphReset(); });
            return this;
          }
          this.graphinfo.graphid = '';
          if(this.flot_plot) {
            this.find("h3.graphTitle").html('');
            this.find("div.plot-legend").html('');
            this.flot_plot.setData({});
            this.flot_plot.setupGrid();
            this.flot_plot.draw();
          }
          this.data('__recon', this);
          return this;
        },
      PrepareStream:
	function (time_window, time_interval) {                      
	    if(this.flot_plot) {
                doptions.time_window = time_window;
                doptions.time_interval = time_interval;
                doptions.max_time = 0;
		for(var i=0; i<ddata.length;i++) {  
                    ddata[i].data = [];
		}		
		this.flot_plot.setData({});       
		this.flot_plot.setupGrid();
		this.flot_plot.draw();                         
	    }		
	    return this;
	},
     AddPoint:
        function (xdata, uuid, metric_name, ydata) {

	    //note that lastval[0] and the xdata need to be converted from seconds to milliseconds for flot

	    tdata = [xdata, ydata.toString()];

	    for(var i=0; i<ddata.length;i++) {
		if( (ddata[i].uuid ==  uuid) 
		    && (ddata[i].metric_name == metric_name)
		    && !ddata[i].hidden ) {

//		    console.log("got data from stream for ",uuid,"-",metric_name," data = ",tdata, "hidden = ", ddata[i].hidden);

		    if((xdata*1000)>doptions.max_time) { doptions.max_time = xdata*1000; }
		    if( !doptions.min_time || ((xdata*1000)<doptions.min_time)) { doptions.min_time = xdata*1000;}

                    if(ddata[i].metric_type == 'numeric') {

			if(ddata[i].lastval) {
			    slope = (tdata[1] - ddata[i].lastval[1]) / (tdata[0] - ddata[i].lastval[0]); 		     
			    if(ddata[i].derive_val == 'derive') {
				tdata[1] = slope;
			    }
			    else if(ddata[i].derive_val == 'counter') {
				if(slope>=0) tdata[1] = slope; 
				else tdata[1] = '';
			    }
			}//end if there was a last value available    
                        //if this is the first live datapoint, set slope and count to null
			else if( (ddata[i].derive_val == 'derive') || (ddata[i].derive_val == 'counter') )
			    {
				tdata[1]='';
			    }			    
			if(tdata[1]!=''){
			    if(ddata[i].reconnoiter_source_expression) {
				tdata[1] = rpn_eval(tdata[1], ddata[i].reconnoiter_source_expression, {});
			}
			} //end if ydata was a number
			
			tdata[0]*=1000; //convert from seconds to milliseconds for flot
			ddata[i].data.push(tdata);
			if(ddata[i].lastval) {
			    if ((tdata[0] - ddata[i].data[0][0]) > doptions.time_window) {
				ddata[i].data.shift();
			    }
			}
		    }//end if metric was numeric
		    
                    //if we have a text data type
                    else { 

                        tdata[0]*=1000; //convert from seconds to milliseconds for flot
			tdata.push(tdata[1]);
                        tdata[1] = "0"; 
			
                        //if we had a previous value stored, only push data to plot when the value changes
                        if(ddata[i].lastval) {				    
			    if( ddata[i].lastval[1] != tdata[2] ) {
				ddata[i].data.push(tdata);
				if ((tdata[0] - ddata[i].data[0][0]) > doptions.time_window) {
				    ddata[i].data.shift();
				}
			    }
                            else { //if there was no change in the value clear the metric so it doesnt display
				ddata[i].data = []; 
			    }
			}
                        //otherwise we are adding a text point for the first time
			else { 
			    ddata[i].data.push(tdata);
			}
			
		    }//end if text metric type

		    ddata[i].lastval = [xdata, ydata];
		    
		} //end if the uuid and metric_name match
	    } //end for each dataset	    
	    
	    return this;
	},
    PlotPoints:
        function () {

	    if( (doptions.max_time >= doptions.min_time + doptions.time_window)) {
		doptions.xaxis.min = doptions.max_time - doptions.time_window;
		doptions.xaxis.max = doptions.max_time;
	    }
	    else {
	     	doptions.xaxis.min = doptions.min_time;
		doptions.xaxis.max = doptions.min_time + doptions.time_window;
	    }

            this.flot_plot = $.plot(dplaceholder, ddata, doptions);
            return this;
	},
    refresh:
        function(options) {
          if(this.length > 1) {
            this.each(function(i) { $(this).ReconGraphRefresh(options); });
            return this;
          }
          var o = this.data('__recon');
          this.graphinfo = $.extend({}, o.graphinfo, options||{});
          var url = "flot/graph/settings/" + this.graphinfo.graphid;
          this.find(".plot-area")
              .html('<div class="centered"><div class="loading">&nbsp;</div></div>');
	  
          data = {'cnt':this.graphinfo.cnt,
                          'start':this.graphinfo.start,
                          'end':this.graphinfo.end,
	               'type':this.graphinfo.type};
	
	  $.ajaxq (this.graphinfo.graphid, { url: url,
	    data: data,
            success: (function(o) { return function (r) { r = eval('('+r+')'); o.ReconGraphPlot(r, function() { o.ReconGraphRefresh(); }) }}) (this)
		      });

          this.data('__recon', this);
          return this;
        },
      plot:
        function (r, redraw) {
          var title = this.ReconGraphMacro(r.title, r.data);
          this.find("h3.graphTitle").html(title);
          var placeholder = this.find("> div.plot-area");
          placeholder.bind("plotselected", (function (o) {
            return function (event, ranges) {
              var start = new Date(Math.floor(ranges.xaxis.from));
              var end = new Date(Math.floor(ranges.xaxis.to));
              o.graphinfo.start = start.toUTCString();
              o.graphinfo.end = end.toUTCString();
              if(redraw) redraw();
            };
          })(this));
          dplaceholder = placeholder;

          for(var i=0; i<r.data.length; i++) {
            if(r.data[i].reconnoiter_display_expression)
              r.data[i].dataManip = rpn_magic(r.data[i].reconnoiter_display_expression);
          }
          ddata = r.data;          

          if(!r.options.grid) r.options.grid = {};
          r.options.grid.hoverable = true;
          $("div.tooltip").remove();
          r.options.legend.container = this.find("div.plot-legend");
          r.options.grid.autoHighlight = false;
          r.options.grid.mouseActiveRadius = 4;
          r.options.grid.hoverXOnly = true;
          if(!r.options.points) r.options.points = {};
          r.options.points.radius = 2;
          if(!r.options.yaxis) r.options.yaxis = {};
          r.options.yaxis.tickFormatter = ytickformatter(ddata, 1);
          if(!r.options.y2axis) r.options.y2axis = {};
          r.options.y2axis.tickFormatter = ytickformatter(ddata, 2);
	  r.options.xaxis.localtime = true;
          doptions = r.options;

          var plot = this.flot_plot = $.plot(placeholder, r.data, r.options);
          var hoverings = [];
          placeholder.bind("plothover", function (event, pos, items) {
            var opacity = 0.7;
            for(var h=0; h<hoverings.length; h++)
              plot.unhighlight(hoverings[h].series, hoverings[h].datapoint);
            hoverings = [];
            if(items && items.length) {
              // Emulate opacity on white
              if(! $("div.tooltip")[0]) {
                $('<div class="tooltip"><div class="wrap"></div></div>').appendTo($('body'));
                $("div.tooltip .wrap").bind('mousemove',
                  function() { plot.getEventHolder().trigger('mousemove'); });
              }
              var tt = $("div.tooltip");
              tt.css( { width: 'auto',
                        position: 'absolute',
                        'z-index': 4000 });
              var ttw = $("div.tooltip .wrap");
              ttw.empty();
              ttw.append('<div class="point-down"></div>');

              items.sort(function(a,b) { return a.pageY - b.pageY; });
              var topitem = items[0];
              for(var i = 0; i < items.length; i++) {
                if(items[i].pageY < topitem.pageY) topitem = items[i];
                var rgb = (items[i].series.color.match(/\((.+)\)/))[1].split(',');
                rgb = rgb.map(function(a) { return Math.round(255-(255-a)*1); });
                var soft = 'rgba(' + rgb.join(',') + ',' + opacity + ')';
                var val = items[i].datapoint[1];
                if(items[i].series.dataManip) {
                  var meta = { _max: val };
                  val = items[i].series.dataManip(val, meta);
                  if(meta.suffix) val = val + meta.suffix;
                }

                // I want Y of YUV... for text color selection
                // if Y [0,255], if high (>255 * (1-opacity)) I want black, else white

                var Y = 0.299 * rgb[0] + 0.587 * rgb[1]  + 0.114 * rgb[2];
                var ttp = $('<div class="tip"><div/>')
                  .html((items[i].datapoint[2] ? items[i].datapoint[2] : val) + " (" + items[i].series.label + ")")
                  .css( { color: ( Y > ((1-opacity) * 255) ? 'black' : 'white' ), backgroundColor: soft });
                ttp.appendTo(ttw);
                hoverings.push(items[i]);
                plot.highlight(items[i].series, items[i].datapoint);
              }
              tt.css( { width: tt.width() + 10 } );
              tt.css( { overflow: 'hidden',
                        top: topitem.pageY - tt.height() - 25,
                        left: topitem.pageX - Math.floor(tt.width()/2) - 10,
                        display: 'block' } );
              return true;
            }
            $("div.tooltip").remove();
            return false;
          });
        },
      macro:
        function(str, data) {
          if(str == null) return str;
          var newstr = str.replace(
            /%\{[^\}]+\}/g,
            function(match) {
              var matches = match.match(/^%{([^:]+):?(.*)\}$/);
              for(var i=0; i<data.length; i++) {
                if(data[i].dataname == matches[1]) {
                  if(matches[2] == "" ||
                     matches[2] == "description" ||
                     matches[2] == "last_description")
                    return data[i].data[data[i].data.length-1][2];
                  else if(matches[2] == "value" ||
                          matches[2] == "last_value")
                    return data[i].data[data[i].data.length-1][1];
                  else if(matches[2] == "first_description")
                    return data[i].data[0][2];
                  else if(matches[2] == "first_value")
                    return data[i].data[0][1];
                  return "[unknown: "+matches[2]+"]";
                }
              }
              return "[unknown: "+matches[1]+"]";
            }
          );
          return newstr;
        }
    };
  }();
  $.fn.extend({ ReconGraph: ReconGraph.init,
                ReconGraphRefresh: ReconGraph.refresh,
                ReconGraphPlot: ReconGraph.plot,
                ReconGraphReset: ReconGraph.reset,
	      ReconGraphMacro: ReconGraph.macro,
              ReconGraphPrepareStream: ReconGraph.PrepareStream,
              ReconGraphAddPoint: ReconGraph.AddPoint,
              ReconGraphPlotPoints: ReconGraph.PlotPoints
              });
})(jQuery);

function perform_graph_search_add(params) {
  perform_generic_search('json/graph/search', params,
                         perform_graph_search_add,
                         graphs_for_worksheet, graph_search_summary);
}
function perform_graph_search_edit(params) {
  perform_generic_search('json/graph/search', params,
                         perform_graph_search_edit,
                         graphs_for_edit, graph_search_summary);
}
function perform_ws_search_edit(params) {
  perform_generic_search('json/worksheet/search', params,
                         perform_ws_search_edit,
                         ws_for_edit, ws_search_summary);
}
function perform_datapoint_search_add(params) {
  perform_generic_search('json/datapoint/search', params,
                         perform_datapoint_search_add,
                         datapoints_for_graph, datapoint_search_summary);
}
function ws_search_summary(r) {
  if(r.count > 0 && r.query == '') return '';
  return r.count + ' worksheet' + (r.count == 1 ? '' : 's' ) + ' found for \'' + htmlentities(r.query) + '\'';
}
function graph_search_summary(r) {
  if(r.count > 0 && r.query == '') return '';
  return r.count + ' graph' + (r.count == 1 ? '' : 's' ) + ' found for \'' + htmlentities(r.query) + '\'';
}
function datapoint_search_summary(r) {
  if(r.count > 0 && r.query == '') return '';
  return 'Found ' + r.count + ' data point' + (r.count == 1 ? '' : 's' ) + ' found for \'' + htmlentities(r.query) + '\'';
}
function perform_generic_search(url, params, search_func, create_item, summary_func) {
  $.getJSON(url,
            { 'q' : params.search, 'o' : params.offset, 'l' : params.limit },
            function(r) {
              var summary = summary_func(r);
              if(r.error) summary = 'Error: ' + htmlentities(r.error);
              $(params.domid + " > p.search-summary").html(summary).fadeIn('fast');
              var c = new Number(r.count);
              var l = new Number(r.limit);
              var o = new Number(r.offset);
              var page = $(params.domid + " > p.paginate");
              page.html('');
              if(c > l) {
                if(o > 0) {
                  var po = Math.max(o-l, 0);
                  $('<a/>').html( (po+1) + ' - ' + (po+l) )
                           .click(function() {
                             search_func({ 'domid': params.domid,
                                           'search': params.search,
                                           'offset': po,
                                           'limit': r.limit });
                             return false;
                           }).appendTo(page);
                }
                page.append($('<span/>').html((o+1) + '-' + (o+l)).addClass('searchselect'));
                if(o + l < c) {
                  var pop = o + l;
                  $('<a/>').html( (pop + 1) + '-' + (pop+l) )
                           .click(function() {
                             search_func({ 'domid': params.domid,
                                           'search': params.search,
                                           'offset': pop,
                                           'limit': r.limit });
                             return false;
                           }).appendTo(page);
                }
                page.slideDown('fast');
              }
              else page.slideUp('fast');
              $(params.domid + " > ul.searchresults > li").remove();
              for(var i=0; r.results && i<r.results.length; i++) {
                var g = r.results[i];
                var li = $('<li/>');
                create_item(li, g, { 'domid': params.domid,
                                     'search': params.search,
                                     'offset': o,
                                     'limit': l });
                $(params.domid + " > ul.searchresults").append(li);
              }
            });
}

function graphs_for_edit(li, g, params) {
  var edit = $('<a href="#"/>');
  edit.html('Edit').addClass('editgraph');
  edit.click(
    (function(graphid) {
        return function() {
          set_current_graph_id(graphid);
          return false;
        }
     })(g.graphid)
  );
  var del = $('<a href="#"/>');
  del.html('Forget').addClass('deletegraph');
  del.click(
    (function(graphid, li) {
        return function() {
	    confirm("I will forget the current graph.  Are you sure?", function(){
		    $.getJSON('json/graph/forget/' + graphid + '/0',
			      function (r) {
				  if(r.refed) {
				      var msg = "This graph is used in worksheet(s):<p>";
				      for(var wg in r.refed){ msg += "<br>"+r.refed[wg];}
				      msg+="<p> Forgetting it will remove it from these worksheets as well. <p>Are you sure you want to forget it?";
				      confirm(msg,
					      function() {
						  $.getJSON('json/graph/forget/' + graphid + '/1',
							    function(r) {
								if(r.error) { modal_warning("Database Error!", r.error); }
								else { perform_graph_search_edit(params);}
							    });
					      });
				  }
				  else {
				      if(r.error) { modal_warning("Database Error!", r.error); }
				      else { 
					  perform_graph_search_edit(params);
				      }
				  }
			      });
		    if(current_graph_id==graphid) {set_current_graph_id('');}
		});		
          return false;
        }
    })(g.graphid, li)
	    );
  var ul = $('<ul/>');
  ul.append($('<li/>').html(g.last_update));
  ul.append($('<li/>').append(edit));
  ul.append($('<li/>').append(del));
  li.append($('<div class="graphlist-title"/>').html(g.title)).append(ul);
}
function ws_for_edit(li, ws, params) {
  var add = $('<a href="#"/>');
  add.html('View').addClass('addtows');
  add.click(
    (function(sheetid) {
        return function() {
          worksheet.load(sheetid);
          return false;
        }
     })(ws.sheetid)
  );
  var ul = $('<ul/>');
  ul.append($('<li/>').html(ws.last_update));
  ul.append($('<li/>').append(add));
  li.append($('<div class="worksheetlist-title"/>').html(ws.title)).append(ul);
}
function graphs_for_worksheet(li, g, params) {
  var add = $('<a href="#"/>');
  var qview = $('<a href="#"/>');
  add.html('Add').addClass('addtows');
  add.click(
    (function(graphid) {
        return function() {
          worksheet.add_graph(graphid);
          return false;
        }
     })(g.graphid)
  );
  qview.html('Quick View').addClass('quickviewgraph');
  qview.click(
    (function(graphid, gtype) {
      return function() { worksheet.zoom(graphid, gtype); return false; }
    })(g.graphid, 'standard')
  );
  var ul = $('<ul/>');
  ul.append($('<li/>').html(g.last_update));
  ul.append($('<li/>').append(add));
  ul.append($('<li/>').append(qview));
  li.append($('<div class="ws-add-graph-title"/>').html(g.title)).append(ul);
}
function datapoints_for_graph(li, ds, params) {
  var a = $('<a href="#"/>');
  a.html(ds.target + '`' + ds.name + '`' + ds.metric_name);
  a.click(
    (function(ds_c) {
        return function() {
          graph_add_datapoint({'sid':ds_c.sid,
                               'name':ds_c.target + '`' + ds_c.metric_name,
                               'metric_name':ds_c.metric_name,
		               'metric_type':ds_c.metric_type,
		               'module': ds_c.module,
               		       'target': ds_c.target,
                               'orig_name': ds_c.name
                              });
          return false;
        }
     })(ds)
  );
  if(ds.metric_type == 'text') li.addClass('txt');
  li.append(a);
}

function time_window_to_seconds(w) {
  var p = w.match(/^(\d+)(M|h|d|w|m|y)$/);
  if(!p) return 86400*2;
  switch(p[2]) {
    case 'M': return p[1] * 60;
    case 'h': return p[1] * 3600;
    case 'd': return p[1] * 86400;
    case 'w': return p[1] * 86400 * 7;
    case 'm': return p[1] * 86400 * 30;
    case 'y': return p[1] * 86400 * 365;
  }
  return 86400*2;
}

///////////////////////////
// Worksheet manipulation
//////////////////////////

var worksheet = (function($) {
  var ws_displayinfo = { start : 14*86400, cnt: '100', end: '' };
  var wsinfo = {};
  var locked = true;
  var streaming = false;
  var stream_graph;

  function ws_locked(warning) {
    if(locked) {
      modal_warning("Worksheet Locked!", warning);
      return locked;
    }
    else if($(".rememberWorksheet:visible").length != 0) {
      modal_warning("Worksheet not saved!", "You must hit 'Remember' to continue editing.");
      return true;
    }
    return locked;
  }

  function stream_data(graph_id) {

    polltime = 1000;
    timewindow = 300000;
    stream_object = stream_graph;
    stream_dirty = false;
    stream_graph.ReconGraphPrepareStream(timewindow, polltime);


//setup functionality so that every second check if we are streaming and dirty, plot if true
    stream_graph.everyTime(1000, function() {
      if(!streaming) {
       $('#streambox').html('');
       $(".stream-log").attr("style", "display:none;");
       stream_graph.stopTime();
      }
      else {
        if(stream_dirty){
          stream_graph.ReconGraphPlotPoints();
          stream_dirty=false;
        }
      }
    });

    $.getJSON("json/graph/info/" + graph_id,
      function(g) {

        sids = "";
        var sidneed = new Object();

        for(var i=0; i<g.datapoints.length; i++) {
          if(g.datapoints[i].sid) {
            sidneed[g.datapoints[i].sid] = polltime;
          }
        }
        for(var sid in sidneed) {
          sids+= "/"+sid+"@"+sidneed[sid];
        }

         //console.log("sids requestd from noit server = ", sids);
         $('#streambox').html('<iframe src="http://bob.office.omniti.com/data'+sids+'"></iframe>');
     });
  }

  function make_ws_graph(g) {
    var o = $('<div></div>').ReconGraph(g);

    zb = $("<li class='zoomGraph' id='Zoom-"+g.graphid+"'><img src='images/zoom_icon.png'></li>");
    zb.attr("graphid", g.graphid);
    zb.attr("graphtype", g.type);
    zb.click(function() { worksheet.zoom($(this).attr("graphid"), $(this).attr("graphtype")); });
    mb = $("<li class='moveGraph' id='Move-"+g.graphid+"'><img src='images/drag_icon.png'></li>");
    rb = $("<li class='deleteWorksheetGraph' id='Remove-"+g.graphid+"'><img src='images/remove_icon.png'></li>");
    rb.attr("graphid", g.graphid);
    rb.click( function() {
      $.getJSON('json/worksheet/deletegraph/' + wsinfo.id + '/' + $(this).attr("graphid"),
        function(r) { if(r.error) { $("#ws-tool-error").html(r.error).fadeIn('fast');  } });
      $("#"+$(this).attr("graphid")).remove();
    });

    var ws_tbar = $('<div class="ws-toolbar"></div>').append("<ul/>").append(zb).append(mb).append(rb);

    ws_tbar.attr("style", "display:none;");

    o.prepend(ws_tbar);
    o.mouseover(
              function() {
                 o.attr("style", "outline: 1px solid #DDDDDD;");
                 ws_tbar.removeAttr("style");
              });
    o.mouseout(
              function() {
                o.removeAttr("style");
                ws_tbar.attr("style", "display:none;");
              });

    return o;
  }

  function zoom_modal (id, gtype) {
    stream_graph = $('<div></div>').ReconGraph({graphid: id, type: gtype});
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
    stream_graph.ReconGraphRefresh({graphid: id});


    var dtool =  $("<div id='mini_ws_datetool'>");
    dtool.append('<div class="zoom"> \
                <dl> \
                        <dt>Zoom:</dt> \
                        <dd><a href="#" class="first datechoice">1d</a></dd> \
                        <dd><a href="#" class="datechoice">2d</a></dd> \
                        <dd><a href="#" class="datechoice">1w</a></dd> \
                        <dd><a href="#" class="selected datechoice">2w</a></dd> \
                        <dd><a href="#" class="datechoice">4w</a></dd> \
                        <dd><a href="#" class="datechoice">1y</a></dd> \
                </dl>\
                 </div>\
                  </div>');

    var mheader = $("<div id='stream-modal-header'>").append(dtool);
    mheader.append("<span class='zoomClose'>x</span>");
    mheader.append("<span class='zoomStream'>Stream Data</span><br>");

    stream_graph.prepend(mheader);

    stream_graph.append("<div class='stream-log' style='display:none'></div>");
    $(".zoomClose").click(function() {
      streaming = false;
      $('#streambox').html('');
      smod.close();
    });

    $(".zoomStream").click(function() {
      if(!streaming) {
        streaming = true;
        $(".zoomStream").html('Streaming!').fadeIn('slow');
        $(".stream-log").removeAttr("style").html("stream log_");
        stream_data(id);
      }
      else if(streaming) {
        streaming = false;
        $('#streambox').html('');
        $(".zoomStream").html('Stream Data').fadeIn('slow');
        $(".stream-log").attr("style", "display:none;");
        stream_graph.ReconGraphRefresh({graphid: id});
      }
    }); //end stream click function

    $("#mini_ws_datetool .datechoice").click(function(){
      $(".datechoice").removeClass("selected");
      $(this).addClass("selected");
      stream_graph.ReconGraphRefresh({graphid: id, start: time_window_to_seconds($(this).html()), end: ''});
      return false;
    });
  }

  function lock_wforms() {
    locked = true;
    $("h2#worksheetTitle").unbind();
    $("ul#worksheet-graphs").unbind();
    $(".ws-toolbar-edit").attr("class","ws-toolbar");
  }

  function unlock_wforms() {
    locked = false;
    $("h2#worksheetTitle").editable(function(value, settings) {
      wsinfo.title = value;
      update_current_worksheet();
      return(value);
    }, { });

    var ul = $("ul#worksheet-graphs");
    ul.sortable({ handle: '.moveGraph',
                  scroll: true,
                  stop:
                    function (e,ui) {
                      wsinfo.graphs = new Array();
                      ui.item.parent().find("> li > div").each(
                        function(i) {
                          wsinfo.graphs.push($(this).attr("id"));
                        }
                      );
                      update_current_worksheet();
                    }
                });

    $(".ws-toolbar").attr("class","ws-toolbar-edit");
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
               lock_wforms();
               modal_warning("Worksheet not saved!", "You must hit 'Remember' to continue editing.");
               $(".rememberWorksheet").click(function() {
                 wsinfo.saved = true;
                 update_current_worksheet(function(r) {
                   if(r.error) wsinfo.saved = false;
                   else {
                      $(".rememberWorksheet").html('Remebered').fadeOut('slow');
                      unlock_wforms();
                   }
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

      $.getJSON("json/graph/info/" + g.graphid,
        function (j) {
            g.type = j.type;
            g.graphid = j.id;
            var o = make_ws_graph(g);
            ul.append($('<li/>').append(o));
            o.ReconGraphRefresh();
            wsinfo.graphs.push(g.graphid);
         });

    }
    ul.sortable("refresh");
  }

  function add_graph_to_worksheet(graphid) {
    if(!ws_locked("Click 'Edit Worksheet' to unlock.")){
      for(var i = 0; wsinfo.graphs && (i < wsinfo.graphs.length); i++) {
        if(wsinfo.graphs[i]==graphid) {
          modal_warning("", "Worksheets cannot have duplicate graphs!");
          return;
        }
      }
      var g = { start: ws_displayinfo.start,
                end: ws_displayinfo.end,
                cnt: ws_displayinfo.cnt,
                graphid: graphid };

      $.getJSON("json/graph/info/" + g.graphid,
        function (j) {
          g.type = j.type;
          g.graphid = j.id;
          var o = make_ws_graph(g);
          var ul = $("ul#worksheet-graphs");
          ul.append($('<li/>').append(o));
          o.ReconGraphRefresh();
          ul.sortable("refresh");
          if(!wsinfo.graphs) {wsinfo.graphs = new Array();}
          wsinfo.graphs.push(graphid);
          update_current_worksheet();
          unlock_wforms();
        });
    }
  }
  function display_info(start,end,cnt) {
    ws_displayinfo.start = start;
    ws_displayinfo.end = end;
    if(cnt) ws_displayinfo.cnt = cnt;
  }
  function refresh_worksheet() {
    var g = { start: ws_displayinfo.start,
              end: ws_displayinfo.end,
              cnt: ws_displayinfo.cnt };
    $("ul#worksheet-graphs > li > div").ReconGraphRefresh(g);
  }
  function load_worksheet(id) {
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
  return {
    load: load_worksheet,
    display_info: display_info,
    refresh: refresh_worksheet,
    add_graph: add_graph_to_worksheet,
    update: update_current_worksheet,
    lock: lock_wforms,
    unlock: unlock_wforms,
    zoom: zoom_modal,
    stream: stream_data,
    islocked: function () { return locked; }
  };
})(jQuery);
