<?php

require_once('Reconnoiter_DB.php');

$json = stripslashes($_POST['json']);
$graph = json_decode($json, true);
$saved_id = $graph['id'];
$db = Reconnoiter_DB::getDB();
try {
  $id = $db->saveGraph($graph);
  print json_encode(array('id' => $id));
}
catch(Exception $e) {
  print json_encode(array('id' => $saved_id, 'error' => $e->getMessage()));
}
?>
