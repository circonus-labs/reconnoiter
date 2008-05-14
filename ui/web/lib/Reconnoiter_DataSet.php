<?php

class Reconnoiter_DataSet {
  public $data;
  protected $expr;
  function __construct($uuid, $name, $derive, $expr, $start, $end, $cnt = 400) {
    $db = Reconnoiter_DB::getDB();
    $this->data = $db->get_data_for_window($uuid, $name, $start, $end, $cnt, $derive);
    $this->expr = $expr;
  }
  function points() {
    return array_keys($this->data);
  }
  function description($ts) {
    return null;
  }
  function data($ts, $attr = 'avg_value') {
    if(!$this->expr) return $this->data[$ts][$attr];
    $value = $this->data[$ts][$attr];
    eval("\$value = $this->expr;");
    return $value;
  }
}
