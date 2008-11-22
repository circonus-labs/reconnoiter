<?php 

require_once 'Reconnoiter_DB.php';
require_once 'Reconnoiter_GraphTemplate.php';

$db = Reconnoiter_DB::getDB();

//these are the names of the parameters , one of templateid, targetname, or sid
$l1 = $_GET['l1'];
$l2 = $_GET['l2'];
$l3 = $_GET['l3'];

error_log("------");
error_log("parameters sent to template browse:");
error_log("rootthing = ".$_REQUEST['root']);
error_log($l1."=".$_REQUEST[$l1]);
error_log($l2."=".$_REQUEST[$l2]);	  
error_log($l3."=".$_REQUEST[$l3]);	  

if ($_REQUEST['root'] == 'source'){
  $want = $l1;
}
else if($_REQUEST[$l1]) {
  $want = $l2;
}
else if($_REQUEST[$l2]) {
     $want = $l3;
}
else {
     $want = "we are screwed";
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

else if($want == 'targetname') {         
         $templateid = $_REQUEST[$l1];
	 error_log("retreive template = $templateid");
   	 $template = new Reconnoiter_GraphTemplate($templateid);

	 $target_sid_map = array();

	 foreach ($template->sids() as $item) {	
	    foreach ($item as $match) {
	    	    if(!isset($target_sid_map[$match[target]])) {
		    	$target_sid_map[$match[target]] = array();
		    }
		       $target_sid_map[$match[target]][] = $match[sid];
	    }
         }
	 
	 foreach ($target_sid_map as $target_name => $sid_list) {
	    $sidlist = implode(",", $sid_list);
	    $jitem = array ('id' => $target_name,
	    	     	   'text' => $target_name,
			   'hasChildren' => true,
			   'classes' => $want,
			   'params' => array('targetname' => $sidlist)
			   );
	    $bag[] = $jitem;
	 }
}
else if($want == 'sid') {
     $sid_list = explode (",", $_REQUEST[$l2]);
     
     foreach($sid_list as $sid) {
     $sname = $db->get_sid_name($sid);
     error_log("got $name as the sid name");
     $jitem = array ('id' => $sname,
	    	     	   'text' => $sname,
			   'hasChildren' => false,
			   'classes' => $want,
			   'params' => array(),
			   );
	    $bag[] = $jitem;
      }

}
			   

	 
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

echo json_encode($bag);
