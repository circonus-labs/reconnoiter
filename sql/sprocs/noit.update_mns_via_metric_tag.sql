-- formerly stratcon.trig_update_tsvector_from_metric_tags

set search_path = noit, pg_catalog; 

CREATE OR REPLACE FUNCTION update_mns_via_metric_tag()
RETURNS trigger AS
$BODY$
BEGIN
    UPDATE metric_name_summary SET fts_data=stratcon.metric_name_summary_compile_fts_data(NEW.sid,NEW.metric_name,metric_type)
    WHERE sid=NEW.sid AND metric_name=NEW.metric_name;
   RETURN NEW;
END
$BODY$
LANGUAGE 'plpgsql' 
SECURITY DEFINER;
 
