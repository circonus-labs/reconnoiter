-- Function: stratcon.drop_old_metrics(interval, boolean)

-- DROP FUNCTION stratcon.drop_old_metrics(interval, boolean);

CREATE OR REPLACE FUNCTION stratcon.drop_old_metrics(in_keep interval, in_doit boolean DEFAULT false)
  RETURNS integer AS
$BODY$declare
  v_last date;
  v_rollups date;
  v_tdate text;
  v_rec record;
  v_sql text;
  v_count integer := 0;
begin

  -- refuse to delete todays data
  if in_keep < '1 minute'::interval then
    raise exception 'Cannot drop today data, specify some positive interval';
  end if;
  v_last := current_date - in_keep;

  -- get the last data, that has been rolled up
  select max(rollup_time)::date into v_rollups from noit.metric_numeric_rollup_5m;

  -- if the data to delete is still needed for rollups, refuse to delete it
  if v_last >= v_rollups then
    raise exception 'Cannot drop data, that is not rolled up. Are your rollups running? Last rollup is: %', v_rollups;
  end if;
  v_tdate = extract(year from v_last) || 
            lpad(extract(month from v_last)::text, 2, '0') ||
            lpad(extract(day from v_last)::text, 2, '0');

  -- get table names to drop
  for v_rec in
    select nspname||'.'||relname as tablename
      from pg_class c
      join pg_namespace n on (c.relnamespace = n.oid)
     where relkind = 'r'
       and ( ( relname like 'metric_numeric_archive_%' and relname <= ('metric_numeric_archive_' || v_tdate) ) or 
             ( relname like 'metric_text_archive_%' and relname <= ('metric_text_archive_' || v_tdate) ) )
     order by relname 
  loop
    v_sql := 'drop table ' || v_rec.tablename;
    raise notice 'Dropping table %', v_rec.tablename;
    if in_doit then
      execute v_sql;
    else
      raise notice 'Dry run, would execute: %', v_sql;
    end if;
    v_count := v_count + 1;
  end loop;
  return v_count;
end;$BODY$
  LANGUAGE 'plpgsql' VOLATILE
  COST 100;
ALTER FUNCTION stratcon.drop_old_metrics(interval, boolean) OWNER TO reconnoiter;
COMMENT ON FUNCTION stratcon.drop_old_metrics(interval, boolean) IS 'Drop old metric data, that has been rolled up. Will refuse to drop data, that is still needed.
Parameters:
 in_keep - how long into the past to keep data
 in_doit - must be set to true to actually drop the tables, if set to false, will perform dry run';