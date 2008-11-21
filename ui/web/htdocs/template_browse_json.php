<?php 

require_once 'Reconnoiter_DB.php';
require_once 'Reconnoiter_GraphTemplate.php';


$db = Reconnoiter_DB::getDB();

//these are the names of the parameters , one of templateid or metric_name
$l1 = $_GET['l1'];
$l2 = $_GET['l2'];

error_log("------");
error_log("parameters sent to template browse:");
error_log("rootthing = ".$_REQUEST['root']);
error_log($l1."=".$_REQUEST[$l1]);
error_log($l2."=".$_REQUEST[$l2]);	  

if ($_REQUEST['root'] == 'source'){
  $want = $l1;
}
else if($_REQUEST[$l1]) {
  $want = $l2;
}
else {
     $want = 'got screwed';
}

error_log("want = ".$want);
error_log("------");

$bag = array();

if($want == 'templateid') {
	 foreach($db->get_templates() as $item) {
	 $params = array();
	 $params['templateid'] = $item['templateid'];
	 $params['title'] = $item['title'];
	 $params['json'] = $item['json'];	 

	 $jitem = array ('id' => $item['templateid'],
	 	  	'text' => $item['title'], 
			'hasChildren' => true, 
			'classes' => $want, 
			'params' => $params,
			);
	 $bag[] = $jitem;
	 }
}

else if($want == 'metric_name') {         
         $templateid = $_REQUEST[$l1];
	 error_log("retreive template = $templateid");
   	 $template = new Reconnoiter_GraphTemplate($templateid);

	 $select = "<form><select name='sids' multiple>";
	 foreach ($template->sids() as $item) {
	    foreach ($item as $match) {
	    	    $select.="<option value='$match[sid]'>$match[sid]";
		    }
	}
	$select.="</select></form>";

    	   $jitem = array ('id' =>   "34",
	 	  	'text' => $select,
			'classes' => $want,
			'hasChildren' => false,
			'params' => array()
	    );   		
	    $bag[] = $jitem;
	    
	 
/*
	 $rparams = array( 'SwitchPort' => '1130', 'SwitchPort2' => '1139', 'SwitchName' => 'testingmultiplegraphs' );
	 //error_log("rparams: ".print_r($rparams, true));
	 $graph_json = $template->newGraph($rparams);
	 $graph_json = stripslashes($graph_json);
	 $graph_json = json_decode($graph_json, true);
	$graph_id = $db->saveGraph($graph_json);

	$graph_json['id'] = $graph_id;
//        error_log("graph json: ".print_r($graph_json, true));
	$graph_id = $db->saveGraph($graph_json);
	//error_log("saved to graph id = $graph_id");
*/
}

echo json_encode($bag);
