<?php
$otype = $_GET['otype'];
$id = $_GET['id'];
$start = $_GET['start'];
$end = $_GET['end'];
$gran = $_GET['gran'];

require_once('Reconnoiter_DB.php');
$db = Reconnoiter_DB::getDB();
?>

<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<title>Reconnoiter Viewer</title>
<link href="css/style.css" rel="stylesheet" type="text/css" />
<link href="css/datepicker.css" rel="stylesheet" type="text/css" />
<link href="css/colorpicker.css" rel="stylesheet" type="text/css" />
<link rel="icon" type="image/vnd.microsoft.icon" href="images/favicon.ico" />
<link href="js/jquery-ui-1.7.2/themes/base/ui.all.css" rel="stylesheet" type="text/css" />
<script>document.domain='<?php echo $db->realtime_config('document_domain'); ?>';</script>
<script src="js/htmlentities.js"></script>
<script src="js/json2.js"></script>
<script src="js/jquery-1.3.2.min.js"></script>
<script src="js/jquery.flot.js"></script>
<script src="js/jquery.jeditable.pack.js"></script>
<script src="js/jquery-ui-1.7.2/ui/ui.core.js"></script>
<script src="js/jquery-ui-1.7.2/ui/ui.sortable.js"></script>
<script src="js/jquery-ui-1.7.2/ui/ui.slider.js"></script>
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
<script src="js/reconui.js" type="text/javascript"></script>
<script type="text/javascript">
<!--
recon_realtime_hostname = '<?php echo $db->realtime_config('hostname'); ?>';
-->
</script>
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

</head>

<body style="background:none;">

<div id="streambox" style="display:none"></div>	
<div id="drawing_board"></div>

<script type="text/javascript">

var otype = <?php echo "\"$otype\"";?>;
var id = <?php echo "\"$id\"";?>;

if(id) {
       var start = <?php echo "\"$start\"";?>;
       if(start==parseInt(start)) {
       	  start = new Date(parseInt(start));
	  start = start.toUTCString();
       }
       else start=time_window_to_seconds('2w');

       var end = <?php echo "\"$end\"";?>;
       if(end == parseInt(end)) {
	  end = new Date(parseInt(end));
	  end = end.toUTCString();
       }
       else end = "";
       var gran = <?php echo "\"$gran\"";?>;

       if(otype == 'graph') {
	  $('#drawing_board').width('780px');
	  $('#drawing_board').height('400px');
	  worksheet.render_graph_inpage('drawing_board', id, start, end, gran);
       }
       else if(otype == 'wsheet') {
     	  $('#drawing_board').width('1200px');
	  $('#drawing_board').height('800px');
	  worksheet.render_ws_inpage('drawing_board', id, start, end, gran);
       }
}
</script>

</body>
</html>
