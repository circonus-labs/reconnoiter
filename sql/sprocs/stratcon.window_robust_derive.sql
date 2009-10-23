-- Function: stratcon.window_robust_derive(timestamp with time zone)

DROP TYPE IF EXISTS stratcon.metric_numeric_rollup_segment;
CREATE TYPE stratcon.metric_numeric_rollup_segment AS (sid integer,  name text, rollup_time timestamp with time zone, count_rows integer, avg_value numeric, counter_dev numeric);      


CREATE OR REPLACE FUNCTION stratcon.window_robust_derive(in_start_time timestamp with time zone)
  RETURNS SETOF stratcon.metric_numeric_rollup_segment AS
$BODY$
declare
  rec stratcon.metric_numeric_rollup_segment%rowtype;
  r record;
  rise numeric;
  last_row_whence timestamp;
  last_value numeric;
  run numeric;
begin

   rec.sid := null;
   rec.name := null;
   rec.count_rows := 0;
   rec.avg_value := 0;
   rise := 0;
   run := 0;
   rec.rollup_time = in_start_time;
   for r in SELECT sid, name, whence,
                   (whence > in_start_time - '5 minutes'::interval) as in_window,
                   value
              FROM metric_numeric_archive
             WHERE whence <= in_start_time
               AND whence > in_start_time - ('5 minutes'::interval * 2)
          order BY sid,name,whence
  loop
  if (rec.sid is not null and rec.name is not null) and
     (rec.sid <> r.sid or rec.name <> r.name) then
     if rec.count_rows > 0 then
       rec.avg_value := rec.avg_value / rec.count_rows;
       if run is not null and run > 0 then
         rec.counter_dev := rise/run;
       end if;
       return next rec;
     end if;
     rec.avg_value := 0;
     rec.count_rows := 0;
     rec.counter_dev := null;
     rise := 0;
     run := 0;
     last_value := null;
     last_row_whence := null;
  end if;
  rec.sid := r.sid;
  rec.name := r.name;
  if r.in_window then
    if r.value is not null then
      rec.count_rows := rec.count_rows + 1;
      rec.avg_value := rec.avg_value + coalesce(r.value,0);
      if     last_row_whence is not null
         and last_value is not null
         and last_value <= r.value then
        rise := rise + (r.value - last_value);
        run := run + ((extract(epoch from r.whence) +
                       (extract(milliseconds from r.whence)::integer % 1000)/1000.0) -
                      (extract(epoch from last_row_whence) +
                       (extract(milliseconds from last_row_whence)::integer % 1000)/1000.0));
      end if;
    end if;
  end if;
  if r.value is not null then
    last_row_whence := r.whence;
    last_value := r.value;
  end if;
  end loop;
  if rec.count_rows > 0 then
    rec.avg_value := rec.avg_value / rec.count_rows;
    if run is not null and run > 0 then
      rec.counter_dev := rise/run;
    end if;
    return next rec;
  end if;
return;
end;
$BODY$
  LANGUAGE 'plpgsql' SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION stratcon.window_robust_derive(timestamp with time zone) TO stratcon;

