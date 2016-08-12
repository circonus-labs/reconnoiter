<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<?php
  require_once('Reconnoiter_DB.php');
  $db = Reconnoiter_DB::getDB();
?>
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<title>Reconnoiter</title>

<link href="css/style.css" rel="stylesheet" type="text/css" />
<link href="css/datepicker.css" rel="stylesheet" type="text/css" />
<link href="css/colorpicker.css" rel="stylesheet" type="text/css" />
<link rel="icon" type="image/vnd.microsoft.icon" href="images/favicon.ico" />
<link href="js/jquery-ui-1.7.2/themes/base/ui.all.css" rel="stylesheet" type="text/css" />
<script>document.domain='<?php echo $db->realtime_config('document_domain'); ?>';</script>
<script src="js/htmlentities.js"></script>
<script src="js/json2.js"></script>
<script src="js/jquery-1.3.2.min.js"></script>
<script src="js/jquery-ui-1.7.2/ui/ui.core.js"></script>
<script src="js/jquery-ui-1.7.2/ui/ui.sortable.js"></script>
<script src="js/jquery-ui-1.7.2/ui/ui.slider.js"></script>
<script src="js/jquery-ui-1.7.2/ui/ui.draggable.js"></script>
<script src="js/jquery-ui-1.7.2/ui/ui.droppable.js"></script>
<script src="js/jquery.flot.js"></script>
<script src="js/jquery.jeditable.pack.js"></script>
<script src="js/jquery.cookie.js" type="text/javascript"></script>
<script src="js/jquery.treeview.js" type="text/javascript"></script>
<script src="js/jquery.treeview.async.js" type="text/javascript"></script>
<script src="js/jquery.timers.js" type="text/javascript"></script>
<script src="js/jquery.ajaxq-0.0.1.js" type="text/javascript"></script>
<script src="js/eye/datepicker.js" type="text/javascript"></script>
<script src="js/eye/colorpicker.js" type="text/javascript"></script>
<script src="js/eye/eye.js" type="text/javascript"></script>
<script src="js/eye/utils.js" type="text/javascript"></script>
<script src="js/recon.js" type="text/javascript"></script>
<script type="text/javascript">
<!--
recon_realtime_hostname = '<?php echo $db->realtime_config('hostname'); ?>';
-->
</script>


</head>

<body>
<div id="header">
	<h1><a href="#">Reconnoiter</a></h1>
	<ul>
		<li><a href="https://labs.omniti.com/docs/reconnoiter/">Documentation</a></li>
		<li><a href="https://labs.omniti.com/trac/reconnoiter/">Support</a></li>
		<!--
                <li><a href="#">Username</a></li>
		<li class="xx"><a href="#">Logout</a></li>
                -->

                <li><a href=#>Username</a></li>
		<li><a href="http://localhost/Logout.php">Logout</a></li>

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
<script src="js/reconui.js" type="text/javascript"></script>
</body>
</html>
