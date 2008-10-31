<?php

require_once('Reconnoiter_flot_Driver.php');
require_once('Reconnoiter_DB.php');

global $graph_settings;
require_once('graph_settings.inc');

$start = $_GET['start']?$_GET['start']:(7*86400);
if(preg_match('/^\d+$/', $start))
  $start = strftime("%Y-%m-%d %H:%M:%S", time() - $start);
$end = $_GET['end']?$_GET['end']:strftime("%Y-%m-%d %H:%M:%S", time());
$cnt = $_GET['cnt']?$_GET['cnt']:400;

$driver = new Reconnoiter_flot_Driver($start, $end, $cnt);
$db = Reconnoiter_DB::GetDB();
$row = $db->getGraphByID($_GET['id']);
$graph = json_decode($row['json'], true);

foreach($graph['datapoints'] as $d) {
  if($d['metric_type'] == 'guide') {
    $driver->calcPercentile($d['math2']);
  }
}

$i = 0;
$autounits = 0;
foreach($graph['datapoints'] as $d) {
  if($d['metric_type'] == 'guide') {
    $color = isset($d['color']) ? $d['color'] : '#ff0000';
    $driver->addPercentileGuide($d['name'], $d['math2'],
                                array('expression' => $d['math1'],
                                      'color' => $color));
  }
  else {
    $settings = $graph_settings[$i++];
    $settings['color'] = isset($d['color']) ? $d['color'] : $settings['color'];
    if($d['hidden'] == "true") $settings['hidden'] = "true";
    if($d['math1']) $settings['expression'] = $d['math1'];
    $settings['axis'] = ($d['axis'] == 'l') ? 'left' : 'right';
    $settings['title'] = $d['name'];
    if($d['metric_type'] == 'numeric') {
      $driver->addDataSet($d['sid'], $d['metric_name'],
                          $d['derive'], $d['math2'], $settings);
    }
    else
      $driver->addChangeSet($d['sid'], $d['metric_name'], $settings);
  }
}

$data = $driver->graphdata();
$yaxis = array();
if($driver->min() > 0) $yaxis['min'] = 0;
if($driver->max() < 0) $yaxis['max'] = 0;

$options = array(
  'xaxis' => array ( 'mode' => 'time' ),
  'yaxis' => array ( 'suffix' => $driver->autounit() . '' ),
  'legend' => array ( 'noColumns' => 4, position => 'sw' ),
  'selection' => array ( 'mode' => 'x' ),
  'shadowSize' => 0,
  'colors' => $driver->graphcolors()
);
print json_encode(array(
  'data' => $data,
  'options' => $options,
  'title' => $graph['title'] . '',
));

