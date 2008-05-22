<?xml version="1.0" encoding="UTF-8"?>
<?php

require_once('Reconnoiter_amLine_Driver.php');
global $graph_settings;
require_once('graph_settings.inc');

$start = $_GET['start'];
if(!$start) {
  $start = strftime("%Y-%m-%d %H:%M:%S-00", time() - (7*86400));
}
$end = $_GET['end'];
if(!$end) {
  $end = strftime("%Y-%m-%d %H:%M:%S-00", time());
}

$driver = new Reconnoiter_amLine_Driver($start, $end, isset($_GET['cnt']) ? $_GET['cnt'] : 400);

$i = 0;
foreach(split(";", $_GET['metric']) as $m) {
  preg_match('/^(d|n|t)(l|r)(~|-)([0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12})-(.*)$/', $m,
             $matches);
  $settings = $graph_settings[$i++];
  if($matches[3] == '~') $settings['expression'] = '$this->bw($value)';
  $settings['axis'] = ($matches[2] == 'l') ? 'left' : 'right';
  if($matches[1] == 'n')
    $driver->addDataSet($matches[4], $matches[5], 'false', null, $settings);
  else if($matches[1] == 'd')
    $driver->addDataSet($matches[4], $matches[5], 'true', null, $settings);
  else
    $driver->addChangeSet($matches[4], $matches[5]);
}

$i = 0;
?>
<settings>
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
 </y_left>
</values>
</settings>
