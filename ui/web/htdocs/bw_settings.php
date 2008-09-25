<?xml version="1.0" encoding="UTF-8"?>
<?php

require_once('Reconnoiter_amLine_Driver.php');
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

$driver = new Reconnoiter_amLine_Driver($start, $end,
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
?>
<settings>
<type></type>
<?php if($_GET['type'] == 'small') { ?>
  <legend><enabled>false</enabled></legend>
  <grid><x><enabled>false</enabled></x></grid>
  <plot_area>
    <margins><top>20</top><right>50</right><bottom>30</bottom><left>50</left></margins>
  </plot_area>
<?php } ?>
<data>
<chart>
  <?php $driver->seriesXML() ?>
  <?php $driver->graphsXML() ?>
  <?php $driver->guidesXML() ?>
</chart>
</data>
<labels>
  <label>
    <x>0</x>
    <y>0</y>
    <align>center</align>
    <text_size><?php print ($_GET['type'] == 'small') ? 12 : 16 ?></text_size>
    <text_color>#000000</text_color>
    <text><![CDATA[<?php print $driver->title() ?>]]></text>
  </label>
</labels>
<decimals_separator>.</decimals_separator>
<values>
 <y_left>
  <unit><?php print $driver->autounit() ?></unit>
 </y_left>
<?php if($_GET['type'] == 'small') { ?>
  <x><enabled>false</enabled></x>
<?php } ?>
</values>
</settings>
