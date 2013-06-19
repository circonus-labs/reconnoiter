-- Function: stratcon.delete_metrics(integer, text)

-- DROP FUNCTION stratcon.delete_metrics(integer, text);

CREATE OR REPLACE FUNCTION stratcon.delete_metrics(IN in_sid integer, IN in_metric text)
  RETURNS TABLE(table_name text, deleted_rows numeric) AS
$BODY$DECLARE
  delcount numeric;
  metriccount numeric;
  graphcount numeric;
BEGIN

  -- check if the input matches any metrics
  SELECT count(*) INTO metriccount FROM noit.metric_name_summary WHERE sid=in_sid AND metric_name LIKE in_metric;
  IF metriccount=0 THEN
    RAISE EXCEPTION 'No metrics match provided criteria: SID=%, Metric name=%', in_sid, in_metric;
  END IF;

  RAISE NOTICE 'Deleting % metrics data', metriccount;

  -- check if the metrics are used in any graph, if yes, refuse to delete them
  SELECT count(*) INTO graphcount FROM prism.saved_graphs_dep WHERE sid=in_sid and metric_name LIKE in_metric;
  IF graphcount>0 THEN
    RAISE EXCEPTION 'Cannot delete metrics (sid=%, metric=%), % graphs depend on them', in_sid, in_metric, graphcount;
  END IF;
  
  -- do the deletes
  DELETE FROM noit.metric_numeric_rollup_5m WHERE sid=in_sid and name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_numeric_rollup_5m'::text, delcount;

  DELETE FROM noit.metric_numeric_rollup_20m WHERE sid=in_sid and name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_numeric_rollup_20m'::text, delcount;

  DELETE FROM noit.metric_numeric_rollup_30m WHERE sid=in_sid and name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_numeric_rollup_30m'::text, delcount;

  DELETE FROM noit.metric_numeric_rollup_1hour WHERE sid=in_sid and name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_numeric_rollup_1hour'::text, delcount;

  DELETE FROM noit.metric_numeric_rollup_4hour WHERE sid=in_sid and name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_numeric_rollup_4hour'::text, delcount;

  DELETE FROM noit.metric_numeric_rollup_1day WHERE sid=in_sid and name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_numeric_rollup_1day'::text, delcount;
  
  DELETE FROM noit.metric_tag WHERE sid=in_sid and metric_name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_tag'::text, delcount;

  DELETE FROM noit.metric_text_changelog WHERE sid=in_sid and name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_text_changelog'::text, delcount;

  DELETE FROM noit.metric_text_currently WHERE sid=in_sid and name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_text_currently'::text, delcount;

  DELETE FROM noit.metric_name_summary WHERE sid=in_sid and metric_name LIKE in_metric;
  GET DIAGNOSTICS delcount = ROW_COUNT;
  return query select 'metric_name_summary'::text, delcount;

END$BODY$
  LANGUAGE plpgsql VOLATILE
  COST 10000
  ROWS 1000;

ALTER FUNCTION stratcon.delete_metrics(integer, text)
  OWNER TO reconnoiter;

COMMENT ON FUNCTION stratcon.delete_metrics(integer, text) IS 'Delete unwanted metric data. Will refuse to delete metrics used in graphs. Does not delete from metric archives, as these should be deleted separately.

Input:
  in_sid - SID of the check, if uknown, the SID can be derived from check UUID by map_uuid_to_sid function
  in_metric - metric name pattern, can use the standard LIKE % wildcard';
