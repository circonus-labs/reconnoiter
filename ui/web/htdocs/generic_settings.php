<?xml version="1.0" encoding="UTF-8"?>
<?php

require_once('Reconnoiter_amLine_Driver.php');

$start = $_GET['start'];
if(!$start) {
  $start = strftime("%Y-%m-%d %H:%M:%S", time() - (7*86400));
}
$end = $_GET['end'];
if(!$end) {
  $end = strftime("%Y-%m-%d %H:%M:%S", time());
}

$driver = new Reconnoiter_amLine_Driver($start, $end, isset($_GET['cnt']) ? $_GET['cnt'] : 400);

foreach(split(",", $_GET['metric']) as $m) {
  preg_match('/^(n|t)-([0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12})-(.*)$/', $m,
             $matches);
  if($matches[1] == 'n')
    $driver->addDataSet($matches[2], $matches[3], 'false', null, array('expression' => '$this->bw($value)'));
  else
    $driver->addChangeSet($matches[2], $matches[3]);
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
  <unit><?php print $driver->autounit() ?></unit>
 </y_left>
</values>
</settings>
