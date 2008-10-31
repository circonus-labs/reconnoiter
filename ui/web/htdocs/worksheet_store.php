<?php

require_once('Reconnoiter_DB.php');

$json = stripslashes($_POST['json']);
$worksheet = json_decode($json, true);
$saved_id = $worksheet['id'];
$db = Reconnoiter_DB::getDB();
try {
  $id = $db->saveWorksheet($worksheet);
  print json_encode(array('id' => $id));
}
catch(Exception $e) {
  print json_encode(array('id' => $saved_id, 'error' => $e->getMessage()));
}
?>

