<?xml version="1.0" encoding="UTF-8"?>
<?php

require_once('Reconnoiter_amLine_Driver.php');
global $graph_settings;
require_once('graph_settings.inc');

$uuid = $_GET['id']; //'cfe2aad7-71e5-400b-8418-a6d5834a0386';
$math = '$this->bw($value)';
$start = $_GET['start'];
$view = $_GET['view'];
if(!$start) {
  $start = strftime("%Y-%m-%d %H:%M:%S", time() - (7*86400));
}
$end = $_GET['end'];
if(!$end) {
  $end = strftime("%Y-%m-%d %H:%M:%S", time());
}

$driver = new Reconnoiter_amLine_Driver($start, $end,
                                        isset($_GET['cnt']) ? $_GET['cnt'] : 400);
$i = 0;
$settings = $graph_settings[$i++];
$settings['expression'] = "0 - $math";
$settings['hidden'] = ($view != "packets") ? "false" : "true";
$driver->addDataSet($uuid, 'inoctets', true, '$value * 8', $settings);
$settings = $graph_settings[$i++];
$settings['expression'] = "$math";
$settings['hidden'] = ($view != "packets") ? "false" : "true";
$driver->addDataSet($uuid, 'outoctets', true, '$value * 8', $settings);
$settings = $graph_settings[$i++];
$settings['expression'] = '0 - $value';
$settings['axis'] = "right";
$settings['hidden'] = ($view == "packets") ? "false" : "true";
$driver->addDataSet($uuid, 'inucastpkts', true, '$value', $settings);
$settings = $graph_settings[$i++];
$settings['expression'] = '$value';
$settings['axis'] = "right";
$settings['hidden'] = ($view == "packets") ? "false" : "true";
$driver->addDataSet($uuid, 'outucastpkts', true, '$value', $settings);
$driver->addChangeSet($uuid, 'alias');
$driver->calcPercentile(95);
$driver->addPercentileGuide('min', 0, array('expression' => "$math"));
$driver->addPercentileGuide('95th', 95, array('expression' => "$math"));
$driver->addPercentileGuide('max', 100, array('expression' => "$math"));

$i = 0;
?>
<settings>
<type></type>
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
  <unit><?php print $driver->autounit() ?></unit>
 </y_left>
</values>
</settings>
