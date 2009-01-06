<?php

require_once('Reconnoiter_DB.php');

$db = Reconnoiter_DB::getDB();

try {
  $db->deleteWorksheetGraph($_GET['wid'], $_GET['gid']);
  print json_encode(array());
}
catch(Exception $e) {
  print json_encode(array('error' => $e->getMessage()));
}
