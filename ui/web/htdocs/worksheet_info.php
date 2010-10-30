<?php

require_once('Reconnoiter_DB.php');

$db = Reconnoiter_DB::GetDB();

header('Content-Type: application/json; charset=utf-8');

try {
  $results = $db->getWorksheetByID($_GET['id']);
  print json_encode($results);
}
catch(Exception $e) {
  print json_encode(array(
    'error' => $e->getMessage()
  ));
}
