<?php
require_once 'Reconnoiter_DB.php';
require_once 'Reconnoiter_GraphTemplate.php';

$templateid = $_POST['templateid'];

$sid_vars = explode(",", $_POST['sidvars']);
$text_vars = explode(",", $_POST['textvars']);

$rparams = array();
$var_vals = array();

foreach ($text_vars as $tv) {
  $rparams[$tv] = $_POST[$tv];
}

foreach ($sid_vars as $sv) {
  $var_vals[] = explode(",", $_POST[$sv]);
}

//this number is used so we dont end up trying to create multiple graphs with the same title		
$graph_num = 1;
   
function createGraphsFromCombos($combo, $var_vals, $i, $sid_vars, $rparams, $templateid, $graph_num)
    {
	
        if ($i >= count($var_vals)){
	    $vals_combo = explode(",", $combo);
	    for ($j=0; $j<count($vals_combo); $j++) {
               $rparams[$sid_vars[$j]] = $vals_combo[$j];
            }
	    $template = new Reconnoiter_GraphTemplate($templateid);
	    $db = Reconnoiter_DB::getDB();
	    $graph_json = $template->newGraph($rparams);
	    $graph_json = stripslashes($graph_json);
	    $graph_json = json_decode($graph_json, true);
	    $graph_json['title'] = $graph_json['title'].$graph_num;
	    $graph_id = $db->saveGraph($graph_json);
	    $graph_json['id'] = $graph_id;
	    $graph_id = $db->saveGraph($graph_json);
	}
        else
        {
            foreach ($var_vals[$i] as $vval){
	        $graph_num++;
	        createGraphsFromCombos(($combo) ? "$combo,$vval" : $vval, $var_vals, $i + 1, $sid_vars, $rparams, $templateid, $graph_num);
           }
        }
    }

createGraphsFromCombos('', $var_vals, 0, $sid_vars, $rparams, $templateid, $graph_num);

?>

