<?php

require_once('Reconnoiter_RPN.php');

class Reconnoiter_DataSet extends Reconnoiter_RPN {
  public $data;
  protected $expr;
  protected $default_attr;
  protected $groupname;
  protected $derive;
  protected $uuid;

  function __construct($uuid, $name, $derive, $expr, $start, $end, $cnt = 400) {
    $db = Reconnoiter_DB::getDB();
    $pgd = 'false';
    $this->default_attr = 'avg_value';

    $this->derive = $derive;
   $this->uuid = $db->get_uuid_by_sid($uuid);

    if($derive == 'derive' || $derive == 'true') {
      $pgd = 'true';
    }
    else if($derive == 'counter') {
      $this->default_attr = 'counter_dev';
    }
    $this->data = $db->get_data_for_window($uuid, $name, $start, $end, $cnt, $pgd);
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
  function expression() {
    return $this->expr;
  }
  function derive_val() {
    return $this->derive;
  }
  function uuid_val() {
    return $this->uuid;
  }
  function data($ts, $attr = NULL) {
    if(!isset($attr)) $attr = $this->default_attr;
    if(!$this->expr) return $this->data[$ts][$attr];
    return $this->rpn_eval($this->data[$ts][$attr], $this->expr);
  }
}
