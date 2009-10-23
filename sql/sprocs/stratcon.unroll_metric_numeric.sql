
CREATE OR REPLACE FUNCTION stratcon.unroll_metric_numeric
(in_sid integer, in_name text, in_start timestamp with time zone, in_end timestamp with time zone, in_roll text) 
RETURNS SETOF stratcon.metric_numeric_rollup_segment
AS $$
DECLARE
   v_row   stratcon.metric_numeric_rollup_segment%rowtype;
   v_begin TIMESTAMPTZ;
   v_end   TIMESTAMPTZ;
   adj_end TIMESTAMPTZ;
   v_min_i INT4;
   v_max_i INT4;
   v_rollup_time timestamptz;
   v_conf  RECORD;
   i       INT4;
   temprec RECORD;
BEGIN
   SELECT * FROM metric_numeric_rollup_config WHERE rollup = in_roll INTO v_conf; 
   IF NOT FOUND THEN
       RAISE EXCEPTION 'Unknown rollup %', in_roll;
   END IF;
   v_begin := 'epoch'::timestamptz + (floor(extract('epoch' FROM in_start) / v_conf.seconds) * v_conf.seconds) * '1 second'::interval;
   adj_end := 'epoch'::timestamptz + (floor(extract('epoch' FROM in_end) / v_conf.seconds) * v_conf.seconds) * '1 second'::interval;
   LOOP
       EXIT WHEN v_begin > adj_end;
       v_rollup_time := 'epoch'::timestamptz + (floor(extract('epoch' FROM v_begin) / v_conf.span) * v_conf.span) * '1 second'::interval;
       v_end := LEAST( v_rollup_time + (v_conf.span * '1 second'::interval) - '1 second'::interval, adj_end );
       execute 'SELECT * FROM metric_numeric_rollup_' || in_roll || ' ' ||
               ' WHERE sid = ' || in_sid || ' AND "name" = ' || quote_literal(in_name) || ' ' ||
               '   AND rollup_time = ' || quote_literal(v_rollup_time) INTO temprec;
       v_min_i := (ceil(extract('epoch' FROM v_begin ) / v_conf.seconds))::INT8 % (v_conf.span / v_conf.seconds);
       v_max_i := (extract('epoch' FROM v_end )::INT8 / v_conf.seconds) % (v_conf.span / v_conf.seconds);
       v_row.sid  := in_sid;
       v_row.name := in_name;
       FOR i in v_min_i .. v_max_i LOOP
           v_row.rollup_time := v_rollup_time + '1 second'::INTERVAL * i * v_conf.seconds;
           v_row.count_rows  := temprec.count_rows[i + array_lower( temprec.count_rows, 1 ) ];
           v_row.avg_value   := NULL;
           v_row.counter_dev := NULL;
           IF v_row.count_rows > 0 THEN
               v_row.avg_value   := temprec.avg_value[i + array_lower( temprec.avg_value, 1 ) ];
               v_row.counter_dev := temprec.counter_dev[i + array_lower( temprec.counter_dev, 1 ) ];
           END IF;
           RETURN next v_row;
       END LOOP;
       -- add a span
       v_begin := v_begin + v_conf.span * '1 second'::interval;
       -- trunc to the beginning of a span
       v_begin := 'epoch'::timestamptz + (floor(extract('epoch' FROM v_begin) / v_conf.span) * v_conf.span) * '1 second'::interval;
   END LOOP;
END;
$$
LANGUAGE plpgsql
SECURITY DEFINER;


