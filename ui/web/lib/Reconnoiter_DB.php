<?php

require_once('Reconnoiter_UUID.php');

class Reconnoiter_DB {
  private $db;
  private $time_kludge;

  function __construct() {
    $this->time_kludge = "(? ::timestamp::text || '-00')::timestamptz";
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
    $this->db = new PDO("pgsql:host=noit.office.omniti.com;dbname=reconnoiter",
                        "prism", "prism");
    $this->db->setAttribute( PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION );
    $sth = $this->db->prepare("set timezone to 'UTC'");
    $sth->execute();
    
  }
  function prepare($sql) {
    return $this->db->prepare($sql);
  }

  // Crazy extract syntax to pull out the timestamps so that it looks like the current timezone, but in UTC
  function get_data_for_window($uuid, $name, $start, $end, $expected, $derive) {
    $type = preg_match('/^\d+$/', $uuid) ? '::integer' : '::uuid';
    $dsql = "select sid, name, extract(epoch from
                                rollup_time) as rollup_time,
             count_rows, avg_value, counter_dev
        from stratcon.fetch_dataset(
               ? $type,?,?,?,?,?)";
    $sth = $this->db->prepare($dsql);
    $args = array($uuid,$name,$start,$end,$expected,$derive);
    $sth->execute($args);
    $rv = array();
    while($row = $sth->fetch()) {
      $rv[$row['rollup_time']] = $row;
    }
    return $rv;
  }
  function get_var_for_window($uuid, $name, $start, $end, $expected) {
    $type = preg_match('/^\d+$/', $uuid) ? "::int" : "::uuid";
    $sth = $this->db->prepare("
      select sid, extract(epoch from whence) as whence,
             name, value
        from stratcon.fetch_varset(
               ? $type,?,?,?,?)");
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
  function get_noits() {
    $sth = $this->db->prepare("
      select distinct(remote_address) as remote_address
        from stratcon.loading_dock_check_s
        join (   select id, max(whence) as whence
                   from stratcon.loading_dock_check_s
               group by id) latestrecord
       USING (id,whence)");
    $rv = array();
    while($row = $sth->fetch()) {
      $rv[] = $row['remote_address'];
    }
    return $rv;
  }
  function valid_source_variables() {
    return array('module', 'remote_address', 'target', 'name', 'metric_name');
  }
  private function tsearchize($searchstring) {
    $searchstring = trim($searchstring);
    $searchstring = preg_replace('/\s+/', ' ', $searchstring);
    $searchstring = preg_replace('/\b(\'[^\']+\'|[^\s\|\'&]\S*)\s+(?![\|\)&])/',
                                 '$1 & ', $searchstring);
    return $searchstring;
  }
  function get_templates($templateid = '') {
    $sql = "select * from prism.graph_templates";
    $binds = array();
    if($templateid) {
      $sql = "$sql where templateid = ?";
      $binds[] = $templateid;
    }
    $sth = $this->db->prepare($sql);
    $sth->execute($binds);
    $a = array();
    while($row = $sth->fetch()) {
      $a[] = $row;
    }
    return $a;
  }
  protected function run_tsearch($searchstring, $countsql, $datasql, $offset, $limit) {
    $searchstring = $this->tsearchize($searchstring);

    $binds = array($searchstring);
    $sth = $this->db->prepare($countsql);
    $sth->execute($binds);
    $r = $sth->fetch();

    array_push($binds, $limit);
    array_push($binds, $offset);
    $sth = $this->db->prepare("$datasql limit ? offset ?");
    $sth->execute($binds);
    $a = array();
    while($row = $sth->fetch()) $a[] = $row;
    return array('query' => $searchstring, 'limit' => $limit,
                 'offset' => $offset, count => $r['count'], 'results' => $a);
  }
  function get_worksheets($searchstring, $offset, $limit) {
    return $this->run_tsearch($searchstring,
      "select count(*) as count
         from prism.saved_worksheets,
              (select ? ::text as query) q
        where saved = true and
           ((ts_search_all @@ to_tsquery(query) or query = '')
            or sheetid in (select sheetid
                            from prism.saved_worksheets_dep wd
                            join prism.saved_graphs g
                           using (graphid)
                            join prism.saved_graphs_dep gd
                           using (graphid)
                            join stratcon.metric_name_summary s
                           using (sid,metric_name,metric_type)
                           where g.ts_search_all @@ to_tsquery(query)
                              or s.ts_search_all @@ to_tsquery(query)))",
      "select sheetid, title,
              to_char(last_update, 'YYYY/mm/dd') as last_update
         from prism.saved_worksheets,
              (select ? ::text as query) q
        where saved = true and
           ((ts_search_all @@ to_tsquery(query) or query = '')
            or sheetid in (select sheetid
                            from prism.saved_worksheets_dep wd
                            join prism.saved_graphs g
                           using (graphid)
                            join prism.saved_graphs_dep gd
                           using (graphid)
                            join stratcon.metric_name_summary s
                           using (sid,metric_name,metric_type)
                           where g.ts_search_all @@ to_tsquery(query)
                              or s.ts_search_all @@ to_tsquery(query)))
     order by last_update desc",
      $offset, $limit);
  }
  function get_graphs($searchstring, $offset, $limit) {
    return $this->run_tsearch($searchstring,
      "select count(*) as count
         from prism.saved_graphs,
              (select ? ::text as query) q
        where saved = true and
           ((ts_search_all @@ to_tsquery(query) or query = '')
            or graphid in (select graphid
                            from prism.saved_graphs_dep gd
                            join stratcon.metric_name_summary s
                           using (sid,metric_name,metric_type)
                           where ts_search_all @@ to_tsquery(query)))",
      "select graphid, title, json,
              to_char(last_update, 'YYYY/mm/dd') as last_update
         from prism.saved_graphs,
              (select ? ::text as query) q
        where saved = true and
           ((ts_search_all @@ to_tsquery(query) or query = '')
            or graphid in (select graphid
                            from prism.saved_graphs_dep gd
                            join stratcon.metric_name_summary s
                           using (sid,metric_name,metric_type)
                           where ts_search_all @@ to_tsquery(query)))
     order by last_update desc",
      $offset, $limit);
  }
  function get_datapoints($searchstring, $offset, $limit) {
    return $this->run_tsearch($searchstring,
      "select count(*) as count
         from stratcon.mv_loading_dock_check_s c
         join stratcon.metric_name_summary m using (sid),
              (select ? ::text as query) q
        where active = true and (query = '' or ts_search_all @@ to_tsquery(query))",
      "select c.id, c.sid, c.remote_address,
              c.target, c.whence, c.module, c.name,
              m.metric_name, m.metric_type
         from stratcon.mv_loading_dock_check_s c
         join stratcon.metric_name_summary m using (sid),
              (select ? ::text as query) q
        where active = true and (query = '' or ts_search_all @@ to_tsquery(query))
     order by target, module, name, remote_address",
      $offset, $limit);
  }
  function get_sources($want, $fixate, $active = true) {
    $vars = $this->valid_source_variables();
    if(!in_array($want, $vars)) return array();
    $tblsrc = ($want == 'metric_name') ? 'm' : 'c';
    $binds = array();
    $named_binds = array();
    $where_sql = '';
    foreach($vars as $var) {
      if(isset($fixate[$var])) {
        $fix_tblsrc = ($var == 'metric_name') ? 'm' : 'c';
        $where_sql .= " and $fix_tblsrc.$var = ?";
        $binds[] = $fixate[$var];
        $named_binds[$var] = $fixate[$var];
      }
    }
    $ptr_select = '';
    $ptr_groupby = '';
    $ptr_join = '';
    if($want == 'target' || $want == 'remote_address') {
      $ptr_select = 'ciamt.value as ptr, ';
      $ptr_groupby = ', ciamt.value';
      $ptr_join = "
        left join stratcon.mv_loading_dock_check_s cia
               on (    $tblsrc.$want ::inet = cia.target ::inet
                   and cia.module='dns' and cia.name='in-addr.arpa')
        left join stratcon.current_metric_text ciamt
               on (cia.sid = ciamt.sid and ciamt.name='answer')";
    }
    else if($want == 'name') {
      $ptr_select = 'caliasmt.value as ptr, ';
      $ptr_groupby = ', caliasmt.value';
      $ptr_join = "
        left join stratcon.current_metric_text caliasmt
               on (c.sid = caliasmt.sid and caliasmt.name='alias')";
    }
    else if($want == 'metric_name') {
      $ptr_select = "min(c.module) as module, ";
    }
    $sql = "
      select $tblsrc.$want, $ptr_select
             min(c.sid) as sid, min(metric_type) as metric_type,
             count(1) as cnt
        from stratcon.mv_loading_dock_check_s c
        join stratcon.metric_name_summary m using (sid)
             $ptr_join
       where active = " . ($active ? "true" : "false") . $where_sql . "
    group by $tblsrc.$want $ptr_groupby
    order by $tblsrc.$want";
    $sth = $this->db->prepare($sql);
    $sth->execute($binds);

    $rv = array();
    while($row = $sth->fetch()) {
      $copy = $named_binds;
      $copy[$want] = $row[$want];
      $copy['sid'] = $row['sid'];
      $copy['id'] = 'ds';
      foreach($vars as $var) {
        $copy['id'] .= "-" . $copy[$var];
      }
      $copy['cnt'] = $row['cnt'];
      if(isset($row['module'])) $copy['module'] = $row['module'];
      if(isset($row['ptr'])) $copy['ptr'] = $row['ptr'];
      if($copy['cnt'] == 1 &&
         isset($row['sid']) && 
         isset($row['metric_name'])) {
        $copy['unique'] = true;
        $copy['metric_type'] = $row['metric_type'];
      }
      else
        $copy['unique'] = false;
      $rv[] = $copy;
    }
    return $rv;
  }
  function get_targets($remote_address = null) {
    if($remote_address) {
      $sth = $this->db->prepare("select distinct(target) as target from stratcon.loading_dock_check_s where remote_address = ?");
      $sth->execute(array($remote_address));
    }
    else {
      $sth = $this->db->prepare("select distinct(target) as target from stratcon.loading_dock_check_s");
      $sth->execute();
    }
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
                             'sid' => $row['sid'],
                             'text' => array(),
                             'numeric' => array() );
      $rv[$label][$row['metric_type']][] = $row['metric_name'];
    }
    return $rv;
  }
  function percentile($arr, $p, $groupname = 'left', $attr = NULL) {
    // This sums the sets and returns the XX percentile bucket.
    if(!is_array($arr)) return array();
    if(!is_array($p)) $p = array($p);
    $full = array();
    foreach ($arr[0]->points() as $ts) {
      $sum = 0;
      $nonnull = 0;
      foreach ($arr as $sets) {
        if($sets->groupname() != $groupname) continue;
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
  function getWorksheetByID($id) {
    $sth = $this->db->prepare("select w.sheetid, w.title, wd.graphid
                                 from prism.saved_worksheets as w
                            left join prism.saved_worksheets_dep as wd
                                using (sheetid)
                                where sheetid=?
                             order by ordering asc");
    $sth->execute(array($id));
    $a = array();
    $row = $sth->fetch();
    $a['sheetid'] = $row['sheetid'];
    $a['title'] = $row['title'];
    $a['graphs'] = array();
    if($row && $row['graphid']) $a['graphs'][] = $row['graphid'];
    while($row = $sth->fetch()) {
      $a['graphs'][] = $row['graphid'];
    }
    return $a;
  }
  function saveWorksheet($ws) {

    $id = '';
    if($ws['id']) {
      $id = $ws['id'];
      unset($ws['id']);
    }
    $this->db->beginTransaction();
    try {
      if($id) {
        $sth = $this->db->prepare("update prism.saved_worksheets
                                      set title=?, saved=(saved or ?),
                                          last_update=current_timestamp
                                    where sheetid=?");
        $sth->execute(array($ws['title'],$ws['saved'],$id));
        if($sth->rowCount() != 1) throw(new Exception('No such worksheet: '.$id));
      }
      else {
        $id = Reconnoiter_UUID::generate();
        $sth = $this->db->prepare("insert into prism.saved_worksheets
                                               (sheetid, title,
                                                last_update)
                                        values (?, ?, current_timestamp)");
        $sth->execute(array($id, $ws['title']));
      }

       $sth = $this->db->prepare("delete from prism.saved_worksheets_dep
                                    where sheetid=?");
       $sth->execute(array($id));

      $sth = $this->db->prepare("insert into prism.saved_worksheets_dep
                                             (sheetid, ordering, graphid)
                                      values (?,?,?)");
      $ordering = 0;
      foreach($ws['graphs'] as $graphid) {
        $sth->execute(array($id,$ordering++,$graphid));
      }
      $this->db->commit();
    }
    catch(PDOException $e) {
      $this->db->rollback();
      throw(new Exception('DB: ' . $e->getMessage()));
    }
    return $id;
  }
  function get_uuid_by_sid($sid) {
    $sth = $this->db->prepare("select * from stratcon.map_uuid_to_sid
                                           where sid=?");
    $sth->execute(array($sid));
    $row = $sth->fetch();
    return $row['id'];
  }
  function getGraphByID($id) {
    $sth = $this->db->prepare("select *
                                 from prism.saved_graphs
                                where graphid=?");
    $sth->execute(array($id));
    $row = $sth->fetch();
    return $row;
  }
  function getGraphByGenesis($genesis) {
    $sth = $this->db->prepare("select *
                                 from prism.saved_graphs
                                where genesis=?");
    $sth->execute(array($genesis));
    $row = $sth->fetch();
    return $row;
  }
  function deleteWorksheetGraph($ws_id, $graphid) {
    $sth = $this->db->prepare("delete from prism.saved_worksheets_dep 
                                      where sheetid=? and graphid=?");
    $sth->execute(array($ws_id, $graphid));
  }
  function getWorksheetsByGraph($graphid){

    $sth = $this->db->prepare("select title from
    	 (select sheetid from prism.saved_worksheets_dep 
	where prism.saved_worksheets_dep.graphid=?) as rel_swd
	natural join prism.saved_worksheets");

    $sth->execute(array($graphid));
    
    $rv = array();
    while($row = $sth->fetch()) {
      $rv[] = $row['title'];
    }
    return $rv;
}
//this will also delete from saved_graphs_dep by cascade, so better call getWorksheetsByGraph to check first
  function deleteGraph($id) {
    $sth = $this->db->prepare("delete from prism.saved_graphs
                                     where graphid=?");
    $sth->execute(array($id));
  }
  function saveGraph($graph) {
    $id = '';
    if($graph['id']) {
      $id = $graph['id'];
      unset($graph['id']);
    }
    $json = json_encode($graph);
    $this->db->beginTransaction();
    try {
      if($id) {
        $sth = $this->db->prepare("update prism.saved_graphs
                                      set json=?, title=?, saved=(saved or ?),
                                          last_update=current_timestamp
                                    where graphid=?");
        $sth->execute(array($json,$graph['title'],
                            $graph['saved']?'true':'false',$id));
        if($sth->rowCount() != 1) throw(new Exception('No such graph: '.$id));
        $sth = $this->db->prepare("delete from prism.saved_graphs_dep
                                         where graphid = ?");
        $sth->execute(array($id));
      }
      else {
        $id = Reconnoiter_UUID::generate();
        $sth = $this->db->prepare("insert into prism.saved_graphs
                                               (graphid, json, title,
                                                last_update, genesis)
                                        values (?, ?, ?, current_timestamp, ?)");
        $sth->execute(array($id, $json, $graph['title'], $graph['genesis']));
      }
      $sth = $this->db->prepare("insert into prism.saved_graphs_dep
                                             (graphid, sid,
                                              metric_name, metric_type)
                                      values (?,?,?,?)");
      foreach($graph['datapoints'] as $datapoint) {
        if($datapoint['sid']) {
          $sth->execute(array($id, $datapoint['sid'],
                              $datapoint['metric_name'],
                              $datapoint['metric_type']));
        }
      }
      $this->db->commit();
    }
    catch(PDOException $e) {
      $this->db->rollback();
      throw(new Exception('DB: ' . $e->getMessage()));
    }
    return $id;
  }

  public function make_names_for_template($sid_list) {

    $binds = array();

    $sql = "select m.* from stratcon.mv_loading_dock_check_s m where m.sid in (";
    for ($i =0 ; $i<count($sid_list);$i++){
      $binds[] = $sid_list[$i];
      $sql.= ($i == count($sid_list)-1) ? "?)" : "?,";
    }
    $sth = $this->db->prepare($sql);
    $sth->execute($binds);

    $data = array();
    while($row = $sth->fetch()) {
      $data[] = $row;
    }

    foreach ($data as $key => $rw) {
      $target[$key] = $rw['target'];
      $module[$key] = $rw['module'];
      $name[$key] = $rw['name'];
      $sid[$key] = $rw['sid'];
    }
    array_multisort($target, SORT_ASC, $module, SORT_ASC,$name, SORT_ASC, $data);

    $rvlist = array();
    foreach($data as $row) {
      $rvlist[] = array( 'sid' => $row['sid'],
      		'option' => $row['target']."`".$row['module']."`".$row['name']);
    }
    return $rvlist;
  }

}

