-- formerly  trigger function stratcon.loading_dock_status_s_change_log 

CREATE OR REPLACE FUNCTION noit.check_status_archive_log_changes()
  RETURNS trigger AS
$BODY$
DECLARE
    v_state CHAR(1);
    v_avail CHAR(1);
    v_whence timestamp with time zone;
BEGIN

IF TG_OP = 'INSERT' THEN
    SELECT state,availability,whence FROM  noit.check_status_changelog WHERE sid = NEW.sid
        AND WHENCE = (SELECT max(whence) FROM noit.check_status_changelog
                        WHERE  SID=NEW.sid and  WHENCE <> NEW.whence )
    INTO v_state,v_avail,v_whence;

    IF NEW.whence > v_whence AND 
       (v_state IS DISTINCT FROM NEW.state OR v_avail IS DISTINCT FROM NEW.availability) THEN
        INSERT INTO noit.check_status_changelog (sid,whence,state,availability,duration,status)
            VALUES (NEW.sid,NEW.whence,NEW.state,NEW.availability,NEW.duration,NEW.status);
    END IF;

ELSE
        RAISE EXCEPTION 'Error in trigger function noit.check_status_archive_log_changes()';

END IF;

    RETURN NULL;
END
$BODY$
  LANGUAGE 'plpgsql'  SECURITY DEFINER;
 
GRANT EXECUTE ON FUNCTION noit.check_status_archive_log_changes() TO stratcon;
