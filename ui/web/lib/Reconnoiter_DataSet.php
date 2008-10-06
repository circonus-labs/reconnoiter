<?php

require_once('Reconnoiter_RPN.php');

class Reconnoiter_DataSet extends Reconnoiter_RPN {
  public $data;
  protected $expr;
  protected $groupname;
  function __construct($uuid, $name, $derive, $expr, $start, $end, $cnt = 400) {
    $db = Reconnoiter_DB::getDB();
    $this->data = $db->get_data_for_window($uuid, $name, $start, $end, $cnt, $derive);
    $this->expr = $expr;
  }
  function groupname($gn = null) {
    if(isset($gn)) $this->groupname = $gn;
    return $this->groupname;
  }
  function points() {
    return array_keys($this->data);
  }
  function description($ts) {
    return null;
  }
  function data($ts, $attr = 'avg_value') {
    if(!$this->expr) return $this->data[$ts][$attr];
    return $this->rpn_eval($this->data[$ts][$attr], $this->expr);
  }
}
