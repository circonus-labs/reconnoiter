<?php
require_once 'Reconnoiter_DB.php';
require_once 'Reconnoiter_GraphTemplate.php';

$templateid = $_POST['templateid'];
$update = $_POST['update'];

$sid_vars = explode(",", $_POST['sidvars']);
$text_vars = explode(",", $_POST['textvars']);

$rparams = array();
$var_vals = array();

$genesis_base = "templateid=".$templateid;

foreach ($text_vars as $tv) {
  $rparams[$tv] = $_POST[$tv];
  $genesis_base.=$tv."=".$_POST[$tv];
}

foreach ($sid_vars as $sv) {
  $var_vals[] = explode(",", $_POST[$sv]);
}

$graphs_to_update = array();

//this number is used so we dont end up trying to create multiple graphs with the same title		
$graph_num = 1;

//this function will create each combination of values for each SID placeholder, against the TEXT placeholders set
//then, it will create graphs for each of these combinations, updating a graph if one already exists with the same genesis
//a graph's genesis is composed of its templateid, its text vars and their values, and its sid vars and their values, and
//should be unique among saved_graphs   
function createGraphsFromCombos($combo, $var_vals, $i, $sid_vars, $genesis_base, $rparams, $templateid, $graph_num, $update)
    {
        global $graphs_to_update;

	$genesis = $genesis_base;
        if ($i >= count($var_vals)){	    
	    $vals_combo = explode(",", $combo);
	    for ($j=0; $j<count($vals_combo); $j++) {
               $rparams[$sid_vars[$j]] = $vals_combo[$j];
	       $genesis.=",".$sid_vars[$j]."=".$vals_combo[$j];
            }

	    $template = new Reconnoiter_GraphTemplate($templateid);
	    $db = Reconnoiter_DB::getDB();
	    $graph_json = $template->getNewGraphJSON($rparams);
	    $graph_json = stripslashes($graph_json);
	    $graph_json = json_decode($graph_json, true);
	    $graph_json['title'] = $graph_json['title'].$graph_num;
	    $graph_json['genesis'] = $genesis;

	    $grow = $db->getGraphByGenesis($genesis);
	    if($grow['graphid']) {
	       if($update) {
	         $graph_json['id'] = $grow['graphid'];
	         $graph_id = $db->saveGraph($graph_json);
               }
	       $graphs_to_update[] = array( 'graphid' => $grow['graphid'],
	        		     	    	          'title' => $grow['title'],);
	       return;
	    }
	    if($update) {
	      $graph_id = $db->saveGraph($graph_json);
	      $graph_json['id'] = $graph_id;
	      $graph_id = $db->saveGraph($graph_json);
            }
	    return;
	}
        else
        {
            foreach ($var_vals[$i] as $vval){
	        $graph_num++;
	        createGraphsFromCombos(($combo) ? "$combo,$vval" : $vval, $var_vals, $i + 1, $sid_vars, $genesis_base, $rparams, $templateid, $graph_num, $update);
           }
        }
    }

createGraphsFromCombos('', $var_vals, 0, $sid_vars, $genesis_base, $rparams, $templateid, $graph_num, $update);

print json_encode($graphs_to_update);
?>

