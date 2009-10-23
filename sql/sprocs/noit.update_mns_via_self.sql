-- formerly stratcon.trig_update_tsvector_from_metric_summary 

set search_path = noit, pg_catalog;

CREATE OR REPLACE FUNCTION update_mns_via_self()
RETURNS trigger AS
$BODY$
BEGIN
IF TG_OP = 'UPDATE' THEN
    IF NEW.metric_name <> OLD.metric_name THEN
        RETURN NEW;
    END IF;
END IF;

NEW.fts_data = stratcon.metric_name_summary_compile_fts_data(NEW.sid, NEW.metric_name, NEW.metric_type);

RETURN new;
END
$BODY$
LANGUAGE 'plpgsql'  
SECURITY DEFINER;

