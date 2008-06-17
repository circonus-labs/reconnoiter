<?php

class Reconnoiter_DB {
  private $db;

  function __construct() {
  }
  function getDB() {
    static $one;
    if(!isset($one)) {
      $class = __CLASS__;
      $one = new $class;
      $one->connect();
    }
    return $one;
  }
  function connect() {
    $this->db = new PDO("pgsql:host=localhost;dbname=reconnoiter",
                        "stratcon", "stratcon");
    $this->db->setAttribute( PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION );
  }

  function get_data_for_window($uuid, $name, $start, $end, $expected, $derive) {
    $sth = $this->db->prepare("select * from stratcon.fetch_dataset(?,?,?,?,?,?)");
    $sth->execute(array($uuid,$name,$start,$end,$expected,$derive));
    $rv = array();
    while($row = $sth->fetch()) {
      $rv[$row['rollup_time']] = $row;
    }
    return $rv;
  }
  function get_var_for_window($uuid, $name, $start, $end, $expected) {
    $sth = $this->db->prepare("select * from stratcon.fetch_varset(?,?,?,?,?)");
    $sth->execute(array($uuid,$name,$start,$end,$expected));
    $rv = array();
    while($row = $sth->fetch()) {
      if(isset($rv[$row['whence']]))
        $rv[$row['whence']]['value'] = $rv[$row['whence']]['value'] . "\n" .
                                       $row['value'];
      else
        $rv[$row['whence']] = $row;
    }
    return $rv;
  }
  function get_targets() {
    $sth = $this->db->prepare("select distinct(target) as target from stratcon.loading_dock_check_s");
    $sth->execute();
    $rv = array();
    while($row = $sth->fetch()) {
      $rv[] = $row['target'];
    }
    return $rv;
  }
  function get_checks($target, $active = 'true') {
    $sth = $this->db->prepare("
      select sid, id, check_name, metric_name, metric_type
        from (
         select distinct on (sid, id) sid, id, name as check_name
           from stratcon.loading_dock_check_s
          where target = ?
       order by sid, id, whence desc
             ) c
        join stratcon.metric_name_summary using(sid)
       where active = ?
    ");
    $sth->execute(array($target, $active));
    $rv = array();
    while($row = $sth->fetch()) {
      $label = $row['check_name']."(".$row['sid'].")";
      if(!isset($rv[$label])) 
        $rv[$label] = array ('id' => $row['id'],
                             'text' => array(),
                             'numeric' => array() );
      $rv[$label][$row['metric_type']][] = $row['metric_name'];
    }
    return $rv;
  }
  function percentile($arr, $p, $attr = 'avg_value') {
    // This sums the sets and returns the XX percentile bucket.
    if(!is_array($arr)) return array();
    if(!is_array($p)) $p = array($p);
    $full = array();
    foreach ($arr[0]->points() as $ts) {
      $sum = 0;
      $nonnull = 0;
      foreach ($arr as $sets) {
        $value = $sets->data($ts, $attr);
        if($value != "") $nonnull = 1;
        $sum += $value;
      }
      if($nonnull == 1) $full[] = $sum;
    }
    sort($full, SORT_NUMERIC);
    $rv = array();
    foreach ($p as $wanted) {
      $idx = max(ceil((count($full)-1) * ($wanted/100.0)), 0);
      $rv[$wanted] = $full[$idx];
    }
    return $rv;
  }
}

