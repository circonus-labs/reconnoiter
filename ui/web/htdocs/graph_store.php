<?php

require_once('Reconnoiter_DB.php');

$graph = json_decode($_POST['json'], true);
$saved_id = $graph['id'];
$db = Reconnoiter_DB::getDB();

header('Content-Type: application/json; charset=utf-8');

try {
  $id = $db->saveGraph($graph);
  print json_encode(array('id' => $id));
}
catch(Exception $e) {
  print json_encode(array('id' => $saved_id, 'error' => $e->getMessage()));
}
?>
