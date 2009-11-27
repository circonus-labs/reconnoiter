<?php

require_once('Reconnoiter_DB.php');

// Truly hatin' PHP
function __r_r($m) {
  global $__rgt_replace_set;
  return $__rgt_replace_set[$m[1]];
}

class Reconnoiter_GraphTemplate {
  private $row;
  function __construct($p) {
    if(is_array($p)) $this->row = $p;
    else {
      $db = Reconnoiter_DB::GetDB();
      $t = $db->get_templates($p);
      $this->row = $t[0];
    }
  }
  public $num_sids;

  public function variables() {
    return Reconnoiter_GraphTemplate::find_variables($this->row['json']);
  }
  public function sids() {
    return Reconnoiter_GraphTemplate::find_sids($this->row['json']);
  }
  public function getNewGraphJSON($params) {
    $v = $this->variables();
    global $__rgt_replace_set;
    $__rgt_replace_set = array();
    foreach($params as $p => $val) {
      if(isset($v[$p])) {
        $__rgt_replace_set[$p] = $val;
        unset($v[$p]);
      }
    }
    if(count($v) > 0) throw new Exception("Incomplete replacement set");
    $json = preg_replace_callback('/__(.*?)\[[^\]]+\]__/', "__r_r",
                                  $this->row['json']);
    $d = json_decode($json, true);
    if(!is_array($d)) throw new Exception("Internal Error: malformed json");

    /* Now we pull out the text metrics,
     * so we can pull their current values for interpolation.
     */
    foreach($d['datapoints'] as $dp) {
      if($dp['metric_type'] == "text") {
        $texts[$dp['name']] = array($dp['sid'], $dp['metric_name']);
      }
    }
    if(count($texts)) {
      $db = Reconnoiter_DB::getDB();
      $sth = $db->prepare("select value from stratcon.current_metric_text
                            where sid = ? and name = ?");
      $__rgt_replace_set = array();
      foreach($texts as $key => $binds) {
        $sth->execute($binds);
        $row = $sth->fetch();
        if($row) $__rgt_replace_set[$key] = $row['value'];
      }
      $json = preg_replace_callback('/%\{([^:\}]+):?([^\}]*)\}/', "__r_r",
                                    $json);
    }
    return $json;
  }
  public function find_variables($json) {
    global $__rgt_replace_set;
    $__rgt_replace_set = array();
    preg_match_all('/__(.*?)\[([^\]]+)\]__/', $json, $mall);
    $sid_set = array();
    for($i=0; $i<count($mall[0]); $i++) {
      $sid++;
      if(!isset($__rgt_replace_set[$mall[1][$i]]))
        $__rgt_replace_set[$mall[1][$i]] = $sid;
      if($mall[2][$i] == "SID" && !isset($sid_set[$mall[1][$i]]))
        $sid_set[$mall[1][$i]] = $sid;
      if($mall[2][$i] == "TEXT" && !isset($text_set[$mall[1][$i]]))
        $variable_set[$mall[1][$i]] = array('TEXT' => array());
    }
    $json = preg_replace_callback('/__(.*?)\[[^\]]+\]__/', "__r_r", $json);
    $d = json_decode($json, true);
    foreach($sid_set as $sidname => $sid) {
      foreach($d['datapoints'] as $dp) {
        if($dp['sid'] == $sid) {
          $variable_set[$sidname]['SID'][] =
            array($dp['metric_name'], $dp['metric_type']);
        }
      }
    }
    return $variable_set;
  }
  public function find_sids($json) {
    $this->num_sids = 0;
    $sids = array();
    $vs = Reconnoiter_GraphTemplate::find_variables($json);
    //vs will be multiD array containing variable names, their types (text/sid) and metric 
    //names and metric types
    
    foreach($vs as $v => $d) {
      if(isset($d['SID'])) {
        $this->num_sids++;
        $sql = "select m.* from noit.check_currently as m";
        $binds = array();
        $t = 1;
        $sids[$v] = array();
        foreach($d['SID'] as $m) {
          $sql = "$sql
                    join noit.metric_name_summary t$t
                      on (    m.sid = t$t.sid
                          and t$t.metric_name = ?
                          and t$t.metric_type = ?)";
          $binds[] = $m[0];
          $binds[] = $m[1];
          $t++;
        }
        if(count($binds)) {
          $db = Reconnoiter_DB::getDB();
          $sth = $db->prepare($sql);
          $sth->execute($binds);
          while($row = $sth->fetch()) {
            $sids[$v][] = $row;
          }
        }
      }
    }
    return $sids;
  }
}
