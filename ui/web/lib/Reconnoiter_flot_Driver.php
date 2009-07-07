<?php

require_once 'Reconnoiter_DataContainer.php';

class Reconnoiter_flot_Driver extends Reconnoiter_DataContainer {
  function __construct($start, $end, $cnt, $type) {
    parent::__construct($start, $end, $cnt, $type);
  }
  function defaultDataSetAttrs($uuid, $name, $derive, $attrs) {
    return parent::defaultDataSetAttrs($uuid, $name, $derive, $attrs);
  }
  function defaultChangeSetAttrs($uuid, $name, $derive, $attrs) {
    return parent::defaultChangeSetAttrs($uuid, $name, $derive, $attrs);
  }
  function graphcolors() {
    $c = array();
    foreach($this->sets as $name => $set) {
      $c[] = $this->sets_config[$name]['color'];
    }
    foreach($this->guides as $name => $value) {
      $c[] = $this->guides_config[$name]['color'];
    }
    return $c;
  }
  function graphdata() {
    $a = array();
    $i = 0;
    foreach($this->sets as $name => $set) {
      $m_name = explode("-", $name);      
      $metric_type = '';
      if(get_class($set) == "Reconnoiter_ChangeSet") {
         $metric_type = 'text';
      }
      else if(get_class($set) == "Reconnoiter_CompositeSet") {
         $metric_type = 'composite';
      }
      else {
         $metric_type = 'numeric';
      }
         
      $ds = array (
        'reconnoiter_source_expression' => $set->expression(),
        'reconnoiter_display_expression' => $this->sets_config[$name]['expression'],
        'dataname' => $this->sets_config[$name]['title'] ? $this->sets_config[$name]['title'] : $name,
        'data' => $this->graphdataset($set, $this->sets_config[$name]),
        'yaxis' => ($this->sets_config[$name]['axis'] == 'right') ? 2 : 1,
        'derive_val' => $set->derive_val(),
        'uuid' => $set->uuid_val(),
        'metric_name' => $m_name[1],	
        'metric_type' => $metric_type,
        'hidden' => ($this->sets_config[$name]['hidden'] == "true") ? 1: 0,
      );
      $show_set = ($this->sets_config[$name]['hidden'] != "true") ? 1: 0;
      if($show_set) {
        $ds['label'] = $ds['dataname'];
      }

      //by default, points, lines, and bars are set not to show in jquery.flot.js

      //if we have a numeric metric_type or a composite metric type, draw lines
      if( ($metric_type == 'numeric')  || ($metric_type == 'composite') ) {
        $opacity = isset($this->sets_config[$name]['opacity']) ?
                     $this->sets_config[$name]['opacity'] : '0.3';
        $ds['lines'] = array ( 'show' => ($show_set) ? 1:0, 'fill' => $opacity, 'lineWidth' => '1' , radius => 1 );
      }
      //if we have a text metric type, draw points
      else if($metric_type == 'text'){
        $ds['points'] = array ( 'show' => ($show_set) ? 1:0, 'fill' => 'false', 'lineWidth' => 1, radius => 5 );
      }

      $a[] = $ds;
    }

  if(count($a)) {     
    $start_ts = $a[0]['data'][0][0];
    $finish = end($a[0]['data']);
    $finish_ts = $finish[0];
    foreach($this->guides as $name => $value) {
      $a[] = array (
        'label' => $name,
        'data' => array(array($start_ts, "$value"), array($finish_ts, "$value")),
        'reconnoiter_display_expression' => $this->guides_config[$name]['expression'],
        'yaxis' => 1,
        'lines' => array ( 'color' => '3', 'show' => true, 'fill' => false, 'lineWidth' => '1' )
      );
    }
  }
    return $a;
  }

  function graphdataset($set, $config) {
    $i = 0;
    $a = array();
    $prev_value = '0';
    $timeline = ( (get_class($set) == "Reconnoiter_ChangeSet") 
    	      	  || (get_class($set) == "Reconnoiter_CompositeSet") )  ? $set->points() : $this->series();
    if($timeline) {
        foreach($timeline as $ts) {
            $value = $set->data($ts);
            if($value != "") {
              $desc = $set->description($ts);
              if($desc) {
                $a[] = array( $ts * 1000, "$value", $desc );	
              } else {
                $a[] = array( $ts * 1000, "$value" );
              }
            }
            $i++;
          }
    }
    return $a;
  }

}
