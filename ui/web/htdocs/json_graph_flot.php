<?php

require_once('Reconnoiter_flot_Driver.php');
require_once('Reconnoiter_DB.php');

global $graph_settings;
require_once('graph_settings.inc');

$type = $_GET['type'];

$start = $_GET['start']?$_GET['start']:(7*86400);
if(preg_match('/^\d+$/', $start))
  $start = gmstrftime("%Y-%m-%d %H:%M:%S", time() - $start);
$end = $_GET['end']?$_GET['end']:gmstrftime("%Y-%m-%d %H:%M:%S", time());
$cnt = $_GET['cnt']?$_GET['cnt']:400;

$driver = new Reconnoiter_flot_Driver($start, $end, $cnt, $type);
$db = Reconnoiter_DB::GetDB();
$row = $db->getGraphByID($_GET['id']);
$graph = json_decode($row['json'], true);

foreach($graph['datapoints'] as $k => $d) {
  if($d['metric_type'] == 'guide') {
  	if (preg_match('/\d+\%?/', $d['math2'])) {
  	  // if the math field cotains just numbers or numbers with % sign, we want percentile
      $driver->calcPercentile($d['math2']);
      $guideType[$k]='percentile';
  	} else {
  		// if it is a string, see if we can recognize some aggregate aliases
  		switch ($d['math2']) {
  			case 'med':
  			case 'median':
  				$driver->calcPercentile(50);
  				$guideType[$k]='percentile';
  				$graph['datapoints'][$k]['math2']='50';
  				break;
  			case 'min':
  			case 'minimum':
  				$driver->calcPercentile(0);
  				$guideType[$k]='percentile';
  				$graph['datapoints'][$k]['math2']='0';
  				break;
  			case 'max':
  			case 'maximum':
  				$driver->calcPercentile(100);
  				$guideType[$k]='percentile';
  				$graph['datapoints'][$k]['math2']='100';
  				break;
  			case 'average':
  				$driver->calcAggregate('avg');
  				$guideType[$k]='aggregate';
  				$graph['datapoints'][$k]['math2']='avg';
  				break;
  			default:
  				// everything else is passed to aggregate calculator
  				$driver->calcAggregate($d['math2']);
  				$guideType[$k]='aggregate';
  		}
  	}
  }
}

$i = 0;
$autounits = 0;
foreach($graph['datapoints'] as $k => $d) {
  if (!isset($d['math1'])) $d['math1']='';
  if (!isset($d['math2'])) $d['math2']='';
  if($d['metric_type'] == 'guide') {
    $color = isset($d['color']) ? $d['color'] : '#ff0000';
    
    if ($guideType[$k]=='percentile') {
    $driver->addPercentileGuide($d['name'], $d['math2'],
                                array('expression' => $d['math1'],
                                      'color' => $color));
    } else {
    $driver->addAggregateGuide($d['name'], $d['math2'],
                                array('expression' => $d['math1'],
                                      'color' => $color));
    }
  }
  else if($d['metric_type'] == 'composite') {
     $color = isset($d['color']) ? $d['color'] : '#ff0000';
     $driver->addCompositeSet($d['name'],$d['math2'],
				 array('expression' => $d['math1'],
                                      'color' => $color));
  }
  else {
    $settings = $graph_settings[$i++];
    $settings['color'] = isset($d['color']) ? $d['color'] : $settings['color'];
    if($d['hidden'] == "true") $settings['hidden'] = "true";
    if(!empty($d['math1'])) $settings['expression'] = $d['math1'];
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
  'legend' => array ( 'noColumns' => 4, 'position' => 'sw' ),
  'selection' => array ( 'mode' => 'x' ),
  'shadowSize' => 0,
  'colors' => $driver->graphcolors()
);

header('Content-Type: application/json; charset=utf-8');

print json_encode(array(
  'data' => $data,
  'options' => $options,
  'title' => $graph['title'] . '',
));

