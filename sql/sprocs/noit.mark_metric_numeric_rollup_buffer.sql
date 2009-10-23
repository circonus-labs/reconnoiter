-- formerly stratcon.loading_dock_metric_numeric_s_whence_log

CREATE OR REPLACE FUNCTION noit.mark_metric_numeric_rollup_buffer()
  RETURNS trigger AS
$BODY$
DECLARE
v_whence timestamptz;
v_whence_5 timestamptz;
v_sid integer;
v_name text;
BEGIN
IF TG_OP = 'INSERT' THEN
 
 v_whence_5:=date_trunc('H',NEW.WHENCE) + (round(extract('minute' from NEW.WHENCE)/5)*5) * '1 minute'::interval;
 
   SELECT whence FROM noit.metric_numeric_rollup_queue WHERE whence=v_whence_5 and interval='5m'
     INTO v_whence;
     
   IF NOT FOUND THEN
      BEGIN
       INSERT INTO  noit.metric_numeric_rollup_queue VALUES(v_whence_5,'5m');
       EXCEPTION
        WHEN UNIQUE_VIOLATION THEN
        -- do nothing 
      END;
    END IF;

   SELECT sid,metric_name FROM noit.metric_name_summary WHERE sid=NEW.sid  and metric_name=NEW.name
     INTO v_sid,v_name;
   IF NOT FOUND THEN
       INSERT INTO  noit.metric_name_summary VALUES(NEW.sid,NEW.name,'numeric');
    END IF;

END IF;
    RETURN NULL;
END
$BODY$
  LANGUAGE 'plpgsql'   SECURITY DEFINER;

GRANT EXECUTE ON FUNCTION noit.mark_metric_numeric_rollup_buffer() TO stratcon;
