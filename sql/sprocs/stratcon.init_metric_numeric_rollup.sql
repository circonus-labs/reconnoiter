set search_path = noit, pg_catalog; 

DROP TYPE IF EXISTS stratcon.metric_numeric_rollup CASCADE;  
CREATE TYPE stratcon.metric_numeric_rollup AS (sid integer,  name text, rollup_time timestamp with time zone, count_rows integer[], avg_value numeric[], counter_dev numeric[]);  

CREATE OR REPLACE FUNCTION stratcon.init_metric_numeric_rollup(in_rollup text)
RETURNS SETOF stratcon.metric_numeric_rollup 
AS $$
  SELECT null::int, null::text, null::timestamptz, array_agg(count_rows), array_agg(avg_value), array_agg(counter_dev) from (select NULL::int as count_rows, NULL::numeric as avg_value, NULL::numeric as counter_dev from generate_series(1,(select span/seconds from metric_numeric_rollup_config where rollup = $1))) x;
$$ 
LANGUAGE 'sql'
SECURITY DEFINER 
; 


