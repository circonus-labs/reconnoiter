<?xml version="1.0" encoding="UTF-8"?>
<?php

require_once('Reconnoiter_amLine_Driver.php');

$uuid = $_GET['id']; //'cfe2aad7-71e5-400b-8418-a6d5834a0386';
$math = '$this->bw($value)';
$start = $_GET['start'];
if(!$start) {
  $start = strftime("%Y-%m-%d %H:%M:%S", time() - (7*86400));
}
$end = $_GET['end'];
if(!$end) {
  $end = strftime("%Y-%m-%d %H:%M:%S", time());
}

$driver = new Reconnoiter_amLine_Driver($start, $end, isset($_GET['cnt']) ? $_GET['cnt'] : 400);
$driver->addDataSet($uuid, 'inoctets', true, '$value * 8', array('expression' => "0 - $math"));
$driver->addDataSet($uuid, 'outoctets', true, '$value * 8', array('expression' => "$math"));
$driver->addChangeSet($uuid, 'alias');
#$driver->calcPercentile(95);
#$driver->addPercentileGuide('min', 0, array('expression' => "$math"));
#$driver->addPercentileGuide('95th', 95, array('expression' => "$math"));
#$driver->addPercentileGuide('max', 100, array('expression' => "$math"));

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
<values>
 <x><enabled>false</enabled></x>
 <y_left>
  <frequency>2</frequency>
  <unit><?php print $driver->autounit() ?></unit>
 </y_left>
</values>
<plot_area>
  <margins>
    <left>45</left>
    <right>0</right>
    <top>15</top>
    <bottom>5</bottom>
  </margins>
</plot_area>

<labels>
  <label>
    <x>!200</x>
    <y>0</y>
    <rotate></rotate>
    <width>200</width>
    <align>right</align>
    <text_color></text_color>
    <text_size></text_size>
    <text>
      <![CDATA[<?php print $driver->title() ?>]]>
    </text>        
  </label>    
</labels>

</settings>
