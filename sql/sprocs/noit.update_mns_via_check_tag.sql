-- formerly stratcon.trig_update_tsvector_from_check_tags()

CREATE OR REPLACE FUNCTION noit.update_mns_via_check_tag()
  RETURNS trigger AS
$BODY$
DECLARE
BEGIN
    UPDATE noit.metric_name_summary SET fts_data=stratcon.metric_name_summary_tsvector(NEW.sid,metric_name,metric_type)
    where sid=NEW.sid;
   RETURN NEW;
END
$BODY$
  LANGUAGE 'plpgsql'   SECURITY DEFINER;

GRANT EXECUTE ON FUNCTION noit.update_mns_via_check_tag() TO stratcon;
