(function ($) {
  var ReconGraph = function() {
    var displayinfo = { start : 14*86400, end: '', width: 380, height: 180 };
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
                          'end':this.graphinfo.end},
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
                ReconGraphMacro: ReconGraph.macro
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
          $.getJSON('json/graph/forget/' + graphid,
            function (r) {
              if(r.error) { alert(r.error); }
              else {
                perform_graph_search(params);
              }
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
