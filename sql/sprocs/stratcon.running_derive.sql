DROP TYPE IF EXISTS stratcon.three_way_split;
CREATE TYPE stratcon.three_way_split AS (sid integer,  name text, rollup_time timestamp with time zone, value numeric, derivative numeric, counter numeric);      


CREATE OR REPLACE FUNCTION stratcon.running_derive(in_start_time timestamp with time zone)
  RETURNS SETOF stratcon.three_way_split AS
$BODY$
declare
  rec stratcon.three_way_split%rowtype;
  r record;
  rise numeric;
  last_row_whence timestamp;
  last_value numeric;
  run numeric;
  v_sql text;
  v_in_minus_5 timestamptz := in_start_time - '5 minutes'::interval;
  v_in_minus_10 timestamptz := in_start_time - '10 minutes'::interval;

begin

   rec.sid := null;
   rec.name := null;
   rec.value := 0;
   rise := 0;
   run := 0;
   rec.rollup_time = in_start_time;
   v_sql := 'SELECT sid, name, whence,
                   (whence > '||quote_literal(v_in_minus_5)||'::timestamptz) as in_window,
                   value
              FROM metric_numeric_archive
             WHERE whence <= '||quote_literal(in_start_time)||'::timestamptz
               AND whence > '||quote_literal(v_in_minus_10)||'::timestamptz
          order BY sid,name,whence
             ';

  for r in EXECUTE v_sql
  loop
  if (rec.sid is not null and rec.name is not null) and
     (rec.sid <> r.sid or rec.name <> r.name) then
     rise := 0;
     run := 0;
     last_value := null;
     last_row_whence := null;
  end if;
  rec.sid := r.sid;
  rec.name := r.name;
  if r.in_window then
    rec.derivative := null;
    rec.counter := null;
    rec.value := null;
    if r.value is not null then
      rec.value = r.value;
      if     last_row_whence is not null
         and last_value is not null
         and last_value <= r.value then
        rise := rise + (r.value - last_value);
        run := run + ((extract(epoch from r.whence) +
                       (extract(milliseconds from r.whence)::integer % 1000)/1000.0) -
                      (extract(epoch from last_row_whence) +
                       (extract(milliseconds from last_row_whence)::integer % 1000)/1000.0));
        if run is not null and rise is not null then
          rec.derivative = rise / run;
          if rise >= 0 then
            rec.counter = rise / run;
          end if;
        end if;
      end if;
    end if;
    return next rec;
  end if;
  if r.value is not null then
    last_row_whence := r.whence;
    last_value := r.value;
  end if;
  end loop;
return;
end;
$BODY$
  LANGUAGE 'plpgsql' SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION stratcon.window_robust_derive(timestamp with time zone) TO stratcon;

