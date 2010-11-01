<?php

require_once('Reconnoiter_DB.php');

$db = Reconnoiter_DB::getDB();

$force = $_GET['force'];
$graphs = $db->getGraphsByWorksheet($_GET['id']);

header('Content-Type: application/json; charset=utf-8');

if($graphs && !$force) {
  print json_encode(array('refed' => $graphs));
}

else if($force || !$graphs) {
  try {
    $db->deleteWorksheet($_GET['id']);
    print json_encode(array());
  }
  catch(Exception $e) {
    print json_encode(array('error' => $e->getMessage()));
  }
}
