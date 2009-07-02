<?php

class Reconnoiter_CompositeSet{
  public $data;
  protected $expr;
  protected $default_attr;
  protected $groupname;
  protected $derive;
  protected $uuid;

  function __construct($num_composites, $name, $expr) {

   $this->default_attr = 'avg_value';
   $this->uuid = $num_composites;
    $this->derive = 'none';
    $this->data = null;
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
    return '';
  }
}
