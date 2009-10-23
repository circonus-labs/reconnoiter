-- formerly stratcon.trig_update_tsvector_from_check_tags()

set search_path = noit, pg_catalog;

CREATE OR REPLACE FUNCTION update_mns_via_check_tag()
RETURNS trigger AS
$BODY$
BEGIN
    UPDATE metric_name_summary SET fts_data=stratcon.metric_name_summary_compile_fts_data(NEW.sid,metric_name,metric_type) WHERE sid=NEW.sid;
    RETURN NEW;
END
$BODY$
LANGUAGE 'plpgsql' 
SECURITY DEFINER;

