<?php

require_once('Reconnoiter_DB.php');

$db = Reconnoiter_DB::getDB();

header('Content-Type: application/json; charset=utf-8');

try {
  $db->deleteWorksheetGraph($_GET['wid'], $_GET['gid']);
  print json_encode(array());
}
catch(Exception $e) {
  print json_encode(array('error' => $e->getMessage()));
}
