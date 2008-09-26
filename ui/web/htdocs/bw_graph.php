<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=iso-8859-1" />
<title>Bandwidth Graph</title>
<script src="js/jquery.min.js"></script>
<script language="javascript" type="text/javascript" src="js/jquery.flot.js"></script>
</head>
<body>
	<div id="maingraph" style="text-align:center; width:600px;">
	<span class="plot-title"></span><br/>
        <div class="plot-area" style="width:600px;height:300px"></div><br/>
	<div><div class="plot-legend">[legend]</div></div>
	</div>

  <script language="javascript">
  function plot_id(r) {
    $("span.plot-title").html(r.title);
    var placeholder = $("#maingraph > div.plot-area");
    placeholder.bind("plotselected", function (event, ranges) {
/*
        $("#selection").text(ranges.xaxis.from.toFixed(1) + " to " + ranges.xaxis.to.toFixed(1));
        var zoom = $("#zoom").attr("checked");
        if (zoom)
            plot = $.plot(placeholder, data,
                          $.extend(true, {}, options, {
                              xaxis: { min: ranges.xaxis.from, max: ranges.xaxis.to }
                          }));
*/
    });
    r.options.legend.container = $("#maingraph div.plot-legend");
    var plot = $.plot(placeholder, r.data, r.options);

/*
    $("#clearSelection").click(function () {
        plot.clearSelection();
    });

    $("#setSelection").click(function () {
        plot.setSelection({ x1: 1994, x2: 1995 });
    });
*/
  }
  $(document).ready(function () {
    $('').ajaxError(function (a,b,c) { console.log(a); console.log(b); console.log(c); });
    //$.getJSON('bw_settings.php', {'id': '<?php print $_GET['id'] ?>', 'cnt': '<?php print $_GET['cnt'] ?>'}, function(r) { plot_id(r); } );
    $.getJSON('flot/graph/settings/48dc5706-abc8-29d2-ef23-ce754a8ec3d9?cnt=&start=1209600&end=&cnt=&start=1209600&end=', function(r) { plot_id(r); } );
  });
  </script>

</body>
</html>
