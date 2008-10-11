<?php

class Reconnoiter_ChangeSet {
  public $data;
  protected $last_value;

  function __construct($uuid, $name, $start, $end, $cnt = 400) {
    $db = Reconnoiter_DB::getDB();
    $this->data = $db->get_var_for_window($uuid, $name, $start, $end, $cnt);
    $last = end($this->data);
    if($last) $this->last_value = $last['value'];
  }
  function points() {
    return array_keys($this->data);
  }
  function description($ts) {
    return $this->data[$ts]['value'];
  }
  function data($ts, $attr = 'avg_value') {
    return isset($this->data[$ts]['value']) ? "0" : null;
  }
  function last_value() {
    return $this->last_value;
  }
  function expression() { return ''; }
}

