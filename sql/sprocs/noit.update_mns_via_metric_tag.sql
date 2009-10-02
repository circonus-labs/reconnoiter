-- formerly stratcon.trig_update_tsvector_from_metric_tags

CREATE OR REPLACE FUNCTION noit.update_mns_via_metric_tag()
  RETURNS trigger AS
$BODY$
DECLARE
BEGIN
    UPDATE noit.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(NEW.sid,NEW.metric_name,metric_type)
    where sid=NEW.sid and metric_name=NEW.metric_name;
   RETURN NEW;
END
$BODY$
  LANGUAGE 'plpgsql'  SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION noit.update_mns_via_metric_tag() TO stratcon;
