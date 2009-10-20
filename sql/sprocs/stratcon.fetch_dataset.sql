-- Function: stratcon.fetch_dataset(uuid, text, timestamp with time zone, timestamp with time zone, integer, boolean)

CREATE OR REPLACE FUNCTION stratcon.fetch_dataset(in_uuid uuid, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean)
  RETURNS SETOF noit.metric_numeric_rollup_5m AS
$BODY$declare
  v_sid int;
  v_record noit.metric_numeric_rollup_5m%rowtype;
begin
  select sid into v_sid from stratcon.map_uuid_to_sid where id = in_uuid;
  if not found then
    return;
  end if;

    for v_record in  select sid, name, rollup_time, count_rows, avg_value, counter_dev from stratcon.fetch_dataset(v_sid::integer, in_name, in_start_time, in_end_time, in_hopeful_nperiods, derive) loop
    return next v_record; 
    end loop;

--  return query select sid, name, rollup_time, count_rows, avg_value from stratcon.fetch_dataset(v_sid::integer, in_name, in_start_time, in_end_time, in_hopeful_nperiods, derive);
  return;
end
$BODY$
  LANGUAGE 'plpgsql' SECURITY DEFINER;

GRANT EXECUTE ON FUNCTION stratcon.fetch_dataset(uuid, text, timestamp with time zone, timestamp with time zone, integer, boolean) TO stratcon;

CREATE OR REPLACE FUNCTION stratcon.fetch_dataset(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer, derive boolean)
  RETURNS SETOF noit.metric_numeric_rollup_5m AS
$BODY$declare
  v_sql text;
  v_sid int;
  v_target record;
  v_interval numeric;
  v_start_adj timestamptz;
  v_end_adj timestamptz;
  v_l_rollup_row noit.metric_numeric_rollup_5m%rowtype;
  v_rollup_row noit.metric_numeric_rollup_5m%rowtype;
  v_r_rollup_row noit.metric_numeric_rollup_5m%rowtype;
begin

  -- Map out uuid to an sid.
  v_sid := in_sid;

  select * into v_target from stratcon.choose_window(in_start_time, in_end_time, in_hopeful_nperiods);

  if not found then
    raise exception 'no target table';
    return;
  end if;

  select 'epoch'::timestamp +
         ((floor(extract('epoch' from in_start_time) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval
    into v_start_adj;

  select 'epoch'::timestamp +
         ((floor(extract('epoch' from in_end_time) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval
    into v_end_adj;

  v_sql := 'select ' || v_sid || ' as sid, ' || quote_literal(in_name) || ' as name, ' ||
           's.rollup_time, d.count_rows, d.avg_value, d.counter_dev ' ||
           ' from ' ||
           '(select ' || quote_literal(v_start_adj) || '::timestamp' ||
                  ' + t * ' || quote_literal(v_target.period) || '::interval' ||
                       ' as rollup_time' ||
             ' from generate_series(1,' || v_target.nperiods || ') t) s ' ||
           'left join ' ||
           '(select * from ' || v_target.tablename ||
           ' where sid = ' || v_sid ||
             ' and name = ' || quote_literal(in_name) ||
             ' and rollup_time between ' || quote_literal(v_start_adj) || '::timestamp' ||
                                 ' and ' || quote_literal(v_end_adj) || '::timestamp) d' ||
           ' using(rollup_time)';

  for v_rollup_row in execute v_sql loop
    if derive is true then
      v_r_rollup_row := v_rollup_row;
      if v_l_rollup_row.count_rows is not null and
         v_rollup_row.count_rows is not null then
        v_interval := extract('epoch' from v_rollup_row.rollup_time) - extract('epoch' from v_l_rollup_row.rollup_time);
        v_r_rollup_row.count_rows := (v_l_rollup_row.count_rows + v_rollup_row.count_rows) / 2;
        v_r_rollup_row.avg_value :=
          (v_rollup_row.avg_value - v_l_rollup_row.avg_value) / v_interval;
      else
        v_r_rollup_row.count_rows = NULL;
        v_r_rollup_row.avg_value = NULL;
        
      end if;
    else
      v_r_rollup_row := v_rollup_row;
    end if;
    return next v_r_rollup_row;
    v_l_rollup_row := v_rollup_row;
  end loop;
  return;
end
$BODY$
  LANGUAGE 'plpgsql' SECURITY DEFINER;
GRANT EXECUTE ON FUNCTION  stratcon.fetch_dataset(integer, text, timestamp with time zone, timestamp with time zone, integer, boolean) TO stratcon;
 
