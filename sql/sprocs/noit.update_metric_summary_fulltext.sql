-- formerly stratcon.trig_update_tsvector_from_mv_dock

CREATE OR REPLACE FUNCTION noit.update_metric_summary_fulltext()
  RETURNS trigger AS
$BODY$
DECLARE
BEGIN
    UPDATE noit.metric_name_summary SET fts_data=stratcon.metric_name_summary_tsvector(sid, metric_name, metric_type) WHERE sid = NEW.sid;
   RETURN NEW;
END
$BODY$
  LANGUAGE 'plpgsql'  SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION noit.update_metric_summary_fulltext() TO stratcon;

