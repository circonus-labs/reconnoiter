<?xml version="1.0" encoding="UTF-8"?>
<?php

require_once('Reconnoiter_amLine_Driver.php');
require_once('Reconnoiter_DB.php');

global $graph_settings;
require_once('graph_settings.inc');

$start = $_GET['start']?$_GET['start']:(7*86400);
if(preg_match('/^\d+$/', $start))
  $start = strftime("%Y-%m-%d %H:%M:%S", time() - $start);
$end = $_GET['end']?$_GET['end']:strftime("%Y-%m-%d %H:%M:%S", time());
$cnt = $_GET['cnt']?$_GET['cnt']:100;

$driver = new Reconnoiter_amLine_Driver($start, $end, $cnt);
$db = Reconnoiter_DB::GetDB();
$row = $db->getGraphByID($_GET['id']);
$graph = json_decode($row['json'], true);

$i = 0;
$autounits = 0;
foreach($graph['datapoints'] as $d) {
  $settings = $graph_settings[$i++];
  if($d['hidden'] == "true") $settings['hidden'] = "true";
  if($d['math1']) $settings['expression'] = $d['math1'];
  $settings['axis'] = ($d['axis'] == 'l') ? 'left' : 'right';
  $settings['title'] = $d['name'];
  if($d['metric_type'] == 'numeric') {
    $driver->addDataSet($d['sid'], $d['metric_name'],
                        ($d['derive']=="true")?"true":"false",
                        $d['math2'], $settings);
  }
  else
    $driver->addChangeSet($d['sid'], $d['metric_name']);
}

$i = 0;
?>
<settings>
<type><?php print ($graph['type'] == "stacked") ? "stacked" : "line" ?></type>
<data>
<chart>
  <?php $driver->seriesXML() ?>
  <?php $driver->graphsXML() ?>
  <?php $driver->guidesXML() ?>
</chart>
</data>
<decimals_separator>.</decimals_separator>
<values>
 <y_left>
  <min><?php print ($driver->min() > 0) ? '0' : '' ?></min>
  <max><?php print ($driver->max() < 0) ? '0' : '' ?></max>
  <unit><?php print $autounits ? $driver->autounit() : "" ?></unit>
 </y_left>
</values>
</settings>
