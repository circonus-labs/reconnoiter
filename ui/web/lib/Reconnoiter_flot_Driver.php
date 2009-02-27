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
      $ds = array (
        'reconnoiter_source_expression' => $set->expression(),
        'reconnoiter_display_expression' => $this->sets_config[$name]['expression'],
        'dataname' => $this->sets_config[$name]['title'] ? $this->sets_config[$name]['title'] : $name,
        'data' => $this->graphdataset($set, $this->sets_config[$name]),
        'yaxis' => ($this->sets_config[$name]['axis'] == 'right') ? 2 : 1,
        'derive_val' => $set->derive_val(),
        'uuid' => $set->uuid_val(),
        'metric_name' => $m_name[1],
        'metric_type' => (get_class($set) == "Reconnoiter_DataSet") ? 'numeric' : 'text',
        'hidden' => ($this->sets_config[$name]['hidden'] == "true") ? 1: 0,
      );
      $show_set = ($this->sets_config[$name]['hidden'] != "true") ? 1: 0;
      if($show_set) {
        $ds['label'] = $ds['dataname'];
      }

      //by default, points, lines, and bars are set not to show in jquery.flot.js
      //if we have a numeric metric_type, draw lines
      if(get_class($set) == "Reconnoiter_DataSet") {
        $opacity = isset($this->sets_config[$name]['opacity']) ?
                     $this->sets_config[$name]['opacity'] : '0.3';
        $ds['lines'] = array ( 'show' => ($show_set) ? 1:0, 'fill' => $opacity, 'lineWidth' => '1' , radius => 1 );
      }
      //if we have a text metric type, draw points
      else if(get_class($set) == "Reconnoiter_ChangeSet") {
        $ds['points'] = array ( 'show' => ($show_set) ? 1:0, 'fill' => 'false', 'lineWidth' => 1, radius => 5 );
      }

      $a[] = $ds;
    }

    //hack for stacking...will ignore datasets and datapoints that are not set or not on the left axis, so
    //each dataset needs to have each point set for this stacking to work
    //non numeric metric are given the value 0 above, so that if stacked, they show up on the plot-line itself
    if($this->type == "stacked") {
        $left_count = 0; $bottom = -1; $index=0;
        foreach($this->sets as $name => $set) {	
            if($this->sets_config[$name]['axis'] == 'left'){
	        $left_count++;
		if($left_count>1) {
                    for ($point = 0; $point < count($a[$index]['data']); $point++){
	                if( ($a[$bottom]['data'][$point][1] != "") && ($a[$index]['data'][$point][1] != "")) {	
		            $tmp = $a[$index]['data'][$point][1] +  $a[$bottom]['data'][$point][1];
                            $a[$index]['data'][$point][1] = "$tmp";
  	                }                
	            }
	        }  	   	       
		$bottom =  $index; 
            }//end if left axis
	    $index++;
        }
    }//end if stacking

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
    $timeline = (get_class($set) == "Reconnoiter_ChangeSet") ? $set->points() : $this->series();
    foreach($timeline as $ts) {
        $value = $set->data($ts);
        if($value != "") {
          if(isset($config['expression'])) {
            $value = $this->rpn_eval($value, $config['expression']);
          }
          $desc = $set->description($ts);
          if($desc) {
            $a[] = array( $ts * 1000, "$value", $desc );	
          } else {
            $a[] = array( $ts * 1000, "$value" );
          }
        }
        else $a[] = array( $ts * 1000, "" );	
        $i++;
      }
    return $a;
  }

}
