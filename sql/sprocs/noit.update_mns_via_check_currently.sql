set search_path = noit,pg_catalog;

CREATE OR REPLACE FUNCTION update_mns_via_check_currently() 
RETURNS trigger
AS $$
BEGIN
   UPDATE metric_name_summary SET fts_data=stratcon.metric_name_summary_compile_fts_data(sid, metric_name, metric_type) WHERE sid = NEW.sid;
   RETURN NEW;
END
$$
LANGUAGE plpgsql
SECURITY DEFINER
;


