<?php

require_once('Reconnoiter_flot_Driver.php');
global $graph_settings;
require_once('graph_settings.inc');

$uuid = $_GET['id']; //'cfe2aad7-71e5-400b-8418-a6d5834a0386';
$start = $_GET['start'];
$view = $_GET['view'];
if(!$start) {
  $start = strftime("%Y-%m-%d %H:%M:%S", time() - (7*86400));
}
$end = $_GET['end'];
if(!$end) {
  $end = strftime("%Y-%m-%d %H:%M:%S", time());
}

$driver = new Reconnoiter_flot_Driver($start, $end,
                                      isset($_GET['cnt']) ? $_GET['cnt'] : 400);
$i = 0;
$math = 'auto,2,round';
if($view == "packets") {
  $settings = $graph_settings[$i++];
  $settings['expression'] = "$math,-1,*";
  $driver->addDataSet($uuid, 'inucastpkts', true, null, $settings);
  $settings = $graph_settings[$i++];
  $settings['expression'] = "$math";
  $driver->addDataSet($uuid, 'outucastpkts', true, null, $settings);
} else {
  $settings = $graph_settings[$i++];
  $settings['expression'] = "$math,-1,*";
  $driver->addDataSet($uuid, 'inoctets', true, '8,*', $settings);
  $settings = $graph_settings[$i++];
  $settings['expression'] = "$math";
  $driver->addDataSet($uuid, 'outoctets', true, '8,*', $settings);
  $driver->addChangeSet($uuid, 'alias');
  if($_GET['type'] != 'small') {
    $driver->calcPercentile(95);
    $driver->addPercentileGuide('min', 0, array('expression' => "$math"));
    $driver->addPercentileGuide('max', 100, array('expression' => "$math"));
    $driver->addPercentileGuide('95th', 95, array('expression' => "$math"));
  }
}

$i = 0;

$data = $driver->graphdata();
$options = array(
  'xaxis' => array ( 'mode' => 'time' ),
  'legend' => array ( 'noColumns' => 1 ),
  'selection' => array ( 'mode' => 'x' ),
  'shadowSize' => 0,
);
print json_encode(array('data' => $data, 'options' => $options, 'title' => $driver->title()));
?>
