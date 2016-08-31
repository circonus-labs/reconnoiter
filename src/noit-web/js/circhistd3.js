function CirconusHistogram(options) {
  if (!options) options = {}
  var req_width = options.width || 500;
  var req_height = options.height || 200;
  var minbarwidth = options.minbarwidth || 5;
  var qheight = 8, fbh = 0;

  qheight -= Math.floor(3 * Math.max(80 - Math.max(req_height, 30), 0) / 50);

  var margin = {top: 0, right: 10, bottom: qheight, left: 0};

  if (options.yaxis) { 
    margin.left = 60;
  }
  if (options.xaxis) { 
    margin.bottom = 20;
  }
  var width = req_width - margin.left - margin.right,
      height = req_height - margin.top - margin.bottom;
  var nxticks = options.nxticks || Math.floor(width/150);
  var nyticks = Math.max(options.nyticks || Math.floor(height/150), 2);
  if (options.fullband) {
    fbh = height;
    qheight = 0;
  }

  var x = d3.scaleLinear().range([0, width]);
  
  var y = d3.scaleLinear().range([height, 0]);
 
  var test = function(tgt, path) { 
    d3.json(path, function(error, json) {
      if (error) throw error;
      render(tgt, json);
    })
  }

  var render = function(tgt, json) {
    var div = d3.select(tgt).append("div")
      .attr("class", "tooltip")
      .style("opacity", 0);

    var svg = d3.select(tgt).append("svg")
        .attr("width", width + margin.left + margin.right)
        .attr("height", height + margin.top + margin.bottom)
      .append("g")
        .attr("transform", "translate(" + margin.left + "," + margin.top + ")");

    var getWidth = function(base) {
      return (base == 0.0) ? 0 : Math.pow(10,Math.floor(Math.log10(Math.abs(base)))-1.0);
    }
  
    if (!json.hasOwnProperty("0")) {
      // Anchor a zero, we need that.
      json["0"] = 0
    }
    var bins = []
    var binv = []
    for (var bin in json) { binv.push(bin) }
    binv.sort(function(a,b) { return parseFloat(a) < parseFloat(b) ? -1 : 1; })
    var hist = new circllhist();
    for (var i=0; i<binv.length; i++) {
      bin = binv[i];
      fullBin = {offset: parseFloat(bin), width: getWidth(parseFloat(bin)), value: json[bin], height: json[bin]};
      if(parseFloat(bin) >= 0) {
        hist.insert(parseFloat(bin), json[bin]);
        bins.push(fullBin);
      }
    }
  
    var qs = []
    var qin = [0,0.25,0.5,0.75,1];
    var iqr = []
    var iqr_lines = []
    hist.approx_quantile(qin, qs);
    var mean_value = hist.approx_mean(hist);
    if(qs.length == 5) {
      var cutoff = qs[4];
      var iqr_w = qs[3] - qs[1];
      if(iqr_w*4 > qs[2]) cutoff = iqr_w*4;
      for (var i=0; i<4; i++) {
	   iqr.push({ offset: qs[i], width: qs[i+1]-qs[i],
			    label: "p("+Math.floor(qin[i]*100)+")-p("+Math.floor(qin[i+1]*100)+")",
                   qname: "iqr"+Math.floor(qin[i]*100)+"-"+Math.floor(qin[i+1]*100) })
        iqr_lines.push({ qname: "iqr"+Math.floor(qin[i]*100), offset: qs[i] });
      }
      iqr_lines.push({ qname: "iqr"+Math.floor(qin[4]*100), offset: qs[4] });
      mean_line = [{ qname: "qbar-mean", offset: mean_value }];
    }
    // Set the scale domain.
    x.domain([0, d3.max(bins.map(function(d) { return d.offset > cutoff ? 0 : d.offset + d.width; }))]);
  
    // Find the smallest bin that is still large enough to see.
    var minunit = 0, stopunit;
    for(var i=0; i<bins.length; i++) {
      d = bins[i];
      var bar_width = x(d.offset + d.width) - x(d.offset);
      stopunit = minunit = Math.pow(10,Math.floor(Math.log10(d.offset)))
      if (bar_width >= minbarwidth) {
        if(bar_width/5 >= minbarwidth) {
          minunit /= 5;
        }
        if(bar_width/2 >= minbarwidth) {
          minunit /= 2;
        }
        break;
      }
    }
  
    // Zip through the bins consolidate to our min unit / 10
    var cap = minunit/10, c_idx = 0;
    bins[0].width = minunit/10
    var i=0;
    for(i=0; i<bins.length; i++) {
      d = bins[i];
      if(d.offset >= stopunit) break;
      if(d.offset >= cap) {
        c_idx = i;
        cap += minunit/10;
        bins[c_idx].offset = cap - (minunit/10);
        bins[c_idx].width = minunit/10
      }
      if(i != c_idx) {
        bins[c_idx].height += bins[i].height;
        bins[i].height = bins[i].width = 0;
      }
    }
    for(;i<bins.length;i++) {
      if(bins[i].width > minunit/10) {
        var cnt = bins[i].width / (minunit/10);
        bins[i].height /= cnt;
      }
    }
    // rebuild removing empty bins from consolidation
    var oldbins = bins;
    bins = [];
    for(var i=0; i<oldbins.length; i++) {
      if(oldbins[i].height) {
        bins.push(oldbins[i]);
      }
    }
  
    // Build a domain on our newly consolidated heights
    y.domain([0, d3.max(bins.map(function(d) { return 1.1 * d.height; }))]);
 
    var iqrbins; 
    if(!fbh) {
      iqrbins = svg.selectAll(".iqrbin").data(iqr)
      iqrbins.enter().append("rect")
          .attr("class", "iqrbin hidden")
          .attr("x", function(d) { return x(d.offset); })
          .attr("width", function(d) { return x(d.width) || 1; })
          .attr("y", function(d) { return 0; })
          .attr("height", function(d) { return height; });
    }
    svg.selectAll(".qbin")
        .data(iqr)
      .enter().append("rect")
        .attr("class", function(d) { return "qbar qbar-iqr " + d.qname })
        .attr("x", function(d) { return x(d.offset); })
        .attr("width", function(d) { return x(d.width) || 1; })
        .attr("y", function(d) { return height-fbh; })
        .attr("height", function(d) { return fbh+qheight; })
	   .on("mouseover", function(d) {
            div.transition()
                .duration(200)
                .style("opacity", 1);
            div.html(d.label + "<br/>[" + d3.format(".2s")(d.offset) + "-" + d3.format(".2s")(d.offset + d.width) + ")")
                .style("left", (x(d.offset)) + "px")
                .style("top", (y(0) + qheight) + "px");
            if(iqrbins) {
              svg.selectAll(".iqrbin") //.data(iqr)
                .attr("class", function(dl) { return (dl.offset == d.offset) ? "iqrbin qbar qbar-iqr " + d.qname : "iqrbin hidden" })
            }
          })
        .on("mouseout", function(d) {
            svg.selectAll(".iqrbin").attr("class", "iqrbin hidden");
            div.transition()
                .duration(500)
                .style("opacity", 0);
	     });
  
    svg.selectAll(".qbin")
        .data(iqr_lines)
      .enter().append("line")
        .attr("class", function(d) { return "qbar " + d.qname })
        .attr("x1", function(d) { return x(d.offset); })
        .attr("x2", function(d) { return x(d.offset); })
        .attr("y1", function(d) { return height-fbh; })
        .attr("y2", function(d) { return height+qheight; });

    svg.selectAll(".qbin")
        .data(mean_line)
      .enter().append("line")
        .attr("class", function(d) { return "qbar " + d.qname })
        .attr("x1", function(d) { return x(d.offset); })
        .attr("x2", function(d) { return x(d.offset); })
        .attr("y1", function(d) { return height; })
        .attr("y2", function(d) { return 0; });

    var splits = [ { idx:0 }, { idx:1 }, { idx:2 } ];
    // Add the bins.
    svg.selectAll(".bin")
        .data(bins)
      .enter().append("rect")
        .attr("class", "bin")
        .attr("x", function(d) { return x(d.offset); })
        .attr("width", function(d) { return x(d.offset + d.width) - x(d.offset)-1; })
        .attr("y", function(d) { return y(d.height); })
        .attr("height", function(d) { return height - y(d.height); })
        .on("mouseover", function(ds) {
              if(!options.hudlegend) return;
		    rect = svg.selectAll(".bin").data(bins);
              var lt = 0, eq = 0, gt = 0;
              rect.attr("class", function(d) {
                if (ds.offset == d.offset) { eq = d.value; return "bin bin-select"; }
                else if (ds.offset > d.offset) { lt += d.value; return "bin bin-less"; }
                else if (ds.offset < d.offset) { gt += d.value; return "bin bin-more"; }
              });

              splits[0].value = lt; splits[0].p = lt/(lt+eq+gt);
              splits[1].value = eq; splits[1].p = eq/(lt+eq+gt);
              splits[2].value = gt; splits[2].p = gt/(lt+eq+gt);
              legend = svg.selectAll(".legend").data(splits);
              legend.enter().append("text")
                .attr("class", "legend")
                .attr("text-anchor", "end")
                .attr("transform", function(d) { return "translate(" + x(cutoff) + "," + (10 + d.idx * 10) + ")" })
                .text(function(d) {
                    var indicator = (d.idx == 1) ? "=" : ((d.idx == 2) ? ">" : "<");
                    return indicator + " " + d3.format(".2s")(d.value) + " - " + d3.format(".2%")(d.p);
                  });
            })
        .on("mouseout", function(d) {
              if(!options.hudlegend) return;
		    rect = svg.selectAll(".bin").data(bins);
              rect.attr("class", "bin");
		    svg.selectAll(".legend").remove();
            });

    if(options.yaxis) {  
      svg.append("g")
        .attr("class", "axis axis--y")
        .attr("transform", "translate(0,0)")
        .call(d3.axisLeft(y).tickFormat(d3.format(".2s"))
                .ticks(nyticks));
    }

    if(options.xaxis) {  
      svg.append("g")
        .attr("class", "axis axis--x")
        .attr("transform", "translate(0," + height + ")")
        .call(d3.axisBottom(x).tickFormat(d3.format(".2s"))
                .ticks(nxticks));
    } else {
      svg.append("text")
	   .attr("text-anchor", "end")
        .attr("transform", "translate(" + x(cutoff) + "," + (height-5) + ")")
        .text(d3.format(".2s")(cutoff));
    }
  }
  return { test: test, render: render };
}
