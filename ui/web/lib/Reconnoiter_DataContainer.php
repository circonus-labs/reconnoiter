<?php

require_once('Reconnoiter_DB.php');
require_once('Reconnoiter_DataSet.php');
require_once('Reconnoiter_ChangeSet.php');
require_once('Reconnoiter_RPN.php');

class Reconnoiter_DataContainer extends Reconnoiter_RPN {
  protected $units = 0;
  protected $start;
  protected $end;
  protected $cnt;
  protected $ps_to_calc;
  protected $percentile;
  protected $title;

  protected $master_set;
  protected $sets;
  protected $sets_config;
  protected $guides;

  function __construct($start, $end, $cnt) {
    $this->start = $start;
    $this->end = $end;
    $this->cnt = $cnt;
    $this->sets = array();
    $this->guides = array();
    $this->ps_to_calc = array(0 => 'true', 95 => 'true', 100 => 'true');
  }
  function start() { return $this->start; }
  function end() { return $this->end; }
  function cnt() { return $this->cnt; }
  function title() { return $this->title; }
  function defaultDataSetAttrs($uuid, $name, $derive, $attrs) {
    return $attrs;
  }
  function defaultChangeSetAttrs($uuid, $name, $derive, $attrs) {
    return $attrs;
  }
  function addDataSet($uuid, $name, $derive = 'false',
                      $expr = null, $attrs = array()) {
    $attrs = $this->defaultDataSetAttrs($uuid, $name, $derive, $attrs);

    $this->sets["$uuid-$name"] =
      new Reconnoiter_DataSet($uuid, $name, $derive, $expr,
                              $this->start(), $this->end(), $this->cnt());
    $this->sets["$uuid-$name"]->groupname($attrs['axis']);
    $this->sets_config["$uuid-$name"] = is_array($attrs) ? $attrs : array();
    if(!isset($this->master_set)) $this->master_set = $this->sets["$uuid-$name"];
  }
  function addChangeSet($uuid, $name, $attrs = array()) {
    $attrs = $this->defaultChangeSetAttrs($uuid, $name, $derive, $attrs);
    $this->sets["$uuid-$name"] =
      new Reconnoiter_ChangeSet($uuid, $name,
                                $this->start(), $this->end(), $this->cnt());
    if(!$this->title) $this->title = $this->sets["$uuid-$name"]->last_value();
    $this->sets_config["$uuid-$name"] = is_array($attrs) ? $attrs : array();
  }
  function series() {
    return $this->master_set->points();
  }
  function addGuide($name, $g, $config = array()) {
    $this->guides[$name] = $g;
    $this->guides_config[$name] = $config;
  }
  function addPercentileGuide($name, $p, $config = array()) {
    $this->__calc();
    $this->addGuide($name, $this->percentile[$p], $config);
  }
  function min() { $this->__calc(); return $this->percentile[0]; }
  function max() { $this->__calc(); return $this->percentile[100]; }
  function calcPercentile($p) {
    if($this->units) throw Exception("Already calculated percentiles");
    $this->ps_to_calc[$p] = 'true';
  }
  function __calc() {
    if($this->units) return;
    $db = Reconnoiter_DB::getDB();
    foreach($db->percentile(array_values($this->sets),
                            array_keys($this->ps_to_calc)) as $p => $v) {
      $this->percentile[$p] = $v;
    }
    $this->units = pow(1000,floor(log($this->percentile[100], 1000)));
    if($this->units == 0) $this->units = 1;
  }
  function autounits($value) {
    $this->auto_units_on = true;
    $this->__calc();
    return $value / $this->units;
  }
  function autounit() {
    if(!$this->auto_units_on) return '';
    switch($this->units) {
      case 0.000000001: return 'n';
      case '1.0E-6':
      case 0.000001: return 'u';
      case 0.001: return 'm';
      case 1000: return 'k';
      case 1000000: return 'M';
      case 1000000000: return 'G';
      case 1000000000000: return 'T';
    }
    return '';
  }
  function bytes2bits($value) {
    return 8 * $value;
  }
  function bw($value) {
    return round($this->autounits($value),2);
  }
}
