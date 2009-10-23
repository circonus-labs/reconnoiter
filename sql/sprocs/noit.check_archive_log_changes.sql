-- formerly check_currently

set search_path = noit; 

CREATE OR REPLACE FUNCTION check_archive_log_changes()
RETURNS trigger 
AS $$
DECLARE
    v_remote_address INET;
    v_target TEXT;
    v_name TEXT;
BEGIN

IF TG_OP = 'INSERT' THEN
    SELECT remote_address,target,name FROM  check_currently WHERE sid = NEW.sid AND id=NEW.id 
        INTO v_remote_address,v_target,v_name;

    IF v_remote_address IS DISTINCT FROM NEW.remote_address OR v_target IS DISTINCT FROM NEW.target OR v_name IS DISTINCT FROM NEW.name   THEN
        
        DELETE from check_currently WHERE sid = NEW.sid AND id=NEW.id;
        
        INSERT INTO check_currently (sid,remote_address,whence,id,target,module,name)
            VALUES (NEW.sid,NEW.remote_address,NEW.whence,NEW.id,NEW.target,NEW.module,NEW.name); 

    END IF;

ELSE
        RAISE EXCEPTION 'Something wrong with check_archive_log_changes';
END IF;

    RETURN NULL;

END
$$ LANGUAGE plpgsql
SECURITY DEFINER
;

