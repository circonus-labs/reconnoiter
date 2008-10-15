(function ($) {
  var ReconGraph = function() {
    var displayinfo = { start : 14*86400, end: '', width: 380, height: 180 };
    return {
      init:
        function(options) {
          this.graphinfo = $.extend({}, displayinfo, options||{});
          if(!this.graphinfo.cnt) this.graphinfo.cnt = this.graphinfo.width / 2;
          this.attr("id", this.graphinfo.graphid)
              .append($('<h3>').addClass("graphTitle")
                               .html(this.graphinfo.title))
              .append($('<div></div>').addClass("plot-area")
                                      .css('width', this.width + 'px')
                                      .css('height', this.height + 'px'))
              .append($('<div></div>').addClass("plot-legend"));
          return this;
        },
      refresh:
        function() {
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
          var plot = $.plot(placeholder, r.data, r.options);

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
                ReconGraphPlot: ReconGraph.plot
              });
})(jQuery);
