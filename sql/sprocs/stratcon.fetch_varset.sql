-- Function: stratcon.fetch_varset(uuid, text, timestamp with time zone, timestamp with time zone, integer)

CREATE OR REPLACE FUNCTION stratcon.fetch_varset(in_check uuid, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer)
  RETURNS SETOF noit.metric_text_changelog AS
$BODY$
declare
  v_sid int;
begin
  -- Map out uuid to an sid.
  select sid into v_sid from stratcon.map_uuid_to_sid where id = in_check;
  if not found then
    return;
  end if;

  return query select * from stratcon._fetch_varset(v_sid::integer, in_name, in_start_time, in_end_time, in_hopeful_nperiods);
end
$BODY$
  LANGUAGE 'plpgsql'  SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION stratcon.fetch_varset(uuid, text, timestamp with time zone, timestamp with time zone, integer) TO stratcon;

-- Function: stratcon._fetch_varset(integer, text, timestamp with time zone, timestamp with time zone, integer)

CREATE OR REPLACE FUNCTION stratcon._fetch_varset(in_sid integer, in_name text, in_start_time timestamp with time zone, in_end_time timestamp with time zone, in_hopeful_nperiods integer)
  RETURNS SETOF noit.metric_text_changelog AS
$BODY$declare
  v_sid int;
  v_target record;
  v_start_adj timestamptz;
  v_start_text text;
  v_next_text text;
  v_end_adj timestamptz;
  v_change_row noit.metric_text_changelog%rowtype;
begin
  -- Map out uuid to an sid.
  v_sid := in_sid;

  select * into v_target from stratcon.choose_window(in_start_time, in_end_time, in_hopeful_nperiods);

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

  for v_change_row in
    select sid, 'epoch'::timestamp +
         ((floor(extract('epoch' from whence) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval as whence,
           name, value
      from noit.metric_text_changelog
     where sid = v_sid
       and name = in_name
       and whence <= v_start_adj
  order by 'epoch'::timestamp +
         ((floor(extract('epoch' from whence) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval desc
     limit 1
  loop
    v_start_text := coalesce(v_change_row.value, '[unset]');
  end loop;

  for v_change_row in
    select v_sid as sid, whence, in_name as name, value from
--    (select v_start_adj::timestamp + t * v_target.period::interval as whence
--      from generate_series(1, v_target.nperiods) t) s 
-- left join
    (select 'epoch'::timestamp +
         ((floor(extract('epoch' from whence) /
                 extract('epoch' from v_target.period)) *
           extract('epoch' from v_target.period)) || ' seconds') ::interval as whence,
           coalesce(value, '[unset]') as value
      from stratcon.loading_dock_metric_text_s_change_log
     where sid = v_sid
       and name = in_name
       and whence > v_start_adj
       and whence <= v_end_adj) d
--    using (whence)
  order by whence asc
  loop
    v_next_text := v_change_row.value;
    if v_change_row.value is not null and
       v_start_text != v_change_row.value then
      v_change_row.value := coalesce(v_start_text, '[unset]') || ' -> ' || coalesce(v_change_row.value, '[unset]');
    else
      v_change_row.value := v_start_text;
    end if;
    if v_next_text is not null then
      v_start_text := v_next_text;
    end if;
    return next v_change_row;
  end loop;


  if v_next_text is null then
    -- No rows.
    for v_change_row in
      select v_sid as sid, v_start_adj as whence, in_name as name, v_start_text as value
    loop
      return next v_change_row;
    end loop;
  end if;

  return;
end
$BODY$
  LANGUAGE 'plpgsql' SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION stratcon._fetch_varset(integer, text, timestamp with time zone, timestamp with time zone, integer) TO stratcon;

