<?php

require_once 'Reconnoiter_DB.php';
$db = Reconnoiter_DB::getDB();

// jquery.treeview.async.js currently uses root=source for the initial page load
//   will alter later to whatever key/value pair we choose
$l1 = $_GET['l1'];
$l2 = $_GET['l2'];
$l3 = $_GET['l3'];
$l4 = $_GET['l4'];
$l5 = $_GET['l5'];

if ($_REQUEST['root'] == 'source'){
  $want = $l1;
}
else if($_REQUEST[$l1]) {
  $want = $l2;
  if($_REQUEST[$l2]) {
    $want = $l3;
    if($_REQUEST[$l3]) {
      $want = $l4;
      if($_REQUEST[$l4]) {
        $want = $l5;
        if($_REQUEST[$l5])
          $want = '';
      }
    }
  }
}
else {
  $want = '';
}

$bag = array();

foreach ($db->get_sources($want, $_GET) as $item){ 
    $params = array();
    foreach ( $db->valid_source_variables() as $var ) {
      if(isset($item[$var])) $params[$var] = $item[$var];
    }
    $jitem = array('id'          => $item['id'],
                   'text'        => $item['ptr'] ?
                                      $item[$want] . "(" . $item['ptr'] . ")" :
                                      $item[$want],
                   'classes'     => $want,
                   'hasChildren' => $item['unique'] ? false : true,
                   'params'      => $params,
                   );
    if($item['unique']) {
      $jitem['text'] = '<a href="javascript:graph_add_datapoint({\'id\' : \'' . $item['id'] . '\', \'sid\' : ' . $item['sid'] . ', \'module\' : \'' . $item['module'] . '\', \'target\' : \'' . $item['target'] . '\', \'name\' : \'' . $item['target'] . '`' . $item['metric_name'] . '\', \'metric_name\' : \'' . $item['metric_name'] . '\', \'metric_type\' : \'' . $item['metric_type'] . '\'})">' . $item[$want] . '</a>';
      if($item['metric_type'] == "numeric") {
        $jitem['classes'] = 'metric numeric';
      }
      else {
        $jitem['classes'] = 'metric text';
      }
    }
    $bag[] = $jitem;
}

echo json_encode($bag);
