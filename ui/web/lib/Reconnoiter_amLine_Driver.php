<?php

require_once 'Reconnoiter_amCharts_Driver.php';

class Reconnoiter_amLine_Driver extends Reconnoiter_amCharts_Driver {
  function __construct($start, $end, $cnt) {
    parent::__construct($start, $end, $cnt);
  }
  function seriesXML() {
    $i = 0;
    print "<series>\n";
    foreach ($this->series() as $ts) {
      print "<value xid=\"$i\">$ts</value>\n";
      $i++;
    }
    print "</series>\n";
  }
  function graphDataXML($set, $config) {
    $i = 0;
    foreach ($this->series() as $ts) {
      $value = $set->data($ts);
      if($value != "") {
        if(isset($config['expression'])) {
          $value = $this->rpn_eval($value, $config['expression']);
        }
        $desc = $set->description($ts);
        if($desc) {
          print "<value xid=\"$i\" description=\"".htmlentities($desc)."\">".
                htmlentities($value)."</value>\n";
        } else {
          print "<value xid=\"$i\">".htmlentities($value)."</value>\n";
        }
      }
      $i++;
    }
  }
}
