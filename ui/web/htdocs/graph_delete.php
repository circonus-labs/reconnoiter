<?php

require_once('Reconnoiter_DB.php');

$db = Reconnoiter_DB::getDB();

$force = $_GET['force'];
$wsheets = $db->getWorksheetsByGraph($_GET['id']);

if($wsheets && !$force) {
  print json_encode(array('refed' => $wsheets));
}

else if($force || !$wsheets) {
  try {
    $db->deleteGraph($_GET['id']);
    print json_encode(array());
  }
  catch(Exception $e) {
    print json_encode(array('error' => $e->getMessage()));
  }
}
