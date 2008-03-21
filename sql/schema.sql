BEGIN;

-- Tables 

CREATE TABLE stratcon.loading_dock_check_s (
    sid integer NOT NULL,
    remote_address inet,
    whence timestamp with time zone NOT NULL,
    id uuid NOT NULL,
    target text NOT NULL,
    module text NOT NULL,
    name text NOT NULL,
    PRIMARY KEY(sid,id,whence)
);

CREATE TABLE stratcon.loading_dock_status_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    state character(1) NOT NULL,
    availability character(1) NOT NULL,
    duration integer NOT NULL,
    status text,
    PRIMARY KEY(sid,whence)
);

CREATE TABLE stratcon.loading_dock_metric_numeric_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value numeric,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_text_s (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_text_s_change_log (
    sid integer NOT NULL,
    whence timestamp with time zone NOT NULL,
    name text NOT NULL,
    value text,
    PRIMARY KEY(whence,sid,name)
);

CREATE TABLE stratcon.loading_dock_metric_numeric_s_rullup_60m (
    sid integer NOT NULL,
    name text NOT NULL,
    rollup_time timestamp with time zone NOT NULL,
    count_rows integer,
    avg_value numeric,
    stddev_value numeric,
    min_value numeric,
    max_value numeric,
    PRIMARY KEY (rollup_time,sid,name)
);

CREATE TABLE stratcon.map_uuid_to_sid (
    id uuid NOT NULL,
    sid integer NOT NULL,
    PRIMARY KEY(id,sid)
);

CREATE TABLE stratcon.log_whence_s (
    whence timestamp with time zone NOT NULL,
    PRIMARY KEY(whence)
);

-- Schema Sequence 

CREATE SEQUENCE stratcon.seq_sid
    START WITH 50
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;


-- Function To generate SID from ID 

CREATE OR REPLACE FUNCTION stratcon.generate_sid_from_id(v_in_id uuid)
RETURNS integer
AS $$
DECLARE
   v_ex_sid integer;
   v_new_sid integer;
   v_sql text;
BEGIN

SELECT sid FROM stratcon.map_uuid_to_sid WHERE id=v_in_id
  INTO v_ex_sid;

 IF NOT FOUND THEN
    SELECT nextval('stratcon.seq_sid') 
    INTO v_new_sid;

    v_sql:='INSERT INTO stratcon.map_uuid_to_sid(id,sid) VALUES ('||quote_literal(v_in_id)||','||v_new_sid||');';
    EXECUTE v_sql;   
   
    RETURN v_new_sid;
 ELSE
      RETURN v_ex_sid;
 END IF;

END
$$ LANGUAGE plpgsql;

-- Trigger Function to change Metrix Text Changes 

CREATE TRIGGER loading_dock_metric_text_s_change_log
    AFTER INSERT ON loading_dock_metric_text_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_text_s_change_log();

CREATE FUNCTION stratcon.loading_dock_metric_text_s_change_log() RETURNS trigger
    AS $$
DECLARE
    v_oldvalue TEXT;
BEGIN

IF TG_OP = 'INSERT' THEN
    SELECT value FROM  stratcon.loading_dock_metric_text_s WHERE sid = NEW.sid AND name = NEW.name 
        AND WHENCE = (SELECT max(whence) FROM stratcon.loading_dock_metric_text_s_change_log 
                        WHERE WHENCE <> NEW.WHENCE and sid=NEW.sid and name=NEW.name )
    INTO v_oldvalue;

    IF v_oldvalue IS DISTINCT FROM NEW.value THEN

        INSERT INTO stratcon.loading_dock_metric_text_s_change_log (sid,whence,name,value)
            VALUES (NEW.sid, NEW.whence, NEW.name, NEW.value); 

    END IF;

ELSE
        RAISE EXCEPTION 'Non-INSERT DML operation attempted on INSERT only table';
END IF;

    RETURN NULL;

END
$$
    LANGUAGE plpgsql;
    
-- Trigger on Metrix Numeric to log last inserted timestamp 

CREATE TRIGGER loading_dock_metric_numeric_s_whence_log
    AFTER INSERT ON loading_dock_metric_numeric_s
    FOR EACH ROW
    EXECUTE PROCEDURE loading_dock_metric_numeric_s_whence_log();

    
CREATE FUNCTION stratcon.loading_dock_metric_numeric_s_whence_log() RETURNS trigger
    AS $$
DECLARE
v_whence timestamptz;
BEGIN
IF TG_OP = 'INSERT' THEN
    SELECT whence FROM stratcon.log_whence_s WHERE whence=NEW.whence
     INTO v_whence;
   IF NOT FOUND THEN
       UPDATE stratcon.log_whence_s SET whence=NEW.whence;
   END IF;
ELSE
        RAISE EXCEPTION 'Non-INSERT DML operation attempted on INSERT only table';
END IF;

    RETURN NULL;
END
$$
    LANGUAGE plpgsql;



-- GRANTS 

 GRANT SELECT,INSERT ON stratcon.loading_dock_status_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_check  TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_status TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_numeric TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_text TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_check_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_numeric_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_text_s_change_log TO stratcon;
 GRANT SELECT,INSERT,UPDATE ON stratcon.log_whence_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_text_s TO stratcon;
 GRANT SELECT,INSERT ON stratcon.loading_dock_metric_numeric_s_rullup_60m TO stratcon;
 GRANT SELECT,INSERT ON stratcon.map_uuid_to_sid TO stratcon;
 ALTER TABLE stratcon.seq_sid OWNER TO stratcon;

COMMIT;

