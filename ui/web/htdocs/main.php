<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<title>Reconnoiter</title>
<link href="css/style.css" rel="stylesheet" type="text/css" />
<script src="js/json2.js"></script>
<script src="js/jquery.min.js"></script>
<script src="js/jquery.cookie.js" type="text/javascript"></script>
<script src="js/jquery.treeview.js" type="text/javascript"></script>
<script src="js/jquery.treeview.async.js" type="text/javascript"></script>

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
   $(".accordion h3").eq(1).addClass("active");
   $(".accordion span").eq(1).slideUp();
   $(".accordion span:last").slideDown();

   $(".accordion h3").click(function(){
       $(this).next("span").slideToggle("normal")
       .siblings("span:visible").slideUp("normal");
       $(this).toggleClass("active");
       $(this).siblings("h3").removeClass("active");
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


<!--accordion 2-->
<script type="text/javascript">
$(document).ready(function(){
   $(".accordion2 h4").eq(1).addClass("active");
   $(".accordion2 div").eq(1).slideUp();
   $(".accordion2 div:last").slideDown();

   $(".accordion2 h4").click(function(){
       $(this).next("div").slideToggle("normal")
       .siblings("div:visible").slideUp("normal");
       $(this).toggleClass("active");
       $(this).siblings("h4").removeClass("active");
   });

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
	});
</script>

<!-- math box -->

<!-- alert / Remove this when new script is made -->
<script type="text/javascript">
function disp_alert()
{
alert("display the correct graph")
}
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
		<div id="tab_content">
			<ul>
				<li><a href="#" class="selected">Graph Panel</a></li>
				<li><a href="#">Worksheet</a></li>
			</ul><div style="clear:both;"></div>
		</div>
		<!-- buttons -->
		<div id="buttons">
		<input name="Save" type="button" value="New Graph" /> <input name="Save" type="button" value="Save" /> <input name="Save" type="button" value="Save as" /> <input name="Save" type="button" value="Delete" /> 
		</div>
		<!-- graph and controls -->
		<div id="main">
		<?php include('graph_controls.inc') ?>
		</div><!-- end main -->
	</div><!-- end content -->
</div><!-- end container -->
</body>
</html>
