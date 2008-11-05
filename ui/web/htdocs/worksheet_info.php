<?php

require_once('Reconnoiter_DB.php');

$db = Reconnoiter_DB::GetDB();

try {
  $results = $db->getWorksheetByID($_GET['id']);
  print json_encode($results);
}
catch(Exception $e) {
  print json_encode(array(
    'error' => $e->getMessage()
  ));
}
