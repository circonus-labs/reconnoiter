<?php

require_once 'Reconnoiter_DataContainer.php';

class Reconnoiter_flot_Driver extends Reconnoiter_DataContainer {
  function __construct($start, $end, $cnt) {
    parent::__construct($start, $end, $cnt);
  }
  function defaultDataSetAttrs($uuid, $name, $derive, $attrs) {
    return parent::defaultDataSetAttrs($uuid, $name, $derive, $attrs);
  }
  function defaultChangeSetAttrs($uuid, $name, $derive, $attrs) {
    return parent::defaultChangeSetAttrs($uuid, $name, $derive, $attrs);
  }
  function graphdata() {
    $a = array();
    $i = 0;
    foreach($this->sets as $name => $set) {
      $a[] = array (
        'label' => $this->sets_config[$name]['title'] ? $this->sets_config[$name]['title'] : $name,
        'data' => $this->graphdataset($set, $this->sets_config[$name]),
        'yaxis' => ($this->sets_config[$name]['axis'] == 'right') ? 2 : 1,
        'lines' => array ( 'show' => 'true', 'fill' => '0.8', 'lineWidth' => '1' )
      );
    }
    $start_ts = $a[0]['data'][0][0];
    $finish = end($a[0]['data']);
    $finish_ts = $finish[0];
    foreach($this->guides as $name => $value) {
      if(isset($this->guides_config[$name]['expression'])) {
        $value = $this->rpn_eval($value, $this->guides_config[$name]['expression']);
      }
      $a[] = array (
        'label' => $name,
        'data' => array(array($start_ts, "$value"), array($finish_ts, "$value")),
        'yaxis' => 1,
        'lines' => array ( 'color' => '3', 'show' => true, 'fill' => false, 'lineWidth' => '1' )
      );
    }
    return $a;
  }
  function graphdataset($set, $config) {
    $i = 0;
    $a = array();
    $prev_value = '0';
    foreach ($this->series() as $ts) {
      $value = $set->data($ts);
      if($value != "") {
        if(isset($config['expression'])) {
          $value = $this->rpn_eval($value, $config['expression']);
        }
        $desc = $set->description($ts);
        if($desc) {
          $a[] = array( $ts * 1000, "$value" );
        } else {
          $a[] = array( $ts * 1000, "$value" );
        }
      }
      else $a[] = array( $ts * 1000, "" );
      $i++;
    }
    return $a;
  }

  function eachGuideXML() {
    foreach($this->guides as $name => $value) {
      print "<guide>\n";
      $this->guideXML($value, $this->guides_config[$name]);
      print "</guide>\n";
    }
  }
  // So far, guides seems to be consistently implemented (or ignored)
  function guideXML($value, $config) {
    if($value != "") {
      if(isset($config['expression'])) {
        $value = $this->rpn_eval($value, $config['expression']);
      }
    }
    print "<start_value>$value</start_value>\n";
    if(isset($config['title'])) {
      $expr = $config['title'];
      eval("\$title = $expr;");
      print "<title>$title</title>\n";
    } else {
      print "<title>$value</title>\n";
    }
    if(isset($config['color'])) {
      $expr = $config['color'];
      eval ("\$color = $expr;");
      print "<color>$color</color>\n";
    } else {
      print "<color>#ff0000</color>\n";
    }
    print "<inside>true</inside>\n";
  }
}
