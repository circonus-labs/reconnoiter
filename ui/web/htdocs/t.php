<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
   <head>

   <meta http-equiv="content-type" content="text/html; charset=iso-8859-1"/>
   <title>XHR Reconnoiter</title>
   
   <link rel="stylesheet" href="jquery.treeview.css" />

   <script src="http://ajax.googleapis.com/ajax/libs/jquery/1.2.6/jquery.min.js" type="text/javascript"></script>	   
   <script src="js/jquery.cookie.js" type="text/javascript"></script>
   <script src="js/jquery.treeview.js" type="text/javascript"></script>
   <script src="js/jquery.treeview.async.js" type="text/javascript"></script>
	
   <script type="text/javascript">
	jQuery(document).ready(function(){
	    $("#targets").treeview({
	      url: "json/ds/module/remote_address/target/name/metric_name",
	      params: {} // will become hash, indexed by id, of url params
	      })
	});
    </script>
	
  </head>
  <body>

    <h4>Lazy-loading targets tree</h4>	
        <ul id="targets" class="targets">
        </ul>
<?php require_once('thumb.inc'); ?>
</body>
</html>
