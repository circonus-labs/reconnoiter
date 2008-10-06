<?php

require_once('Reconnoiter_DB.php');

$db = Reconnoiter_DB::GetDB();

try {
  $results = $db->get_graphs($_GET['q'],
                             $_GET['o'] + 0,
                             isset($_GET['l']) ? $_GET['l'] : 100);
  $a = array();
  foreach($results['results'] as $k => $v) {
    $j = json_decode($v['json'], true);
    $j['graphid'] = $v['graphid'];
    $a[] = $j;
  }
  print json_encode(array('graphs' => $a));
}
catch(Exception $e) {
  print json_encode(array(
    'count' => 0,
    'query' => $_GET['q'],
    'offset' => $_GET['o'],
    'limit' => $_GET['l'],
    'error' => $e->getMessage()
  ));
}
