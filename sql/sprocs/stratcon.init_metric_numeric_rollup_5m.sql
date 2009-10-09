CREATE OR REPLACE FUNCTION stratcon.init_metric_numeric_rollup_5m()
RETURNS SETOF noit.metric_numeric_rollup_5m 
AS $$
  SELECT null::int, null::text, null::timestamptz, array_agg(count_rows), array_agg(avg_value), array_agg(counter_dev) from (select NULL::int as count_rows, NULL::numeric as avg_value, NULL::numeric as counter_dev from generate_series(1,288)) x;
$$ 
LANGUAGE 'sql'
SECURITY DEFINER 
; 


