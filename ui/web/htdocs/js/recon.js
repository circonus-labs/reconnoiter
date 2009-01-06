//global objects to use for calling plot_ifram_data from stream
var stream_object;
var stream_dirty;

//set the global streaming object to the local ReconGraph object to use,
// and init,update the global streaming boolean to then call this from a server
function plot_iframe_data(uuid, metric_name, ydata, xdata) {
  stream_object.ReconGraphAddPoint(uuid, metric_name, xdata, ydata);
  stream_dirty = true;
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

function rpn_eval(value, expr) {
  s = [];
  ops = expr.split(",");
  s.unshift(value)

  for (i = 0; i < ops.length; i++) {
	op = ops[i];

    switch(op) {
      case 'ln':
        s.unshift(Math.log(s.shift())); break;
      case 'round':
        r = s.shift();
        l = s.shift();
        s.unshift(Math.round(l, r));
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
      case '.':
        s.unshift(s[s.shift()]); break;
      case '+':
        s.unshift(s.shift() + s.shift()); break;
      case '*':
        s.unshift(s.shift() * s.shift()); break;
      // Not implemented
      // case 'auto':
      //   s.unshift( $this->autounits(s.shift())); break;
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
  value = s.shift();
  return value;
}

(function ($) {
  var ReconGraph = function() {
    var displayinfo = { start : 14*86400, end: '', width: 380, height: 180 };
    var doptions, dplaceholder, ddata;
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
      clear:
	function () {                      
	    if(this.flot_plot) {
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
        function (uuid, metric_name, xdata, ydata) {
	    tdata = [xdata, ydata.toString()];

	    for(var i=0; i<ddata.length;i++) {
		if( !ddata[i].hidden && (ddata[i].uuid_name ==  (uuid+"-"+metric_name)) ) {

		    if(ddata[i].data.length>0) ddata[i].lastval = ddata[i].data[0];
		    if(ddata[i].lastval) {
			slope = (tdata[0] - ddata[i].lastval[0]) / (tdata[1] - ddata[i].lastval[1]); 		     
			if(ddata[i].derive_val == 'derive') {
			    tdata[1] = slope;
			}
			else if(ddata[i].derive_val == 'counter') {
			    if(slope>=0) tdata[1] = tdata[1] - ddata[i].lastval[1];
			    else tdata[1] = '';
			}
		    } //end if there was a previous value available
		    
		    if(tdata[1]!=''){
			if(ddata[i].reconnoiter_source_expression) {
			    tdata[1] = rpn_eval(tdata[1], ddata[i].reconnoiter_source_expression);
			}
			if(ddata[i].reconnoiter_display_expression) {
			    tdata[1] = rpn_eval(tdata[1], ddata[i].reconnoiter_display_expression);
			}
			if(ddata[i].data.unshift(tdata) >70) {
			    ddata[i].data.pop();
			}
		    } //end if ydata was a number
		} //end if the uuid and metric_name match
	    } //end for each dataset	    
	    return this;
	},
    PlotPoints:
        function () {
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
          $.getJSON(url, {'cnt':this.graphinfo.cnt,
                          'start':this.graphinfo.start,
                          'end':this.graphinfo.end,
 		          'type':this.graphinfo.type},
                    (function(o) { return function (r) { o.ReconGraphPlot(r, function() { o.ReconGraphRefresh(); }) }})(this));
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
          if(!r.options.grid) r.options.grid = {};
          r.options.grid.hoverable = true;
          $("div.tooltip").remove();
          r.options.legend.container = this.find("div.plot-legend");
          r.options.grid.autoHighlight = false;
          if(!r.options.yaxis) r.options.yaxis = {};
          if(r.options.yaxis.suffix)
            r.options.yaxis.tickFormatter = function (val, axis) {
              return val.toFixed(axis.tickDecimals) + r.options.yaxis.suffix;
            };
          doptions = r.options;
          dplaceholder = placeholder;
          ddata = r.data;
          var plot = this.flot_plot = $.plot(placeholder, r.data, r.options);
          var hovering;
          placeholder.bind("plothover", function (event, pos, item) {
            if(hovering) plot.unhighlight(hovering.series, hovering.datapoint);
            if(item && (item.datapoint[1] != "" || item.datapoint[2] != null)) {
              // Emulate opacity on white
              var soft = 'rgb(' +
                         (item.series.color.match(/\((.+)\)/))[1]
                                           .split(',')
                                           .map(function(a) {
                                             return Math.round(255-(255-a)*0.1);
                                           })
                                           .join(',') +
                         ')';
              if(! $("div.tooltip")[0])
                $('<div class="tooltip"></div>').appendTo($('body'));
              $("div.tooltip")
                .html((item.datapoint[2] ? item.datapoint[2] : item.datapoint[1]) + " (" + item.series.label + ")")
                .css( { top: item.pageY - 15,
                        left: item.pageX + 10,
                        border: '1px solid ' + item.series.color,
                        backgroundColor: soft,
                        position: 'absolute',
                        'z-index': 1000 });
              hovering = item;
              plot.highlight(item.series, item.datapoint);
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
              ReconGraphClear: ReconGraph.clear,
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
  return r.count + ' worksheet' + (r.count == 1 ? '' : 's' ) + ' found for \'' + htmlentities(r.query) + '\'';
}
function graph_search_summary(r) {
  return r.count + ' graph' + (r.count == 1 ? '' : 's' ) + ' found for \'' + htmlentities(r.query) + '\'';
}
function datapoint_search_summary(r) {
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
          $.getJSON('json/graph/forget/' + graphid,
            function (r) {
              if(r.error) { alert(r.error); }
              else { 
                perform_graph_search_edit(params);
              }
            });
		});		
          return false;
        }
    })(g.graphid, li)
	    );
  var ul = $('<ul/>');
  ul.append($('<li/>').html(g.last_update));
  ul.append($('<li/>').append(edit));
  ul.append($('<li/>').append(del));
  li.append($('<a/>').html(g.title)).append(ul);
}
function ws_for_edit(li, ws, params) {
  var add = $('<a href="#"/>');
  add.html('View').addClass('addtows');
  add.click(
    (function(sheetid) {
        return function() {
          load_worksheet(sheetid);
          return false;
        }
     })(ws.sheetid)
  );
  var ul = $('<ul/>');
  ul.append($('<li/>').html(ws.last_update));
  ul.append($('<li/>').append(add));
  li.append($('<a/>').html(ws.title)).append(ul);
}
function graphs_for_worksheet(li, g, params) {
  var add = $('<a href="#"/>');
  add.html('Add').addClass('addtows');
  add.click(
    (function(graphid) {
        return function() {
          add_graph_to_worksheet(graphid);
          return false;
        }
     })(g.graphid)
  );
  var ul = $('<ul/>');
  ul.append($('<li/>').html(g.last_update));
  ul.append($('<li/>').append(add));
  li.append($('<a/>').html(g.title)).append(ul);
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
                               'metric_type':ds_c.metric_type
                              });
          return false;
        }
     })(ds)
  );
  if(ds.metric_type == 'text') li.addClass('txt');
  li.append(a);
}
