-- formerly trigger function stratcon.loading_dock_metric_text_s_change_log

CREATE OR REPLACE FUNCTION noit.metric_text_archive_log_changes()
  RETURNS trigger AS
$BODY$
DECLARE
    v_oldvalue text;
    v_sid integer;
    v_name text;
    v_value text;
    v_whence timestamptz;
    v_old_whence timestamptz;
    v_old_name text;
    v_old_sid integer;
    v_old_value text;
    v_max_whence timestamptz;
BEGIN

IF TG_OP = 'INSERT' THEN

             SELECT value FROM  noit.metric_text_changelog WHERE sid = NEW.sid AND name = NEW.name
                 AND WHENCE = (SELECT max(whence) FROM noit.metric_text_changelog
                                 WHERE WHENCE <> NEW.WHENCE and sid=NEW.sid and name=NEW.name )
                     INTO v_oldvalue;

                    IF v_oldvalue IS DISTINCT FROM NEW.value THEN

                        INSERT INTO noit.metric_text_changelog (sid,whence,name,value)
                            VALUES (NEW.sid, NEW.whence, NEW.name, NEW.value);
                        DELETE FROM noit.metric_text_currently
                                WHERE sid = NEW.sid and name = NEW.name;
                        INSERT INTO noit.metric_text_currently (sid,whence,name,value)
                                VALUES (NEW.sid, NEW.whence, NEW.name, NEW.value);
                    END IF;

SELECT sid,metric_name FROM noit.metric_name_summary WHERE sid=NEW.sid  and metric_name=NEW.name
        INTO v_sid,v_name;
     IF NOT FOUND THEN
          INSERT INTO  noit.metric_name_summary(sid,metric_name,metric_type)  VALUES(NEW.sid,NEW.name,'text');
     END IF;

ELSE
        RAISE EXCEPTION 'Error in trigger function noit.metric_text_archive_log_changes ';
END IF;
    RETURN NULL;
END
$BODY$
  LANGUAGE 'plpgsql'  SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION noit.metric_text_archive_log_changes() TO stratcon;

