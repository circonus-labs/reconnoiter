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
				tdata[1] = rpn_eval(tdata[1], ddata[i].reconnoiter_source_expression);
			}
			    if(ddata[i].reconnoiter_display_expression) {
				tdata[1] = rpn_eval(tdata[1], ddata[i].reconnoiter_display_expression);
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
	  r.options.xaxis.localtime = true;
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
                        'z-index': 4000 });
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
          load_worksheet(sheetid);
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
          add_graph_to_worksheet(graphid);
          return false;
        }
     })(g.graphid)
  );
  qview.html('Quick View').addClass('quickviewgraph');
  qview.click(
    (function(graphid, gtype) {
      return function() { zoom_modal(graphid, gtype); return false; }
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
