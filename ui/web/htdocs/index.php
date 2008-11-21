<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<title>Reconnoiter</title>
<link href="css/style.css" rel="stylesheet" type="text/css" />
<link href="css/datepicker.css" rel="stylesheet" type="text/css" />
<link href="css/colorpicker.css" rel="stylesheet" type="text/css" />
<link rel="icon" type="image/vnd.microsoft.icon" href="images/favicon.ico" />
<script src="js/htmlentities.js"></script>
<script src="js/json2.js"></script>
<script src="js/jquery.min.js"></script>
<script src="js/jquery.flot.js"></script>
<script src="js/jquery.jeditable.pack.js"></script>
<script src="js/ui.core.min.js"></script>
<script src="js/ui.sortable.min.js"></script>
<script src="js/jquery.cookie.js" type="text/javascript"></script>
<script src="js/jquery.treeview.js" type="text/javascript"></script>
<script src="js/jquery.treeview.async.js" type="text/javascript"></script>
<script src="js/recon.js" type="text/javascript"></script>

<!-- color picker -->
<link rel="stylesheet" href="css/colorpicker.css">
<script type="text/javascript" src="js/colorpicker.js"></script>

<!-- Import jQuery and SimpleModal source files -->

<script src='js/jquery.simplemodal.js' type='text/javascript'></script>

<!-- Confirm JS and CSS files -->
<script src='js/confirm.js' type='text/javascript'></script>
<link type='text/css' href='css/confirm.css' rel='stylesheet' media='screen' />

<!-- IE 6 hacks -->
<!--[if lt IE 7]>
<link type='text/css' href='css/confirm_ie.css' rel='stylesheet' media='screen' />
<![endif]-->

<!--accordion-->
<script type="text/javascript">
$(document).ready(function(){
   $(".accordion h3:first").addClass("active");
   $(".accordion span").slideUp();
   $(".accordion span:first").slideDown();

   $("div#content > div:first").siblings().slideUp();

   $(".accordion h3").click(function(){
       $(this).next("span").slideToggle("normal")
       .siblings("span:visible").slideUp("normal");
       $(this).toggleClass("active");
       $(this).siblings("h3").removeClass("active");
       $(this).siblings("h3").each(function(e) {
         $("#" + $(this).attr("id") + "_panel").slideUp("fast");
       });
       if($(this).hasClass("active"))
         $("#" + $(this).attr("id") + "_panel").slideDown("fast");
       else
         $("#" + $(this).attr("id") + "_panel").slideUp("fast");
   });

}); 
</script>

   <script type="text/javascript">
        jQuery(document).ready(function(){
            $("#targets").treeview({
              url: "json/ds/remote_address/target/name/metric_name",
              params: {} // will become hash, indexed by id, of url params
              })
        });
    </script>

   <script type="text/javascript">
        jQuery(document).ready(function(){
            $("#templates").treeview({
	      url: "json/templates/templateid/metric_name",
	      params: {}
              })
        });
    </script>

<!-- search tabs -->
<script type="text/javascript">
	$(function () {
		var tabContainers = $('div.tabs > div');
		tabContainers.hide().filter(':first').show();
		
		$('div.tabs ul.tabNavigation a').click(function () {
			tabContainers.hide();
			tabContainers.filter(this.hash).show();
			$('div.tabs ul.tabNavigation a').removeClass('selected');
			$(this).addClass('selected');
			return false;
		}).filter(':first').click();

		var wstabContainers = $('div.ws-tabs > div');
		wstabContainers.hide().filter(':first').show();
		
		$('div.ws-tabs ul.tabNavigation a').click(function () {
			wstabContainers.hide();
			wstabContainers.filter(this.hash).show();
			$('div.ws-tabs ul.tabNavigation a').removeClass('selected');
			$(this).addClass('selected');
			return false;
		}).filter(':first').click();
	});
</script>

<!-- math box -->

<!-- alert / Remove this when new script is made -->
<script type="text/javascript">
function MM_jumpMenu(targ,selObj,restore){ //v3.0
  eval(targ+".location='"+selObj.options[selObj.selectedIndex].value+"'");
  if (restore) selObj.selectedIndex=0;
}
</script>

</head>

<body>
<div id="header">
	<h1><a href="#">Reconnoiter</a></h1>
	<ul>
		<li><a href="https://labs.omniti.com/docs/reconnoiter/">Documentation</a></li>
		<li><a href="https://labs.omniti.com/trac/reconnoiter/">Support</a></li>
		<li><a href="#">Username</a></li>
		<li class="xx"><a href="#">Logout</a></li>
	</ul>
</div><!-- end header -->
<div id="left">
	<div class="accordion">
	<?php include('worksheet_controls.inc') ?>
        <?php include('search_controls.inc') ?>
	</div>
</div><!-- end left -->
<div id="container">
	<div id="content">
		<div id="worksheet_controls_panel">
		<?php include('worksheet_panel.inc') ?>
		</div><!-- end main -->
		<div id="graph_controls_panel">
		<?php include('graph_panel.inc') ?>
		</div><!-- end main -->
	</div><!-- end content -->
</div><!-- end container -->
</body>
</html>
