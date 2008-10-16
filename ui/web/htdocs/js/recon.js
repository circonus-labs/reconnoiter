(function ($) {
  var ReconGraph = function() {
    var displayinfo = { start : 14*86400, end: '', width: 380, height: 180 };
    return {
      init:
        function(options) {
          this.graphinfo = $.extend({}, displayinfo, options||{});
          if(!this.graphinfo.cnt) this.graphinfo.cnt = this.graphinfo.width / 2;
          if(!this.attr("id")) this.attr("id", this.graphinfo.graphid);
          this.append($('<h3>').addClass("graphTitle")
                               .html(this.graphinfo.title))
              .append($('<div></div>').addClass("plot-area")
                                      .css('width', this.width + 'px')
                                      .css('height', this.height + 'px'))
              .append($('<div></div>').addClass("plot-legend"));
          return this;
        },
      reset:
        function() {
          this.graphinfo.graphid = '';
          if(this.flot_plot) {
            this.find("h3.graphTitle").html('');
            this.find("div.plot-legend").html('');
            this.flot_plot.setData({});
            this.flot_plot.setupGrid();
            this.flot_plot.draw();
          }
          return this;
        },
      refresh:
        function(options) {
          this.graphinfo = $.extend({}, this.graphinfo, options||{});
          var url = "flot/graph/settings/" + this.graphinfo.graphid;
          this.find(".plot-area")
              .html('<div class="centered"><div class="loading">&nbsp;</div></div>');
          $.getJSON(url, {'cnt':this.graphinfo.cnt,
                          'start':this.graphinfo.start,
                          'end':this.graphinfo.end},
                    (function(o) { return function (r) { o.ReconGraphPlot(r, function() { o.ReconGraphRefresh(); }) }})(this));
          return this;
        },
      plot:
        function (r, redraw) {
          this.find("h3.graphTitle").html(this.graphinfo.title);
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
        }
    };
  }();
  $.fn.extend({ ReconGraph: ReconGraph.init,
                ReconGraphRefresh: ReconGraph.refresh,
                ReconGraphPlot: ReconGraph.plot,
                ReconGraphReset: ReconGraph.reset
              });
})(jQuery);

function perform_graph_search(domid, wsmode, string, offset, limit) {
  $.getJSON('json/graph/search',
            { 'q' : string, 'o' : offset, 'l' : limit },
            function(r) {
              var summary = r.count + ' graph' + (r.count == 1 ? '' : 's' ) + ' found for \'' + htmlentities(r.query) + '\'';
              if(r.error) summary = 'Error: ' + htmlentities(r.error);
              $(domid + " > p.graph-search-summary").html(summary).fadeIn('fast');
              var c = new Number(r.count);
              var l = new Number(r.limit);
              var o = new Number(r.offset);
              var page = $(domid + " > p.paginate");
              page.html('');
              if(c > l) {
                if(o > 0) {
                  var po = Math.max(o-l, 0);
                  $('<a/>').html( (po+1) + ' - ' + (po+l) )
                           .click(function() {
                             perform_datapoint_search(domid,wsmode,string,po,r.limit);
                             return false;
                           }).appendTo(page);
                }
                page.append($('<span/>').html((o+1) + '-' + (o+l)).addClass('searchselect'));
                if(o + l < c) {
                  var po = o + l;
                  $('<a/>').html( (po + 1) + '-' + (po+l) )
                           .click(function() {
                             perform_datapoint_search(domid,wsmode,string,po,r.limit);
                             return false;
                           }).appendTo(page);
                }
                page.slideDown('fast');
              }
              else page.slideUp('fast');
              $(domid + " > ul.graph-searchresults > li").remove();
              for(var i=0; r.results && i<r.results.length; i++) {
                var g = r.results[i];
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
                var li = $('<li/>');
                var del = $('<a href="#"/>');
                del.html('Forget').addClass('deletegraph');
                del.click(
                  (function(graphid, li) {
                      return function() {
                        $.getJSON('json/graph/forget/' + graphid,
                          function (r) {
                            if(r.error) { alert(r.error); }
                            else {
                              perform_graph_search(domid,wsmode,string,o,l);
                            }
                          });
                        return false;
                      }
                   })(g.graphid, li)
                );
                var ul = $('<ul/>');
                ul.append($('<li/>').html(g.last_update));
                if(wsmode) {
                  ul.append($('<li/>').append(add));
                }
                else {
                  ul.append($('<li/>').append(edit));
                  ul.append($('<li/>').append(del));
                }
                li.append($('<a/>').html(g.title)).append(ul);
                $(domid + " > ul.graph-searchresults").append(li);
              }
            });
}
