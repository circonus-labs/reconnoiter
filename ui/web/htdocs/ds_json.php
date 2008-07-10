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
                   'text'        => $item[$want],
                   'classes'     => $want,
                   'hasChildren' => $item['cnt'] > 1 ? true : false,
                   'params'      => $params,
                   );
    if($item['cnt'] == 1) {
      $jitem['text'] = '<a href="generic_graph.php?metric=nl-' . $item['sid'] . '-' . $item['metric_name'] . '&cnt=1400">' . $item[$want] . '</a>';
      $jitem['classes'] = 'metric numeric';
    }
    $bag[] = $jitem;
}

  echo json_encode($bag);
if(false) {

  $metrics = array();

  foreach ($db->get_checks($_REQUEST['target']) as $name => $check){ 

    foreach ($check['numeric'] as $n){

      $metrics[] = array('id'      => 'metric-'.$check['id'].'-'.$n,
			 'classes' => 'numeric metric',
			 'text'    => '<a href="generic_graph.php?metric=nl-'.$check['id'].'-'.$n.'&cnt=1400">'.$n.'</a>',
			 );
    }

    foreach($check['text'] as $n) {

      $metrics[] = array('id'      => 'metric-'.$check['id'].'-'.$n,
			 'classes' => 'text metric',
			 'text'    => $n
			 );
    }
  }

  echo json_encode($metrics);
 }
if (false) {

    $checks = array();

    foreach ($db->get_checks($_REQUEST['target']) as $name => $check){ 

      $checks[] = array('id'          => 'check-'.$check['id'],
			'text'        => $name,
			'classes'     => 'check',
			'hasChildren' => true,
			'params'      => array('target' => $_REQUEST['target'], 
					       'check'  => $check['id']
					       )
			); 
    }

    echo json_encode($checks);
    /* Returns JSON like:
     [
      {"id":"check-64a20e94-38e2-4484-bf74-97061f954728",
       "text":"ping_icmp(420)",
       "classes":"check",
       "hasChildren":true,
       "params":{"target":"10.0.0.108","check":"64a20e94-38e2-4484-bf74-97061f954728"}
      },
      {"id":"check-c9042bfe-8a4c-478f-aa5a-4203bc5b03f6",
       "text":"ssh2(928)",
       "classes":"check",
       "hasChildren":true,
       "params":{"target":"10.0.0.108","check":"c9042bfe-8a4c-478f-aa5a-4203bc5b03f6"}
       }
       ...
     ]
     */
}
