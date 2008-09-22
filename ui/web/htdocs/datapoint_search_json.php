<?php

require_once('Reconnoiter_DB.php');

$db = Reconnoiter_DB::GetDB();
$rows = $db->get_datapoints($_GET['q'], $_GET['o'], $_GET['l']);
print json_encode(array(
  'query' => $_GET['q'],
  'offset' => $_GET['o'],
  'limit' => $_GET['l'],
  'results' => $rows
));
