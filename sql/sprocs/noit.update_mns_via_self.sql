-- formerly stratcon.trig_update_tsvector_from_metric_summary
CREATE OR REPLACE FUNCTION noit.update_mns_via_self()
  RETURNS trigger AS
$BODY$
DECLARE
 BEGIN
 IF TG_OP != 'INSERT' THEN
   IF (NEW.metric_name <> OLD.metric_name) THEN
           UPDATE noit.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(NEW.sid,NEW.metric_name,NEW.metric_type)
             where sid=NEW.sid and metric_name=NEW.metric_name and metric_type = NEW.metric_type;
   END IF;    
 ELSE 
    UPDATE noit.metric_name_summary SET ts_search_all=stratcon.metric_name_summary_tsvector(NEW.sid,NEW.metric_name,NEW.metric_type)
            where sid=NEW.sid and metric_name=NEW.metric_name and metric_type = NEW.metric_type;
 END IF;  
   RETURN NEW;
END
$BODY$
  LANGUAGE 'plpgsql'  SECURITY DEFINER;

GRANT EXECUTE ON FUNCTION noit.update_mns_via_self() TO stratcon;
