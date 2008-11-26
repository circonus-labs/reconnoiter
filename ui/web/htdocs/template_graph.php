<?php
require_once 'Reconnoiter_DB.php';
require_once 'Reconnoiter_GraphTemplate.php';

$db = Reconnoiter_DB::getDB();

$templateid = $_POST['templateid'];
$graph_name = $_POST['graph_name'];
$sid_list = explode(", ", $_POST['sids']);

$template = new Reconnoiter_GraphTemplate($templateid);

$rparams = array('SwitchName' => $graph_name);

$i = 1;

foreach ($sid_list as $term){
   $sname = ($i==1) ? "SwitchPort": "SwitchPort".$i;
   $sid = explode("`", $term); $sid = $sid[3];
   $rparams[$sname] = $sid;
   $i++;
}

$graph_json = $template->newGraph($rparams);

$graph_json = stripslashes($graph_json);
$graph_json = json_decode($graph_json, true);
$graph_id = $db->saveGraph($graph_json);

$graph_json['id'] = $graph_id;
$graph_id = $db->saveGraph($graph_json);

?>
