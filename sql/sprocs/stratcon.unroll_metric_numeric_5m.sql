
CREATE OR REPLACE FUNCTION stratcon.unroll_metric_numeric_5m
(in_sid integer, in_name text, in_start timestamp with time zone, in_end timestamp with time zone) 
RETURNS SETOF stratcon.metric_numeric_rollup_segment
AS $$
DECLARE
   v_row   stratcon.metric_numeric_rollup_segment%rowtype;
   v_begin TIMESTAMPTZ := in_start;
   v_end   TIMESTAMPTZ := in_end;
   v_min_i INT4;
   v_max_i INT4;
   v_rollup_time timestamptz;
   i       INT4;
   temprec RECORD;
BEGIN
   LOOP
       EXIT WHEN v_begin > in_end;
       v_rollup_time := date_trunc('day', v_begin AT TIME ZONE 'UTC') AT TIME ZONE 'UTC';
       v_end := LEAST( v_rollup_time + '1 day'::INTERVAL - '1 second'::INTERVAL, in_end );
       SELECT * INTO temprec FROM metric_numeric_rollup_5m WHERE sid = in_sid AND "name" = in_name AND rollup_time = v_rollup_time;
       v_min_i := (ceil(extract('epoch' FROM v_begin ) / 300.0))::INT8 % 288;
       v_max_i := (extract('epoch' FROM v_end )::INT8 / 300) % 288;
       v_row.sid  := in_sid;
       v_row.name := in_name;
       FOR i in v_min_i .. v_max_i LOOP
           v_row.rollup_time := v_rollup_time + '5 minutes'::INTERVAL * i;
           v_row.count_rows  := temprec.count_rows[i + array_lower( temprec.count_rows, 1 ) ];
           v_row.avg_value   := NULL;
           v_row.counter_dev := NULL;
           IF v_row.count_rows > 0 THEN
               v_row.avg_value   := temprec.avg_value[i + array_lower( temprec.avg_value, 1 ) ];
               v_row.counter_dev := temprec.counter_dev[i + array_lower( temprec.counter_dev, 1 ) ];
           END IF;
           RETURN next v_row;
       END LOOP;
       v_begin := date_trunc( 'day', ( v_begin + '1 day'::INTERVAL) AT TIME ZONE 'UTC') AT TIME ZONE 'UTC';
   END LOOP;
END;
$$
LANGUAGE plpgsql
SECURITY DEFINER;


